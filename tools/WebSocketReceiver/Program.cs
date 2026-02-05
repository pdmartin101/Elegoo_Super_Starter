using System.Net.WebSockets;
using System.Text;
using Makaretu.Dns;

Console.WriteLine("WebSocket Receiver");
Console.WriteLine("==================\n");

string? wsUrl = null;

// Try mDNS discovery first
Console.WriteLine("Searching for ESP32 WebSocket server via mDNS...");

using var mdns = new MulticastService();
using var sd = new ServiceDiscovery(mdns);

var discoveryComplete = new TaskCompletionSource<string?>();
var cts = new CancellationTokenSource(TimeSpan.FromSeconds(5));

sd.ServiceInstanceDiscovered += (s, e) =>
{
    if (e.ServiceInstanceName.ToString().Contains("esp32ws"))
    {
        foreach (var record in e.Message.AdditionalRecords)
        {
            if (record is ARecord aRecord)
            {
                var url = $"ws://{aRecord.Address}:81";
                discoveryComplete.TrySetResult(url);
            }
        }
    }
};

mdns.Start();
sd.QueryServiceInstances("_ws._tcp");

try
{
    wsUrl = await discoveryComplete.Task.WaitAsync(cts.Token);
    Console.WriteLine($"Found ESP32 at: {wsUrl}");
}
catch (OperationCanceledException)
{
    Console.WriteLine("mDNS discovery timed out.");
    Console.Write("Enter WebSocket URL (e.g., ws://192.168.1.100:81): ");
    wsUrl = Console.ReadLine()?.Trim();
}

mdns.Stop();

if (string.IsNullOrEmpty(wsUrl))
{
    Console.WriteLine("No URL provided");
    return;
}

Console.WriteLine($"\nConnecting to {wsUrl}...");

using var ws = new ClientWebSocket();
var appCts = new CancellationTokenSource();

Console.CancelKeyPress += (s, e) =>
{
    e.Cancel = true;
    appCts.Cancel();
    Console.WriteLine("\nDisconnecting...");
};

try
{
    await ws.ConnectAsync(new Uri(wsUrl), appCts.Token);
    Console.WriteLine("Connected! Press Ctrl+C to exit\n");

    var buffer = new byte[1024];

    while (ws.State == WebSocketState.Open && !appCts.Token.IsCancellationRequested)
    {
        try
        {
            var result = await ws.ReceiveAsync(buffer, appCts.Token);

            if (result.MessageType == WebSocketMessageType.Text)
            {
                var message = Encoding.UTF8.GetString(buffer, 0, result.Count);
                Console.WriteLine($"[{DateTime.Now:HH:mm:ss}] Received: {message}");
            }
            else if (result.MessageType == WebSocketMessageType.Close)
            {
                Console.WriteLine("Server closed connection");
                break;
            }
        }
        catch (OperationCanceledException)
        {
            break;
        }
    }

    if (ws.State == WebSocketState.Open)
    {
        await ws.CloseAsync(WebSocketCloseStatus.NormalClosure, "Goodbye", CancellationToken.None);
    }

    Console.WriteLine("Disconnected. Goodbye!");
}
catch (Exception ex)
{
    Console.WriteLine($"Error: {ex.Message}");
}
