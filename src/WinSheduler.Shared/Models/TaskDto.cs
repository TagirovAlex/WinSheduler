using WinSheduler.Shared;

namespace WinSheduler.Shared.Models;

public class TaskDto
{
    public Guid Id { get; set; }
    public string Name { get; set; } = "";
    public string Description { get; set; } = "";
    public bool Enabled { get; set; } = true;
    public string ProgramPath { get; set; } = "";
    public string Arguments { get; set; } = "";
    public string WorkingDirectory { get; set; } = "";
    public WindowStyle WindowStyle { get; set; } = WindowStyle.Normal;
    public string? RunAsUser { get; set; }
    public string? RunAsDomain { get; set; }
    public string? RunAsPasswordEncrypted { get; set; }
    public OnErrorAction OnError { get; set; } = OnErrorAction.SkipNext;
    public int RetryCount { get; set; }
    public OnOverlapAction OnOverlap { get; set; } = OnOverlapAction.WaitComplete;
    public int? TimeoutMinutes { get; set; }
    public bool KillOnTimeout { get; set; }
    public bool LogOutput { get; set; }
    public int MaxHistoryRecords { get; set; } = 1000;
    public List<TimeWindowDto> TimeWindows { get; set; } = [];
}

public class TimeWindowDto
{
    public Guid Id { get; set; }
    public TimeWindowType Type { get; set; } = TimeWindowType.ExactTimes;
    public DateTime? StartDate { get; set; }
    public DateTime? EndDate { get; set; }
    public List<DayOfWeek> DaysOfWeek { get; set; } = [];
    public List<int> DaysOfMonth { get; set; } = [];
    public List<DateTime> SpecificDates { get; set; } = [];
    public List<string> ExactTimes { get; set; } = [];
    public string? StartTime { get; set; }
    public string? EndTime { get; set; }
    public int? RepeatIntervalMinutes { get; set; }
    public string? RepeatUntil { get; set; }
}
