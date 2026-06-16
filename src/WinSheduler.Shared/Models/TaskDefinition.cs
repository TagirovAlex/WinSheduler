namespace WinSheduler.Shared.Models;

public class TaskDefinition
{
    public Guid Id { get; set; } = Guid.NewGuid();
    public string Name { get; set; } = "";
    public string Description { get; set; } = "";
    public bool Enabled { get; set; } = true;
    public string ProgramPath { get; set; } = "";
    public string Arguments { get; set; } = "";
    public string WorkingDirectory { get; set; } = "";
    public WindowStyle WindowStyle { get; set; } = WindowStyle.Normal;
    public RunAsCredentials? RunAsUser { get; set; }
    public OnErrorAction OnError { get; set; } = OnErrorAction.SkipNext;
    public int RetryCount { get; set; }
    public OnOverlapAction OnOverlap { get; set; } = OnOverlapAction.WaitComplete;
    public int? TimeoutMinutes { get; set; }
    public bool KillOnTimeout { get; set; }
    public bool LogOutput { get; set; }
    public int MaxHistoryRecords { get; set; } = 1000;
    public DateTime CreatedAt { get; set; } = DateTime.UtcNow;
    public DateTime? ModifiedAt { get; set; }
}
