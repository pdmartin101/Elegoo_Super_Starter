using System.Collections.ObjectModel;
using System.ComponentModel;
using System.Runtime.CompilerServices;
using Avalonia.Threading;
using ScalextricDesktopClient.Models;
using ScalextricDesktopClient.Services;

namespace ScalextricDesktopClient.ViewModels;

public class MainViewModel : INotifyPropertyChanged
{
    private readonly ClockCalibration _clock = new();
    private readonly EventParser _parser;
    private ITransportClient? _transport;
    private CancellationTokenSource? _connectCts;

    private long _expectedSeq = -1;

    // Transport instances
    private readonly WebSocketTransport _wsTransport = new();
    private readonly SerialTransport _serialTransport = new();
    private readonly BleTransport _bleTransport = new();

    public MainViewModel()
    {
        _parser = new EventParser(_clock);
        Events = new ObservableCollection<ScalextricEvent>();
        LogLines = new ObservableCollection<string>();
        BleDevices = new ObservableCollection<BleDevice>();
        SerialPorts = new ObservableCollection<string>();
        TransportNames = new ObservableCollection<string> { "WebSocket", "Serial", "BLE" };
        SelectedTransport = "WebSocket";
        RefreshSerialPorts();
    }

    // --- Observable properties ---

    public ObservableCollection<ScalextricEvent> Events { get; }
    public ObservableCollection<string> LogLines { get; }
    public ObservableCollection<BleDevice> BleDevices { get; }
    public ObservableCollection<string> SerialPorts { get; }
    public ObservableCollection<string> TransportNames { get; }

    private string _selectedTransport = "WebSocket";
    public string SelectedTransport
    {
        get => _selectedTransport;
        set { _selectedTransport = value; OnPropertyChanged(); OnPropertyChanged(nameof(IsWebSocket)); OnPropertyChanged(nameof(IsSerial)); OnPropertyChanged(nameof(IsBle)); }
    }

    public bool IsWebSocket => SelectedTransport == "WebSocket";
    public bool IsSerial => SelectedTransport == "Serial";
    public bool IsBle => SelectedTransport == "BLE";

    private string _status = "Disconnected";
    public string Status
    {
        get => _status;
        set { _status = value; OnPropertyChanged(); }
    }

    private bool _isConnected;
    public bool IsConnected
    {
        get => _isConnected;
        set { _isConnected = value; OnPropertyChanged(); OnPropertyChanged(nameof(CanConnect)); }
    }

    public bool CanConnect => !IsConnected;

    private int _eventCount;
    public int EventCount
    {
        get => _eventCount;
        set { _eventCount = value; OnPropertyChanged(); }
    }

    private int _droppedCount;
    public int DroppedCount
    {
        get => _droppedCount;
        set { _droppedCount = value; OnPropertyChanged(); }
    }

    private string _carCounts = "";
    public string CarCounts
    {
        get => _carCounts;
        set { _carCounts = value; OnPropertyChanged(); }
    }

    private string _calibrationInfo = "";
    public string CalibrationInfo
    {
        get => _calibrationInfo;
        set { _calibrationInfo = value; OnPropertyChanged(); }
    }

    private int _syncRounds = 5;
    public int SyncRounds
    {
        get => _syncRounds;
        set { _syncRounds = Math.Clamp(value, 1, 50); OnPropertyChanged(); }
    }

    // WebSocket config
    private string _wsUrl = "";
    public string WsUrl
    {
        get => _wsUrl;
        set { _wsUrl = value; OnPropertyChanged(); }
    }

    // Serial config
    private string? _selectedPort;
    public string? SelectedPort
    {
        get => _selectedPort;
        set { _selectedPort = value; OnPropertyChanged(); }
    }

    // BLE config
    private BleDevice? _selectedBleDevice;
    public BleDevice? SelectedBleDevice
    {
        get => _selectedBleDevice;
        set { _selectedBleDevice = value; OnPropertyChanged(); }
    }

    private readonly Dictionary<int, int> _carCountMap = new();
    private double _latencySum;
    private int _latencyCount;

    private string _avgLatency = "";
    public string AvgLatency
    {
        get => _avgLatency;
        set { _avgLatency = value; OnPropertyChanged(); }
    }

    // --- Commands ---

    public void RefreshSerialPorts()
    {
        SerialPorts.Clear();
        foreach (var port in SerialTransport.GetAvailablePorts())
            SerialPorts.Add(port);
        if (SerialPorts.Count > 0 && SelectedPort == null)
            SelectedPort = SerialPorts[0];
    }

    public void StartBleScan()
    {
        BleDevices.Clear();
        _bleTransport.DeviceFound += OnBleDeviceFound;
        _bleTransport.StartScan();
    }

    public void StopBleScan()
    {
        _bleTransport.StopScan();
        _bleTransport.DeviceFound -= OnBleDeviceFound;
    }

    private void OnBleDeviceFound(BleDevice device)
    {
        Dispatcher.UIThread.Post(() =>
        {
            BleDevices.Add(device);
            if (SelectedBleDevice == null)
                SelectedBleDevice = device;
        });
    }

    public async Task ConnectAsync()
    {
        if (IsConnected) return;

        _clock.Reset();
        Events.Clear();
        _expectedSeq = -1;
        EventCount = 0;
        DroppedCount = 0;
        _carCountMap.Clear();
        CarCounts = "";
        CalibrationInfo = "";
        AvgLatency = "";
        _latencySum = 0;
        _latencyCount = 0;
        LogLines.Clear();

        _connectCts = new CancellationTokenSource();

        try
        {
            _transport = SelectedTransport switch
            {
                "WebSocket" => ConfigureWs(),
                "Serial" => ConfigureSerial(),
                "BLE" => ConfigureBle(),
                _ => throw new InvalidOperationException()
            };

            _transport.MessageReceived += OnMessage;
            _transport.LogMessage += OnLog;

            Status = "Connecting...";
            await _transport.ConnectAsync(_connectCts.Token);

            // Subscribe to Disconnected AFTER connect succeeds to avoid
            // transient disconnect events during GATT discovery cancelling the CTS
            _transport.Disconnected += OnDisconnected;
            IsConnected = true;
            Status = "Connected - calibrating...";

            // Run SYNC calibration
            await CalibrateAsync(_connectCts.Token);

            Status = _clock.IsCalibrated
                ? $"Connected (boot: {_clock.EspZeroTime.ToLocalTime():HH:mm:ss.fff})"
                : "Connected (calibration pending)";
        }
        catch (Exception ex)
        {
            Status = $"Error: {ex.Message}";
            await DisconnectInternalAsync();
        }
    }

    private async Task CalibrateAsync(CancellationToken ct)
    {
        if (_transport == null) return;

        LogLines.Add($"SYNC calibration ({_syncRounds} rounds):");
        int calibratedAtRound = 0;

        for (int round = 0; round < _syncRounds; round++)
        {
            var sendTime = DateTime.UtcNow;
            var (reply, receiveTime) = await _transport.SendSyncAsync(ct);
            var rtt = receiveTime - sendTime;

            if (reply != null && reply.StartsWith("SYNC:") && long.TryParse(reply[5..], out var espMillis))
            {
                if (rtt.TotalMilliseconds > 100)
                {
                    LogLines.Add($"  Round {round + 1}: RTT={rtt.TotalMilliseconds:F0}ms (slow, skipped)");
                }
                else
                {
                    var recalibrated = _clock.Update(receiveTime, espMillis);
                    var jitter = _clock.GetLatencyMs(receiveTime, espMillis);
                    if (recalibrated) calibratedAtRound = round + 1;
                    LogLines.Add($"  Round {round + 1}: RTT={rtt.TotalMilliseconds:F0}ms, ESP millis={espMillis}, jitter={jitter:F0}ms{(recalibrated ? " *" : "")}");
                }
            }
            else
            {
                LogLines.Add($"  Round {round + 1}: timeout");
            }

            await Task.Delay(100, ct);
        }

        if (_clock.IsCalibrated)
        {
            CalibrationInfo = $"Boot={_clock.EspZeroTime.ToLocalTime():HH:mm:ss.fff} (round {calibratedAtRound})";
            LogLines.Add($"  Boot time: {_clock.EspZeroTime.ToLocalTime():HH:mm:ss.fff} (from round {calibratedAtRound})");
        }
        else
        {
            LogLines.Add("  Calibration failed (will calibrate from events)");
        }
    }

    public async Task DisconnectAsync()
    {
        StopBleScan();
        await DisconnectInternalAsync();
        Status = "Disconnected";
    }

    private async Task DisconnectInternalAsync()
    {
        _connectCts?.Cancel();
        if (_transport != null)
        {
            _transport.MessageReceived -= OnMessage;
            _transport.LogMessage -= OnLog;
            _transport.Disconnected -= OnDisconnected;
            await _transport.DisconnectAsync();
            _transport = null;
        }
        IsConnected = false;
    }

    private void OnMessage(string message)
    {
        var receiveTime = DateTime.UtcNow;
        Dispatcher.UIThread.Post(() => ProcessMessage(message, receiveTime));
    }

    private void OnLog(string message)
    {
        Dispatcher.UIThread.Post(() => LogLines.Add(message));
    }

    private void OnDisconnected()
    {
        Dispatcher.UIThread.Post(async () =>
        {
            await DisconnectInternalAsync();
            Status = "Disconnected (remote closed)";
        });
    }

    private void ProcessMessage(string message, DateTime receiveTimeUtc)
    {
        if (message.StartsWith("#"))
        {
            LogLines.Add(message);
            return;
        }
        if (message.StartsWith("SYNC")) return;

        var evt = _parser.Parse(message, receiveTimeUtc);
        if (evt == null)
        {
            LogLines.Add($"Unparsed: {message}");
            return;
        }

        // Drop detection
        if (_expectedSeq >= 0 && evt.Sequence != _expectedSeq)
        {
            var missed = (int)(evt.Sequence - _expectedSeq);
            if (missed > 0) DroppedCount += missed;
        }
        _expectedSeq = evt.Sequence + 1;

        EventCount++;
        Events.Insert(0, evt);

        // Keep event list manageable
        while (Events.Count > 200)
            Events.RemoveAt(Events.Count - 1);

        // Update car counts
        _carCountMap[evt.CarNumber] = _carCountMap.GetValueOrDefault(evt.CarNumber) + 1;
        CarCounts = string.Join("  ", Enumerable.Range(1, 6)
            .Where(c => _carCountMap.ContainsKey(c))
            .Select(c => $"C{c}:{_carCountMap[c]}"));

        // Update latency average
        if (evt.LatencyMs.HasValue)
        {
            _latencySum += evt.LatencyMs.Value;
            _latencyCount++;
            AvgLatency = $"{_latencySum / _latencyCount:F0}ms";
        }

        // Update calibration display
        if (_clock.IsCalibrated)
            CalibrationInfo = $"Boot={_clock.EspZeroTime.ToLocalTime():HH:mm:ss.fff}";
    }

    private WebSocketTransport ConfigureWs()
    {
        _wsTransport.ManualUrl = string.IsNullOrWhiteSpace(WsUrl) ? null : WsUrl;
        return _wsTransport;
    }

    private SerialTransport ConfigureSerial()
    {
        _serialTransport.PortName = SelectedPort;
        return _serialTransport;
    }

    private BleTransport ConfigureBle()
    {
        _bleTransport.SelectedDevice = SelectedBleDevice;
        return _bleTransport;
    }

    // --- INotifyPropertyChanged ---

    public event PropertyChangedEventHandler? PropertyChanged;
    private void OnPropertyChanged([CallerMemberName] string? name = null)
        => PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(name));
}
