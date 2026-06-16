namespace WinSheduler.Shared.Models;

public class RunHistory
{
    public Guid Id { get; set; } = Guid.NewGuid();
    public Guid TaskId { get; set; }
    public DateTime StartTime { get; set; }
    public DateTime? EndTime { get; set; }
    public int? ExitCode { get; set; }
    public int Pid { get; set; }
    public RunStatus Status { get; set; }
    public string ErrorMessage { get; set; } = "";
    public string? OutputPath { get; set; }
}
