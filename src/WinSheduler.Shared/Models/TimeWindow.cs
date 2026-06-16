namespace WinSheduler.Shared.Models;

public class TimeWindow
{
    public Guid Id { get; set; } = Guid.NewGuid();
    public Guid TaskId { get; set; }
    public DateTime? StartDate { get; set; }
    public DateTime? EndDate { get; set; }
    public List<DayOfWeek> DaysOfWeek { get; set; } = [];
    public List<int> DaysOfMonth { get; set; } = [];
    public List<DateTime> SpecificDates { get; set; } = [];
    public TimeWindowType Type { get; set; } = TimeWindowType.ExactTimes;
    public List<TimeSpan> ExactTimes { get; set; } = [];
    public TimeSpan? StartTime { get; set; }
    public TimeSpan? EndTime { get; set; }
    public int? RepeatIntervalMinutes { get; set; }
    public TimeSpan? RepeatUntil { get; set; }
}
