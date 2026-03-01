using System.Net.WebSockets;
using System.Text;
using Makaretu.Dns;

Console.WriteLine("Scalextric Client");
Console.WriteLine("=================\n");
Console.WriteLine("Format: SEQ:NODE:SENSOR:CAR:FREQ:MILLIS");
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

// Clock sync: espZeroTime = PC time when ESP32 millis was 0
// candidate = receiveTime - espMillis always overestimates (by network delay)
// so the lowest candidate ever seen is the most accurate estimate
DateTime espZeroTime = DateTime.MaxValue;
bool clockCalibrated = false;

void calibrateClock(DateTime receiveTime, long espMillis)
{
    var candidate = receiveTime - TimeSpan.FromMilliseconds(espMillis);
    if (candidate < espZeroTime)
    {
        espZeroTime = candidate;
        clockCalibrated = true;
    }
}

try
{
    await ws.ConnectAsync(new Uri(wsUrl), appCts.Token);
    Console.WriteLine("Connected!");

    // Initial calibration via SYNC - gives us a baseline before any car events
    var buffer = new byte[1024];
    Console.WriteLine("Calibrating clock...");

    for (int round = 0; round < 5; round++)
    {
        var syncBytes = Encoding.UTF8.GetBytes("SYNC");
        var sendTime = DateTime.UtcNow;
        await ws.SendAsync(syncBytes, WebSocketMessageType.Text, true, appCts.Token);

        var syncResult = await ws.ReceiveAsync(buffer, appCts.Token);
        var receiveTime = DateTime.UtcNow;
        var rtt = receiveTime - sendTime;

        var reply = Encoding.UTF8.GetString(buffer, 0, syncResult.Count);
        if (reply.StartsWith("SYNC:") && long.TryParse(reply[5..], out var espMillis))
        {
            if (rtt.TotalMilliseconds > 100)
            {
                Console.WriteLine($"  Round {round + 1}: RTT={rtt.TotalMilliseconds:F0}ms (slow, skipped)");
            }
            else
            {
                calibrateClock(receiveTime, espMillis);
                var latency = receiveTime - (espZeroTime + TimeSpan.FromMilliseconds(espMillis));
                Console.WriteLine($"  Round {round + 1}: RTT={rtt.TotalMilliseconds:F0}ms, ESP32 millis={espMillis}, jitter={latency.TotalMilliseconds:F0}ms");
            }
        }

        await Task.Delay(100, appCts.Token);
    }

    if (clockCalibrated)
        Console.WriteLine($"  ESP32 boot time (estimated): {espZeroTime.ToLocalTime():HH:mm:ss.fff}");
    else
        Console.WriteLine("  Calibration failed");

    Console.WriteLine($"\nWaiting for car events... (Ctrl+C to exit)\n");
    Console.WriteLine($"{"Time",-12} {"Node",-8} {"Sensor",-8} {"Car",-6} {"Freq",-10} {"Latency",-12}");
    Console.WriteLine(new string('-', 56));

    int eventCount = 0;
    long expectedSeq = -1;
    int droppedCount = 0;
    var carCounts = new Dictionary<string, int>();

    while (ws.State == WebSocketState.Open && !appCts.Token.IsCancellationRequested)
    {
        try
        {
            var result = await ws.ReceiveAsync(buffer, appCts.Token);

            if (result.MessageType == WebSocketMessageType.Text)
            {
                var receiveTime = DateTime.UtcNow;
                var message = Encoding.UTF8.GetString(buffer, 0, result.Count);

                if (message.StartsWith("#"))
                {
                    Console.WriteLine($"  {message}");
                    continue;
                }
                if (message.StartsWith("SYNC"))
                    continue;

                // Parse SEQ:NODE:SENSOR:CAR:FREQ:MILLIS
                var parts = message.Split(':');
                if (parts.Length >= 6 && long.TryParse(parts[0], out var seq) && long.TryParse(parts[5], out var eventMillis))
                {
                    var node = parts[1];
                    var sensor = parts[2];
                    var car = parts[3];
                    var freq = parts[4];

                    // Detect dropped messages
                    if (expectedSeq >= 0 && seq != expectedSeq)
                    {
                        var missed = seq - expectedSeq;
                        droppedCount += (int)missed;
                        Console.WriteLine($"             *** DROPPED {missed} event(s)! (expected seq {expectedSeq}, got {seq}) ***");
                    }
                    expectedSeq = seq + 1;

                    var nodeName = node == "255" ? "Parent" : $"Node {node}";
                    eventCount++;

                    // Self-calibrate with every event
                    var prevZero = espZeroTime;
                    calibrateClock(receiveTime, eventMillis);
                    if (espZeroTime != prevZero)
                        Console.WriteLine($"             [recalibrated: ESP32 boot = {espZeroTime.ToLocalTime():HH:mm:ss.fff}]");

                    var latencyStr = "";
                    var eventTimeStr = "";
                    if (clockCalibrated)
                    {
                        var eventPcTime = espZeroTime + TimeSpan.FromMilliseconds(eventMillis);
                        var latency = receiveTime - eventPcTime;
                        latencyStr = $"  ({latency.TotalMilliseconds:F0}ms)";
                        eventTimeStr = eventPcTime.ToLocalTime().ToString("HH:mm:ss.fff");
                    }
                    else
                    {
                        eventTimeStr = eventMillis.ToString();
                    }

                    var carLabel = car == "0" ? "?" : car;
                    Console.WriteLine($"{DateTime.Now:HH:mm:ss.ff}  {nodeName,-8} S{sensor,-7} Car {carLabel,-4} {freq,5} Hz  ms={eventMillis}  @{eventTimeStr}{latencyStr}");

                    // Track per-car counts
                    var carKey = $"Car {car}";
                    carCounts[carKey] = carCounts.GetValueOrDefault(carKey) + 1;
                    var counts = string.Join("  ", Enumerable.Range(1, 6).Select(c => $"C{c}:{carCounts.GetValueOrDefault($"Car {c}")}"));
                    Console.WriteLine($"             [{counts}]");
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

    Console.WriteLine($"\nTotal events received: {eventCount}" + (droppedCount > 0 ? $"  DROPPED: {droppedCount}" : "  (none dropped)"));
    if (carCounts.Count > 0)
    {
        var summary = string.Join("  ", Enumerable.Range(1, 6)
            .Where(c => carCounts.ContainsKey($"Car {c}"))
            .Select(c => $"Car {c}: {carCounts[$"Car {c}"]}"));
        Console.WriteLine($"Car counts: {summary}");
    }

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
