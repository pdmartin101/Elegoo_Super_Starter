namespace ScalextricDesktopClient.Models;

public record ScalextricEvent(
    long Sequence,
    int NodeId,
    int SensorId,
    int CarNumber,
    int Frequency,
    long EspRecvMillis,
    long? EspSendMillis,
    DateTime ReceiveTimeUtc,
    DateTime? EventTimeLocal,
    double? LatencyMs
)
{
    public string NodeName => NodeId == 255 ? "Parent" : $"Node {NodeId}";
    public string CarLabel => CarNumber == 0 ? "?" : CarNumber.ToString();
    public string TimeStr => EventTimeLocal?.ToString("HH:mm:ss.fff") ?? EspRecvMillis.ToString();
    public string LatencyStr => LatencyMs.HasValue ? $"{LatencyMs.Value:F0}ms" : "";
    public long? InternalDelayMs => EspSendMillis.HasValue ? EspSendMillis.Value - EspRecvMillis : null;
    public string InternalDelayStr => InternalDelayMs.HasValue ? $"{InternalDelayMs.Value}ms" : "";
}
