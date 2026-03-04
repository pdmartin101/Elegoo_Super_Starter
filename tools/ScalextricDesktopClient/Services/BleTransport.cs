using System.Runtime.InteropServices.WindowsRuntime;
using Windows.Devices.Bluetooth;
using Windows.Devices.Bluetooth.Advertisement;
using Windows.Devices.Bluetooth.GenericAttributeProfile;

namespace ScalextricDesktopClient.Services;

public class BleDevice
{
    public string Name { get; set; } = "";
    public ulong Address { get; set; }
    public override string ToString() => $"{Name} ({Address:X12})";
}

public class BleTransport : ITransportClient
{
    private static readonly Guid ServiceUuid = Guid.Parse("a1b2c3d4-e5f6-7890-abcd-ef1234567890");
    private static readonly Guid EventCharUuid = Guid.Parse("a1b2c3d4-e5f6-7890-abcd-ef1234567891");
    private static readonly Guid SyncCharUuid = Guid.Parse("a1b2c3d4-e5f6-7890-abcd-ef1234567892");

    private BluetoothLEDevice? _device;
    private GattCharacteristic? _eventChar;
    private GattCharacteristic? _syncChar;
    private bool _connected;
    private TaskCompletionSource<(string Reply, DateTime ReceiveTime)>? _syncTcs;

    public string Name => "BLE";
    public event Action<string>? MessageReceived;
    public event Action<string>? LogMessage;
    public event Action? Disconnected;
    public bool IsConnected => _connected;

    public BleDevice? SelectedDevice { get; set; }

    public event Action<BleDevice>? DeviceFound;

    private BluetoothLEAdvertisementWatcher? _watcher;

    public void StartScan()
    {
        _watcher = new BluetoothLEAdvertisementWatcher
        {
            ScanningMode = BluetoothLEScanningMode.Active
        };
        _watcher.AdvertisementFilter.Advertisement.ServiceUuids.Add(ServiceUuid);

        var seen = new HashSet<ulong>();
        _watcher.Received += async (sender, args) =>
        {
            if (!seen.Add(args.BluetoothAddress)) return;
            var name = args.Advertisement.LocalName;
            if (string.IsNullOrEmpty(name))
            {
                // Name is in scan response which WinRT may merge — try reading from device
                try
                {
                    using var dev = await BluetoothLEDevice.FromBluetoothAddressAsync(args.BluetoothAddress);
                    if (dev != null) name = dev.Name;
                }
                catch { }
            }
            if (string.IsNullOrEmpty(name)) name = "Scalextric";
            DeviceFound?.Invoke(new BleDevice { Name = name, Address = args.BluetoothAddress });
        };
        _watcher.Start();
        LogMessage?.Invoke("Scanning for BLE devices...");
    }

    public void StopScan()
    {
        _watcher?.Stop();
        _watcher = null;
    }

    public async Task ConnectAsync(CancellationToken ct)
    {
        if (SelectedDevice == null)
            throw new InvalidOperationException("No BLE device selected.");

        StopScan();
        // Let BLE adapter settle after scanning
        await Task.Delay(500, ct);

        const int maxAttempts = 5;
        for (int attempt = 1; attempt <= maxAttempts; attempt++)
        {
            ct.ThrowIfCancellationRequested();
            LogMessage?.Invoke(attempt == 1
                ? $"Connecting to {SelectedDevice.Name}..."
                : $"Retry {attempt}/{maxAttempts}...");

            try
            {
                await ConnectInternalAsync(ct);
                return;
            }
            catch (Exception ex) when (ex is not OperationCanceledException || !ct.IsCancellationRequested)
            {
                // WinRT BLE failure (timeout, service not found, etc.) — clean up and retry
                LogMessage?.Invoke($"  Attempt {attempt} failed: {ex.Message}");
                await CleanupDevice();
                if (attempt < maxAttempts)
                    await Task.Delay(1000, ct);
            }
        }

        throw new InvalidOperationException($"Failed to connect after {maxAttempts} attempts.");
    }

    private async Task ConnectInternalAsync(CancellationToken ct)
    {
        using var timeout = CancellationTokenSource.CreateLinkedTokenSource(ct);
        timeout.CancelAfter(TimeSpan.FromSeconds(15));

        _device = await BluetoothLEDevice.FromBluetoothAddressAsync(SelectedDevice!.Address)
            .AsTask(timeout.Token);
        if (_device == null)
            throw new InvalidOperationException("Failed to connect to BLE device.");

        var servicesResult = await _device.GetGattServicesForUuidAsync(ServiceUuid)
            .AsTask(timeout.Token);
        if (servicesResult.Status != GattCommunicationStatus.Success || servicesResult.Services.Count == 0)
            throw new InvalidOperationException("Scalextric BLE service not found.");

        var service = servicesResult.Services[0];

        // Get event characteristic (notify)
        var eventChars = await service.GetCharacteristicsForUuidAsync(EventCharUuid)
            .AsTask(timeout.Token);
        if (eventChars.Status == GattCommunicationStatus.Success && eventChars.Characteristics.Count > 0)
        {
            _eventChar = eventChars.Characteristics[0];
            await _eventChar.WriteClientCharacteristicConfigurationDescriptorAsync(
                GattClientCharacteristicConfigurationDescriptorValue.Notify)
                .AsTask(timeout.Token);
            _eventChar.ValueChanged += OnEventValueChanged;
        }

        // Get sync characteristic (write + notify)
        var syncChars = await service.GetCharacteristicsForUuidAsync(SyncCharUuid)
            .AsTask(timeout.Token);
        if (syncChars.Status == GattCommunicationStatus.Success && syncChars.Characteristics.Count > 0)
        {
            _syncChar = syncChars.Characteristics[0];
            await _syncChar.WriteClientCharacteristicConfigurationDescriptorAsync(
                GattClientCharacteristicConfigurationDescriptorValue.Notify)
                .AsTask(timeout.Token);
            _syncChar.ValueChanged += OnSyncValueChanged;
        }

        // Subscribe to disconnect AFTER all GATT setup is complete
        // (WinRT BLE can fire transient disconnect events during discovery)
        _device.ConnectionStatusChanged += OnConnectionStatusChanged;
        _connected = true;
        LogMessage?.Invoke("Connected!");
    }

    private async Task CleanupDevice()
    {
        if (_device != null)
        {
            _device.ConnectionStatusChanged -= OnConnectionStatusChanged;
            _device.Dispose();
            _device = null;
        }
        _eventChar = null;
        _syncChar = null;
        await Task.CompletedTask;
    }

    public async Task<(string? Reply, DateTime ReceiveTime)> SendSyncAsync(CancellationToken ct)
    {
        if (_syncChar == null) return (null, DateTime.UtcNow);

        _syncTcs = new TaskCompletionSource<(string, DateTime)>();
        var syncBytes = System.Text.Encoding.UTF8.GetBytes("SYNC").AsBuffer();
        var writeResult = await _syncChar.WriteValueAsync(syncBytes);
        if (writeResult != GattCommunicationStatus.Success) return (null, DateTime.UtcNow);

        using var cts = CancellationTokenSource.CreateLinkedTokenSource(ct);
        cts.CancelAfter(TimeSpan.FromSeconds(2));

        try
        {
            return await _syncTcs.Task.WaitAsync(cts.Token);
        }
        catch (OperationCanceledException)
        {
            return (null, DateTime.UtcNow);
        }
    }

    public async Task DisconnectAsync()
    {
        StopScan();
        _connected = false;

        if (_eventChar != null)
        {
            _eventChar.ValueChanged -= OnEventValueChanged;
            try
            {
                await _eventChar.WriteClientCharacteristicConfigurationDescriptorAsync(
                    GattClientCharacteristicConfigurationDescriptorValue.None);
            }
            catch { }
        }

        if (_syncChar != null)
        {
            _syncChar.ValueChanged -= OnSyncValueChanged;
            try
            {
                await _syncChar.WriteClientCharacteristicConfigurationDescriptorAsync(
                    GattClientCharacteristicConfigurationDescriptorValue.None);
            }
            catch { }
        }

        if (_device != null)
        {
            _device.ConnectionStatusChanged -= OnConnectionStatusChanged;
            _device.Dispose();
            _device = null;
        }

        _eventChar = null;
        _syncChar = null;
    }

    public void Dispose() => DisconnectAsync().GetAwaiter().GetResult();

    private void OnEventValueChanged(GattCharacteristic sender, GattValueChangedEventArgs args)
    {
        var bytes = args.CharacteristicValue.ToArray();
        var message = System.Text.Encoding.UTF8.GetString(bytes);
        MessageReceived?.Invoke(message);
    }

    private void OnSyncValueChanged(GattCharacteristic sender, GattValueChangedEventArgs args)
    {
        var receiveTime = DateTime.UtcNow;
        var bytes = args.CharacteristicValue.ToArray();
        var reply = System.Text.Encoding.UTF8.GetString(bytes);
        // Ignore keepalive PINGs — only complete the TCS for actual SYNC replies
        if (reply.StartsWith("SYNC:"))
            _syncTcs?.TrySetResult((reply, receiveTime));
    }

    private void OnConnectionStatusChanged(BluetoothLEDevice sender, object args)
    {
        if (sender.ConnectionStatus == BluetoothConnectionStatus.Disconnected)
        {
            _connected = false;
            Disconnected?.Invoke();
        }
    }
}
