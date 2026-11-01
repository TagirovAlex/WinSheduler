using WinSheduler.Shared;
using WinSheduler.Shared.Models;
using WinSheduler.UI.Services;

namespace WinSheduler.UI.Tests.Fakes;

/// <summary>
/// Поддельная служба IPC для тестов ViewModel: поведение задаётся делегатами,
/// число вызовов считается.
/// </summary>
internal sealed class FakeIpcService : IIpcService
{
    public Func<CancellationToken, Task<DashboardDto>> DashboardFactory { get; set; }
        = _ => Task.FromResult(new DashboardDto());

    public Func<CancellationToken, Task<ServiceStatus>> StatusFactory { get; set; }
        = _ => Task.FromResult(new ServiceStatus { Running = true, IsOperator = true });

    public int DashboardCalls;
    public int StatusCalls;

    public Task<PingResult> PingAsync(CancellationToken cancellationToken = default)
        => Task.FromResult(new PingResult { Pong = true, ServiceVersion = "1.0.0" });

    public Task<ServiceStatus> GetStatusAsync(CancellationToken cancellationToken = default)
    {
        Interlocked.Increment(ref StatusCalls);
        return StatusFactory(cancellationToken);
    }

    public Task<DashboardDto> GetDashboardAsync(CancellationToken cancellationToken = default)
    {
        Interlocked.Increment(ref DashboardCalls);
        return DashboardFactory(cancellationToken);
    }

    public Task<IReadOnlyList<TaskDto>> ListTasksAsync(CancellationToken cancellationToken = default)
        => Task.FromResult<IReadOnlyList<TaskDto>>([]);

    public Task<TaskDto> GetTaskAsync(Guid taskId, CancellationToken cancellationToken = default)
        => Task.FromResult(new TaskDto { Id = taskId });

    public Task<IReadOnlyList<TimeWindowDto>> GetTimeWindowsAsync(
        Guid taskId,
        CancellationToken cancellationToken = default)
        => Task.FromResult<IReadOnlyList<TimeWindowDto>>([]);

    public Task<IReadOnlyList<RunHistory>> GetHistoryAsync(
        Guid taskId,
        int limit = 100,
        CancellationToken cancellationToken = default)
        => Task.FromResult<IReadOnlyList<RunHistory>>([]);

    public Task<TaskDto> CreateTaskAsync(TaskDto task, CancellationToken cancellationToken = default)
        => Task.FromResult(task);

    public Task<TaskDto> UpdateTaskAsync(TaskDto task, CancellationToken cancellationToken = default)
        => Task.FromResult(task);

    public Task DeleteTaskAsync(Guid taskId, CancellationToken cancellationToken = default)
        => Task.CompletedTask;

    public Task SetTaskEnabledAsync(Guid taskId, bool enabled, CancellationToken cancellationToken = default)
        => Task.CompletedTask;

    public Task RunNowAsync(Guid taskId, CancellationToken cancellationToken = default)
        => Task.CompletedTask;

    public Task<IReadOnlyList<AuditEntryDto>> GetAuditAsync(
        DateTimeOffset from,
        DateTimeOffset to,
        int limit = 500,
        CancellationToken cancellationToken = default)
        => Task.FromResult<IReadOnlyList<AuditEntryDto>>([]);

    public Task<IReadOnlyDictionary<string, string>> GetSettingsAsync(
        CancellationToken cancellationToken = default)
        => Task.FromResult<IReadOnlyDictionary<string, string>>(new Dictionary<string, string>());

    public Task SetSettingsAsync(
        IReadOnlyDictionary<string, string> settings,
        CancellationToken cancellationToken = default)
        => Task.CompletedTask;

    public ValueTask DisposeAsync() => ValueTask.CompletedTask;
}
