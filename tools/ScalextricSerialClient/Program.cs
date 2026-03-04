using System.IO.Ports;
using System.Text;

Console.WriteLine("Scalextric Serial Client");
Console.WriteLine("========================\n");
Console.WriteLine("Format: SEQ:NODE:SENSOR:CAR:FREQ:MILLIS");
Console.WriteLine("  Node 255 = Parent/Dongle, 0-254 = Children\n");

// Determine COM port
string? portName = args.Length > 0 ? args[0] : null;

if (string.IsNullOrEmpty(portName))
{
    var ports = SerialPort.GetPortNames();
    if (ports.Length == 0)
    {
        Console.WriteLine("No serial ports found.");
        return;
    }

    Console.WriteLine("Available ports:");
    for (int i = 0; i < ports.Length; i++)
        Console.WriteLine($"  {i + 1}. {ports[i]}");

    Console.Write($"\nSelect port (1-{ports.Length}): ");
    var input = Console.ReadLine()?.Trim();
    if (int.TryParse(input, out var idx) && idx >= 1 && idx <= ports.Length)
        portName = ports[idx - 1];
    else
        portName = input; // Allow typing port name directly
}

if (string.IsNullOrEmpty(portName))
{
    Console.WriteLine("No port selected.");
    return;
}

Console.WriteLine($"\nOpening {portName} at 115200 baud...");

using var port = new SerialPort(portName, 115200)
{
    ReadTimeout = 2000,
    NewLine = "\n",
    DtrEnable = true  // Triggers ESP32 reset on connect (standard behavior)
};

var appCts = new CancellationTokenSource();
Console.CancelKeyPress += (s, e) =>
{
    e.Cancel = true;
    appCts.Cancel();
    Console.WriteLine("\nDisconnecting...");
};

try
{
    port.Open();
    Console.WriteLine("Connected!\n");

    // Read startup banner (lines starting with #)
    Console.WriteLine("Dongle startup:");
    var bannerDeadline = DateTime.UtcNow.AddSeconds(3);
    while (DateTime.UtcNow < bannerDeadline)
    {
        try
        {
            var line = port.ReadLine().Trim();
            if (line.StartsWith("#"))
                Console.WriteLine($"  {line}");
            else if (line.Length > 0)
                break; // Non-banner line, move on
        }
        catch (TimeoutException) { break; }
    }

    // Clock sync: espZeroTime = PC time when dongle millis was 0
    // candidate = receiveTime - espMillis always overestimates (by serial latency)
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

    // SYNC calibration - same approach as WebSocket client
    Console.WriteLine("\nCalibrating clock...");
    port.ReadTimeout = 500;

    for (int round = 0; round < 5; round++)
    {
        // Flush any pending data
        port.DiscardInBuffer();

        var sendTime = DateTime.UtcNow;
        port.WriteLine("SYNC");

        try
        {
            var reply = port.ReadLine().Trim();
            var receiveTime = DateTime.UtcNow;
            var rtt = receiveTime - sendTime;

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
                    Console.WriteLine($"  Round {round + 1}: RTT={rtt.TotalMilliseconds:F0}ms, dongle millis={espMillis}, jitter={latency.TotalMilliseconds:F0}ms");
                }
            }
        }
        catch (TimeoutException)
        {
            Console.WriteLine($"  Round {round + 1}: timeout");
        }

        Thread.Sleep(100);
    }

    if (clockCalibrated)
        Console.WriteLine($"  Dongle boot time (estimated): {espZeroTime.ToLocalTime():HH:mm:ss.fff}");
    else
        Console.WriteLine("  Calibration failed (will calibrate from first event)");

    Console.WriteLine($"\nWaiting for car events... (Ctrl+C to exit)\n");
    Console.WriteLine($"{"Time",-12} {"Node",-8} {"Sensor",-8} {"Car",-6} {"Freq",-10} {"Latency",-12}");
    Console.WriteLine(new string('-', 56));

    int eventCount = 0;
    long expectedSeq = -1;
    int droppedCount = 0;
    var carCounts = new Dictionary<string, int>();

    port.ReadTimeout = SerialPort.InfiniteTimeout;

    while (!appCts.Token.IsCancellationRequested)
    {
        string line;
        try
        {
            line = port.ReadLine().Trim();
        }
        catch (OperationCanceledException) { break; }
        catch (IOException) { Console.WriteLine("\nPort disconnected."); break; }

        if (string.IsNullOrEmpty(line)) continue;
        if (line.StartsWith("#")) { Console.WriteLine($"  {line}"); continue; }
        if (line.StartsWith("SYNC")) continue;

        var receiveTime = DateTime.UtcNow;

        // Parse SEQ:NODE:SENSOR:CAR:FREQ:MILLIS
        var parts = line.Split(':');
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

            var nodeName = node == "255" ? "Dongle" : $"Node {node}";
            eventCount++;

            // Self-calibrate with every event
            var prevZero = espZeroTime;
            calibrateClock(receiveTime, eventMillis);
            if (espZeroTime != prevZero)
                Console.WriteLine($"             [recalibrated: dongle boot = {espZeroTime.ToLocalTime():HH:mm:ss.fff}]");

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
            Console.WriteLine($"[{DateTime.Now:HH:mm:ss}] Raw: {line}");
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

    Console.WriteLine("Disconnected. Goodbye!");
}
catch (UnauthorizedAccessException)
{
    Console.WriteLine($"Port {portName} is in use by another application.");
}
catch (FileNotFoundException)
{
    Console.WriteLine($"Port {portName} not found.");
}
catch (Exception ex)
{
    Console.WriteLine($"Error: {ex.Message}");
}
