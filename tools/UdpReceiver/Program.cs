using System.Net;
using System.Net.Sockets;
using System.Text;
using Makaretu.Dns;

const int PORT = 12345;
const string SERVICE_NAME = "esp32receiver";
const string SERVICE_TYPE = "_udpreceiver._udp";

Console.WriteLine($"UDP Receiver listening on port {PORT}");

// Advertise via mDNS
var service = new ServiceProfile(SERVICE_NAME, SERVICE_TYPE, (ushort)PORT);
var sd = new ServiceDiscovery();
sd.Advertise(service);

Console.WriteLine($"Advertising as: {SERVICE_NAME}.{SERVICE_TYPE}.local");
Console.WriteLine("ESP32 can now discover this service automatically");
Console.WriteLine("Press Ctrl+C to exit\n");

using var udpClient = new UdpClient(PORT);
var remoteEndPoint = new IPEndPoint(IPAddress.Any, 0);

Console.CancelKeyPress += (s, e) =>
{
    sd.Unadvertise(service);
    sd.Dispose();
    Console.WriteLine("\nService unadvertised. Goodbye!");
};

while (true)
{
    try
    {
        byte[] data = udpClient.Receive(ref remoteEndPoint);
        string message = Encoding.UTF8.GetString(data);

        Console.WriteLine($"[{DateTime.Now:HH:mm:ss}] From {remoteEndPoint}: {message}");
    }
    catch (SocketException ex)
    {
        Console.WriteLine($"Error: {ex.Message}");
    }
}
