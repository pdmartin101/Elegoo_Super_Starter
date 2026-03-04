using ScalextricDesktopClient.Models;

namespace ScalextricDesktopClient.Services;

public class EventParser
{
    private readonly ClockCalibration _clock;

    public EventParser(ClockCalibration clock)
    {
        _clock = clock;
    }

    public ScalextricEvent? Parse(string message, DateTime receiveTimeUtc)
    {
        var parts = message.Split(':');
        if (parts.Length < 6) return null;

        if (!long.TryParse(parts[0], out var seq)) return null;
        if (!int.TryParse(parts[1], out var node)) return null;
        if (!int.TryParse(parts[2], out var sensor)) return null;
        if (!int.TryParse(parts[3], out var car)) return null;
        if (!int.TryParse(parts[4], out var freq)) return null;
        if (!long.TryParse(parts[5], out var espRecvMillis)) return null;

        long? espSendMillis = null;
        if (parts.Length >= 7 && long.TryParse(parts[6], out var sendMs))
            espSendMillis = sendMs;

        _clock.Update(receiveTimeUtc, espRecvMillis);

        DateTime? eventTimeLocal = null;
        double? latencyMs = null;
        if (_clock.IsCalibrated)
        {
            eventTimeLocal = _clock.GetEventTime(espRecvMillis).ToLocalTime();
            latencyMs = _clock.GetLatencyMs(receiveTimeUtc, espRecvMillis);
        }

        return new ScalextricEvent(seq, node, sensor, car, freq, espRecvMillis, espSendMillis,
            receiveTimeUtc, eventTimeLocal, latencyMs);
    }
}
