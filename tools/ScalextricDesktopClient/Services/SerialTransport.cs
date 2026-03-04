using System.IO.Ports;

namespace ScalextricDesktopClient.Services;

public class SerialTransport : ITransportClient
{
    private SerialPort? _port;
    private CancellationTokenSource? _readCts;
    private bool _connected;
    private TaskCompletionSource<(string Reply, DateTime ReceiveTime)>? _syncTcs;

    public string Name => "Serial";
    public event Action<string>? MessageReceived;
    public event Action<string>? LogMessage;
    public event Action? Disconnected;
    public bool IsConnected => _connected;

    public string? PortName { get; set; }
    public int BaudRate { get; set; } = 115200;

    public static string[] GetAvailablePorts() => SerialPort.GetPortNames();

    public Task ConnectAsync(CancellationToken ct)
    {
        if (string.IsNullOrEmpty(PortName))
            throw new InvalidOperationException("No serial port selected.");

        LogMessage?.Invoke($"Opening {PortName} at {BaudRate} baud...");

        _port = new SerialPort(PortName, BaudRate)
        {
            ReadTimeout = 2000,
            NewLine = "\n",
            DtrEnable = true
        };
        _port.Open();
        _connected = true;
        LogMessage?.Invoke("Connected!");

        _readCts = new CancellationTokenSource();
        _ = Task.Run(() => ReadLoop(_readCts.Token));

        return Task.CompletedTask;
    }

    public async Task<(string? Reply, DateTime ReceiveTime)> SendSyncAsync(CancellationToken ct)
    {
        if (_port == null || !_port.IsOpen) return (null, DateTime.UtcNow);

        _syncTcs = new TaskCompletionSource<(string, DateTime)>();
        _port.WriteLine("SYNC");

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

    public Task DisconnectAsync()
    {
        _readCts?.Cancel();
        _connected = false;
        try { _port?.Close(); } catch { }
        _port?.Dispose();
        _port = null;
        return Task.CompletedTask;
    }

    public void Dispose() => DisconnectAsync().GetAwaiter().GetResult();

    private void ReadLoop(CancellationToken ct)
    {
        if (_port == null) return;

        // Read startup banner
        var bannerDeadline = DateTime.UtcNow.AddSeconds(3);
        _port.ReadTimeout = 2000;
        while (DateTime.UtcNow < bannerDeadline && !ct.IsCancellationRequested)
        {
            try
            {
                var line = _port.ReadLine().Trim();
                if (line.StartsWith("#"))
                    LogMessage?.Invoke(line);
                else if (line.Length > 0)
                {
                    MessageReceived?.Invoke(line);
                    break;
                }
            }
            catch (TimeoutException) { break; }
        }

        _port.ReadTimeout = SerialPort.InfiniteTimeout;

        while (!ct.IsCancellationRequested && _connected)
        {
            try
            {
                var line = _port.ReadLine().Trim();
                if (string.IsNullOrEmpty(line)) continue;
                if (line.StartsWith("SYNC:"))
                    _syncTcs?.TrySetResult((line, DateTime.UtcNow));
                else
                    MessageReceived?.Invoke(line);
            }
            catch (TimeoutException) { }
            catch (OperationCanceledException) { break; }
            catch (IOException) { break; }
            catch (InvalidOperationException) { break; }
        }

        _connected = false;
        Disconnected?.Invoke();
    }
}
