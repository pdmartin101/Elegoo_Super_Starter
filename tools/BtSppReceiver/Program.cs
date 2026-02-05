using System.IO.Ports;

Console.WriteLine("Bluetooth SPP Receiver");
Console.WriteLine("======================\n");

// List available COM ports
var ports = SerialPort.GetPortNames();
if (ports.Length == 0)
{
    Console.WriteLine("No COM ports found!");
    Console.WriteLine("Make sure you've paired with ESP32-BT-SPP first.");
    return;
}

Console.WriteLine("Available COM ports:");
for (int i = 0; i < ports.Length; i++)
{
    Console.WriteLine($"  {i + 1}. {ports[i]}");
}

Console.Write("\nSelect port number (or enter COM name): ");
var input = Console.ReadLine()?.Trim();

string? selectedPort = null;
if (int.TryParse(input, out int portIndex) && portIndex >= 1 && portIndex <= ports.Length)
{
    selectedPort = ports[portIndex - 1];
}
else if (input?.StartsWith("COM", StringComparison.OrdinalIgnoreCase) == true)
{
    selectedPort = input.ToUpper();
}

if (selectedPort == null)
{
    Console.WriteLine("Invalid selection");
    return;
}

Console.WriteLine($"\nConnecting to {selectedPort}...");

try
{
    using var port = new SerialPort(selectedPort, 115200)
    {
        ReadTimeout = 1000,
        WriteTimeout = 1000
    };

    port.Open();
    Console.WriteLine("Connected! Press Ctrl+C to exit\n");

    var cts = new CancellationTokenSource();
    Console.CancelKeyPress += (s, e) =>
    {
        e.Cancel = true;
        cts.Cancel();
        Console.WriteLine("\nDisconnecting...");
    };

    // Read loop
    while (!cts.Token.IsCancellationRequested)
    {
        try
        {
            if (port.BytesToRead > 0)
            {
                string message = port.ReadLine();
                Console.WriteLine($"[{DateTime.Now:HH:mm:ss}] Received: {message}");
            }
        }
        catch (TimeoutException)
        {
            // Normal timeout, continue
        }

        Thread.Sleep(10);
    }

    port.Close();
    Console.WriteLine("Disconnected. Goodbye!");
}
catch (Exception ex)
{
    Console.WriteLine($"Error: {ex.Message}");
    Console.WriteLine("\nTroubleshooting:");
    Console.WriteLine("1. Pair ESP32-BT-SPP in Windows Bluetooth settings");
    Console.WriteLine("2. Check which COM port was assigned (Device Manager > Ports)");
    Console.WriteLine("3. Make sure no other app is using the port");
}
