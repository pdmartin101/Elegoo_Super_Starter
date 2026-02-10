using System.Net.WebSockets;
using System.Text;
using Makaretu.Dns;

Console.WriteLine("Scalextric Client");
Console.WriteLine("=================\n");
Console.WriteLine("Format: NODE:SENSOR:CAR:FREQ:TIME");
Console.WriteLine("  Node 255 = Parent, 0-254 = Children\n");

string? wsUrl = null;

// Try mDNS discovery first - look for scalextric.local
Console.WriteLine("Searching for Scalextric parent via mDNS...");

using var mdns = new MulticastService();
using var sd = new ServiceDiscovery(mdns);

var discoveryComplete = new TaskCompletionSource<string?>();
var cts = new CancellationTokenSource(TimeSpan.FromSeconds(5));

sd.ServiceInstanceDiscovered += (s, e) =>
{
    if (e.ServiceInstanceName.ToString().Contains("scalextric"))
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
    Console.WriteLine($"Found Scalextric parent at: {wsUrl}");
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
    Console.WriteLine("Connected! Waiting for car events... (Ctrl+C to exit)\n");
    Console.WriteLine($"{"Time",-12} {"Node",-8} {"Sensor",-8} {"Car",-6} {"Freq",-10} {"Timestamp",-12}");
    Console.WriteLine(new string('-', 56));

    var buffer = new byte[1024];
    int eventCount = 0;

    while (ws.State == WebSocketState.Open && !appCts.Token.IsCancellationRequested)
    {
        try
        {
            var result = await ws.ReceiveAsync(buffer, appCts.Token);

            if (result.MessageType == WebSocketMessageType.Text)
            {
                var message = Encoding.UTF8.GetString(buffer, 0, result.Count);

                // Skip comment lines
                if (message.StartsWith("#"))
                {
                    Console.WriteLine($"  {message}");
                    continue;
                }

                // Parse NODE:SENSOR:CAR:FREQ:TIME
                var parts = message.Split(':');
                if (parts.Length >= 5)
                {
                    var node = parts[0];
                    var sensor = parts[1];
                    var car = parts[2];
                    var freq = parts[3];
                    var timestamp = parts[4];

                    var nodeName = node == "255" ? "Parent" : $"Node {node}";
                    eventCount++;

                    // Format ESP32 time (HH.MM.SS.mmm) with colons for display
                    var espTime = timestamp.Replace('.', ':');
                    Console.WriteLine($"{DateTime.Now:HH:mm:ss.ff}  {nodeName,-8} S{sensor,-7} Car {car,-4} {freq,5} Hz  @{espTime}");
                }
                else
                {
                    Console.WriteLine($"[{DateTime.Now:HH:mm:ss}] Raw: {message}");
                }
            }
            else if (result.MessageType == WebSocketMessageType.Close)
            {
                Console.WriteLine("\nServer closed connection");
                break;
            }
        }
        catch (OperationCanceledException)
        {
            break;
        }
    }

    Console.WriteLine($"\nTotal events received: {eventCount}");

    if (ws.State == WebSocketState.Open)
    {
        await ws.CloseAsync(WebSocketCloseStatus.NormalClosure, "Goodbye", CancellationToken.None);
    }

    Console.WriteLine("Disconnected. Goodbye!");
}
catch (Exception ex)
{
    Console.WriteLine($"Connection error: {ex.Message}");
    Console.WriteLine("Make sure the ESP32 parent is running and on the same network.");
}
