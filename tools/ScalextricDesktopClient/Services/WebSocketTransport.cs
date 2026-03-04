using System.Net.WebSockets;
using System.Text;
using Makaretu.Dns;

namespace ScalextricDesktopClient.Services;

public class WebSocketTransport : ITransportClient
{
    private ClientWebSocket? _ws;
    private CancellationTokenSource? _receiveCts;
    private string? _url;
    private TaskCompletionSource<(string Reply, DateTime ReceiveTime)>? _syncTcs;

    public string Name => "WebSocket";
    public event Action<string>? MessageReceived;
    public event Action<string>? LogMessage;
    public event Action? Disconnected;
    public bool IsConnected => _ws?.State == WebSocketState.Open;

    public string? ManualUrl { get; set; }

    public async Task ConnectAsync(CancellationToken ct)
    {
        _url = ManualUrl;

        if (string.IsNullOrEmpty(_url))
        {
            LogMessage?.Invoke("Searching for Scalextric relay via mDNS...");
            _url = await DiscoverAsync(ct);
        }

        if (string.IsNullOrEmpty(_url))
            throw new InvalidOperationException("No Scalextric relay found. Enter URL manually.");

        LogMessage?.Invoke($"Connecting to {_url}...");
        _ws = new ClientWebSocket();
        await _ws.ConnectAsync(new Uri(_url), ct);
        LogMessage?.Invoke("Connected!");

        _receiveCts = new CancellationTokenSource();
        _ = Task.Run(() => ReceiveLoop(_receiveCts.Token));
    }

    public async Task<(string? Reply, DateTime ReceiveTime)> SendSyncAsync(CancellationToken ct)
    {
        if (_ws?.State != WebSocketState.Open) return (null, DateTime.UtcNow);

        _syncTcs = new TaskCompletionSource<(string, DateTime)>();
        var syncBytes = Encoding.UTF8.GetBytes("SYNC");
        await _ws.SendAsync(syncBytes, WebSocketMessageType.Text, true, ct);

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
        _receiveCts?.Cancel();
        if (_ws?.State == WebSocketState.Open)
        {
            try { await _ws.CloseAsync(WebSocketCloseStatus.NormalClosure, "Goodbye", CancellationToken.None); }
            catch { }
        }
        _ws?.Dispose();
        _ws = null;
    }

    public void Dispose() => DisconnectAsync().GetAwaiter().GetResult();

    private async Task ReceiveLoop(CancellationToken ct)
    {
        var buffer = new byte[1024];
        try
        {
            while (_ws?.State == WebSocketState.Open && !ct.IsCancellationRequested)
            {
                var result = await _ws.ReceiveAsync(buffer, ct);
                if (result.MessageType == WebSocketMessageType.Close) break;
                if (result.MessageType == WebSocketMessageType.Text)
                {
                    var msg = Encoding.UTF8.GetString(buffer, 0, result.Count);
                    if (msg.StartsWith("SYNC:"))
                        _syncTcs?.TrySetResult((msg, DateTime.UtcNow));
                    else
                        MessageReceived?.Invoke(msg);
                }
            }
        }
        catch (OperationCanceledException) { }
        catch (WebSocketException) { }
        Disconnected?.Invoke();
    }

    private async Task<string?> DiscoverAsync(CancellationToken ct)
    {
        using var mdns = new MulticastService();
        using var sd = new ServiceDiscovery(mdns);
        var tcs = new TaskCompletionSource<string?>();
        using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(5));
        using var linked = CancellationTokenSource.CreateLinkedTokenSource(ct, timeout.Token);

        sd.ServiceInstanceDiscovered += (s, e) =>
        {
            if (e.ServiceInstanceName.ToString().Contains("scalextric"))
            {
                foreach (var record in e.Message.AdditionalRecords)
                {
                    if (record is ARecord aRecord)
                        tcs.TrySetResult($"ws://{aRecord.Address}:81");
                }
            }
        };

        mdns.Start();
        sd.QueryServiceInstances("_ws._tcp");

        try { return await tcs.Task.WaitAsync(linked.Token); }
        catch (OperationCanceledException) { return null; }
        finally { mdns.Stop(); }
    }
}
