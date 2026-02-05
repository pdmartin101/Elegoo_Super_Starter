using System.Net;
using System.Net.Sockets;
using System.Text;
using Makaretu.Dns;

const int PORT = 12346;
const string SERVICE_NAME = "esp32receiver";
const string SERVICE_TYPE = "_tcpreceiver._tcp";

Console.WriteLine($"TCP Receiver listening on port {PORT}");

// Advertise via mDNS
var service = new ServiceProfile(SERVICE_NAME, SERVICE_TYPE, (ushort)PORT);
var sd = new ServiceDiscovery();
sd.Advertise(service);

Console.WriteLine($"Advertising as: {SERVICE_NAME}.{SERVICE_TYPE}.local");
Console.WriteLine("ESP32 can now discover this service automatically");
Console.WriteLine("Press Ctrl+C to exit\n");

var listener = new TcpListener(IPAddress.Any, PORT);
listener.Start();

Console.CancelKeyPress += (s, e) =>
{
    sd.Unadvertise(service);
    sd.Dispose();
    listener.Stop();
    Console.WriteLine("\nService unadvertised. Goodbye!");
};

while (true)
{
    try
    {
        Console.WriteLine("Waiting for connection...");
        using var client = listener.AcceptTcpClient();
        var endpoint = client.Client.RemoteEndPoint as IPEndPoint;
        Console.WriteLine($"Client connected from {endpoint}");

        using var stream = client.GetStream();
        using var reader = new StreamReader(stream, Encoding.UTF8);
        using var writer = new StreamWriter(stream, Encoding.UTF8) { AutoFlush = true };

        while (client.Connected)
        {
            string? message = reader.ReadLine();
            if (message == null) break;

            Console.WriteLine($"[{DateTime.Now:HH:mm:ss}] Received: {message}");

            // Send acknowledgment
            writer.WriteLine("ACK: " + message);
        }

        Console.WriteLine("Client disconnected\n");
    }
    catch (SocketException ex)
    {
        Console.WriteLine($"Error: {ex.Message}");
    }
    catch (IOException)
    {
        Console.WriteLine("Connection closed\n");
    }
}
