namespace WinSheduler.Shared;

public class IpcRequest
{
    public string Action { get; set; } = "";
    public string Payload { get; set; } = "";
}

public class IpcResponse
{
    public bool Success { get; set; }
    public string Error { get; set; } = "";
    public string Payload { get; set; } = "";
}

public class ServiceStatus
{
    public bool Running { get; set; }
    public int TaskCount { get; set; }
    public int RunningTaskCount { get; set; }
    public DateTime StartedAt { get; set; }
}
