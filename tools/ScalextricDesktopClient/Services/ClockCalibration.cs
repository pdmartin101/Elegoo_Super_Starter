namespace ScalextricDesktopClient.Services;

public class ClockCalibration
{
    private DateTime _espZeroTime = DateTime.MaxValue;

    public bool IsCalibrated => _espZeroTime != DateTime.MaxValue;
    public DateTime EspZeroTime => _espZeroTime;

    public bool Update(DateTime receiveTimeUtc, long espMillis)
    {
        var candidate = receiveTimeUtc - TimeSpan.FromMilliseconds(espMillis);
        if (candidate < _espZeroTime)
        {
            _espZeroTime = candidate;
            return true;
        }
        return false;
    }

    public DateTime GetEventTime(long espMillis)
        => _espZeroTime + TimeSpan.FromMilliseconds(espMillis);

    public double GetLatencyMs(DateTime receiveTimeUtc, long espMillis)
        => (receiveTimeUtc - GetEventTime(espMillis)).TotalMilliseconds;

    public void Reset()
        => _espZeroTime = DateTime.MaxValue;
}
