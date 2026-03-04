namespace ScalextricDesktopClient.Services;

public interface ITransportClient : IDisposable
{
    string Name { get; }
    event Action<string> MessageReceived;
    event Action<string> LogMessage;
    event Action? Disconnected;
    Task ConnectAsync(CancellationToken ct);
    Task<(string? Reply, DateTime ReceiveTime)> SendSyncAsync(CancellationToken ct);
    Task DisconnectAsync();
    bool IsConnected { get; }
}
