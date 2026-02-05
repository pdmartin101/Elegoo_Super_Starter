using System.Text;
using Windows.Devices.Bluetooth;
using Windows.Devices.Bluetooth.Advertisement;
using Windows.Devices.Bluetooth.GenericAttributeProfile;
using Windows.Storage.Streams;

// UUIDs must match the ESP32
var SERVICE_UUID = Guid.Parse("4fafc201-1fb5-459e-8fcc-c5c9c331914b");
var CHARACTERISTIC_UUID = Guid.Parse("beb5483e-36e1-4688-b7f5-ea07361b26a8");

Console.WriteLine("BLE Receiver - Scanning for ESP32-BLE-Sender...");
Console.WriteLine("Press Ctrl+C to exit\n");

var watcher = new BluetoothLEAdvertisementWatcher
{
    ScanningMode = BluetoothLEScanningMode.Active
};

BluetoothLEDevice? connectedDevice = null;
GattCharacteristic? characteristic = null;

watcher.Received += async (sender, args) =>
{
    try
    {
        var device = await BluetoothLEDevice.FromBluetoothAddressAsync(args.BluetoothAddress);
        if (device == null) return;

        var name = device.Name;
        if (string.IsNullOrEmpty(name) || !name.Contains("ESP32-BLE-Sender"))
        {
            device.Dispose();
            return;
        }

        Console.WriteLine($"Found: {name} ({args.BluetoothAddress:X})");
        watcher.Stop();

        connectedDevice = device;

        // Get the service
        var servicesResult = await device.GetGattServicesForUuidAsync(SERVICE_UUID);
        if (servicesResult.Status != GattCommunicationStatus.Success || servicesResult.Services.Count == 0)
        {
            Console.WriteLine("Service not found");
            return;
        }

        var service = servicesResult.Services[0];
        Console.WriteLine("Connected to service");

        // Get the characteristic
        var charsResult = await service.GetCharacteristicsForUuidAsync(CHARACTERISTIC_UUID);
        if (charsResult.Status != GattCommunicationStatus.Success || charsResult.Characteristics.Count == 0)
        {
            Console.WriteLine("Characteristic not found");
            return;
        }

        characteristic = charsResult.Characteristics[0];

        // Subscribe to notifications
        characteristic.ValueChanged += (s, e) =>
        {
            var reader = DataReader.FromBuffer(e.CharacteristicValue);
            var bytes = new byte[reader.UnconsumedBufferLength];
            reader.ReadBytes(bytes);
            var message = Encoding.UTF8.GetString(bytes);
            Console.WriteLine($"[{DateTime.Now:HH:mm:ss}] Received: {message}");
        };

        var status = await characteristic.WriteClientCharacteristicConfigurationDescriptorAsync(
            GattClientCharacteristicConfigurationDescriptorValue.Notify);

        if (status == GattCommunicationStatus.Success)
        {
            Console.WriteLine("Subscribed to notifications\n");
        }
        else
        {
            Console.WriteLine($"Failed to subscribe: {status}");
        }
    }
    catch (Exception ex)
    {
        Console.WriteLine($"Error: {ex.Message}");
    }
};

watcher.Start();

Console.CancelKeyPress += (s, e) =>
{
    watcher.Stop();
    characteristic = null;
    connectedDevice?.Dispose();
    Console.WriteLine("\nDisconnected. Goodbye!");
};

// Keep running
await Task.Delay(Timeout.Infinite);
