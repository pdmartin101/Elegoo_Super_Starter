using System.Text;
using InTheHand.Bluetooth;

const string SERVICE_UUID = "a1b2c3d4-e5f6-7890-abcd-ef1234567890";
const string EVENT_CHAR_UUID = "a1b2c3d4-e5f6-7890-abcd-ef1234567891";
const string SYNC_CHAR_UUID = "a1b2c3d4-e5f6-7890-abcd-ef1234567892";

Console.WriteLine("Scalextric BLE Client");
Console.WriteLine("=====================\n");
Console.WriteLine("Format: SEQ:NODE:SENSOR:CAR:FREQ:MILLIS");
Console.WriteLine("  Node 255 = Relay, 0-254 = Children\n");

var appCts = new CancellationTokenSource();
Console.CancelKeyPress += (s, e) =>
{
    e.Cancel = true;
    appCts.Cancel();
    Console.WriteLine("\nDisconnecting...");
};

// Scan for BLE relay
Console.WriteLine("Scanning for Scalextric-Relay...");

BluetoothDevice? device = null;

var filter = new RequestDeviceOptions();
filter.Filters.Add(new BluetoothLEScanFilter { Name = "Scalextric-Relay" });

try
{
    device = await Bluetooth.RequestDeviceAsync(filter);
}
catch (Exception ex)
{
    Console.WriteLine($"Scan failed: {ex.Message}");
    Console.WriteLine("Make sure Bluetooth is enabled and the BLE relay is powered on.");
    return;
}

if (device == null)
{
    Console.WriteLine("No Scalextric-Relay found.");
    return;
}

Console.WriteLine($"Found: {device.Name} ({device.Id})");
Console.WriteLine("Connecting...");

RemoteGattServer? gatt = null;
try
{
    gatt = device.Gatt;
    await gatt.ConnectAsync();
    Console.WriteLine("Connected!");
}
catch (Exception ex)
{
    Console.WriteLine($"Connection failed: {ex.Message}");
    return;
}

// Get service and characteristics
GattService? service = null;
GattCharacteristic? eventChar = null;
GattCharacteristic? syncChar = null;

try
{
    service = await gatt.GetPrimaryServiceAsync(BluetoothUuid.FromGuid(Guid.Parse(SERVICE_UUID)));
    if (service == null) { Console.WriteLine("Service not found."); return; }

    eventChar = await service.GetCharacteristicAsync(BluetoothUuid.FromGuid(Guid.Parse(EVENT_CHAR_UUID)));
    syncChar = await service.GetCharacteristicAsync(BluetoothUuid.FromGuid(Guid.Parse(SYNC_CHAR_UUID)));

    if (eventChar == null) { Console.WriteLine("Event characteristic not found."); return; }
    if (syncChar == null) { Console.WriteLine("Sync characteristic not found."); return; }
}
catch (Exception ex)
{
    Console.WriteLine($"Service discovery failed: {ex.Message}");
    return;
}

// Clock sync: espZeroTime = PC time when relay millis was 0
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

// SYNC calibration via BLE write + notify
Console.WriteLine("\nCalibrating clock...");

// Subscribe to sync notifications for calibration
var syncTcs = new TaskCompletionSource<string>();

syncChar.CharacteristicValueChanged += (s, e) =>
{
    var reply = Encoding.UTF8.GetString(e.Value ?? Array.Empty<byte>());
    syncTcs.TrySetResult(reply);
};
await syncChar.StartNotificationsAsync();

for (int round = 0; round < 5; round++)
{
    syncTcs = new TaskCompletionSource<string>();
    var sendTime = DateTime.UtcNow;
    await syncChar.WriteValueWithResponseAsync(Encoding.UTF8.GetBytes("SYNC"));

    try
    {
        var reply = await syncTcs.Task.WaitAsync(TimeSpan.FromSeconds(2));
        var receiveTime = DateTime.UtcNow;
        var rtt = receiveTime - sendTime;

        if (reply.StartsWith("SYNC:") && long.TryParse(reply[5..], out var espMillis))
        {
            if (rtt.TotalMilliseconds > 200)
            {
                Console.WriteLine($"  Round {round + 1}: RTT={rtt.TotalMilliseconds:F0}ms (slow, skipped)");
            }
            else
            {
                calibrateClock(receiveTime, espMillis);
                var latency = receiveTime - (espZeroTime + TimeSpan.FromMilliseconds(espMillis));
                Console.WriteLine($"  Round {round + 1}: RTT={rtt.TotalMilliseconds:F0}ms, relay millis={espMillis}, jitter={latency.TotalMilliseconds:F0}ms");
            }
        }
    }
    catch (TimeoutException)
    {
        Console.WriteLine($"  Round {round + 1}: timeout");
    }

    await Task.Delay(100);
}

await syncChar.StopNotificationsAsync();

if (clockCalibrated)
    Console.WriteLine($"  Relay boot time (estimated): {espZeroTime.ToLocalTime():HH:mm:ss.fff}");
else
    Console.WriteLine("  Calibration failed (will calibrate from first event)");

// Subscribe to event notifications
Console.WriteLine($"\nWaiting for car events... (Ctrl+C to exit)\n");
Console.WriteLine($"{"Time",-12} {"Node",-8} {"Sensor",-8} {"Car",-6} {"Freq",-10} {"Latency",-12}");
Console.WriteLine(new string('-', 56));

int eventCount = 0;
long expectedSeq = -1;
int droppedCount = 0;
var carCounts = new Dictionary<string, int>();

eventChar.CharacteristicValueChanged += (s, e) =>
{
    var receiveTime = DateTime.UtcNow;
    var message = Encoding.UTF8.GetString(e.Value ?? Array.Empty<byte>());

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

        var nodeName = node == "255" ? "Relay" : $"Node {node}";
        eventCount++;

        // Self-calibrate with every event
        var prevZero = espZeroTime;
        calibrateClock(receiveTime, eventMillis);
        if (espZeroTime != prevZero)
            Console.WriteLine($"             [recalibrated: relay boot = {espZeroTime.ToLocalTime():HH:mm:ss.fff}]");

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
};

await eventChar.StartNotificationsAsync();

// Wait until cancelled
try
{
    await Task.Delay(Timeout.Infinite, appCts.Token);
}
catch (OperationCanceledException) { }

await eventChar.StopNotificationsAsync();

Console.WriteLine($"\nTotal events received: {eventCount}" + (droppedCount > 0 ? $"  DROPPED: {droppedCount}" : "  (none dropped)"));
if (carCounts.Count > 0)
{
    var summary = string.Join("  ", Enumerable.Range(1, 6)
        .Where(c => carCounts.ContainsKey($"Car {c}"))
        .Select(c => $"Car {c}: {carCounts[$"Car {c}"]}"));
    Console.WriteLine($"Car counts: {summary}");
}

gatt.Disconnect();
Console.WriteLine("Disconnected. Goodbye!");
