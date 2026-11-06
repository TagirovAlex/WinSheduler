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

    public Func<Guid, int, CancellationToken, Task<IReadOnlyList<RunHistory>>> HistoryFactory { get; set; }
        = (_, _, _) => Task.FromResult<IReadOnlyList<RunHistory>>([]);

    public Func<CancellationToken, Task<IReadOnlyList<TaskDto>>> ListTasksFactory { get; set; }
        = _ => Task.FromResult<IReadOnlyList<TaskDto>>([]);

    public Func<Guid, CancellationToken, Task<TaskDto>> GetTaskFactory { get; set; }
        = (id, _) => Task.FromResult(new TaskDto { Id = id });

    public Func<TaskDto, CancellationToken, Task<TaskDto>> CreateTaskFactory { get; set; }
        = (task, _) => Task.FromResult(task);

    public Func<TaskDto, CancellationToken, Task<TaskDto>> UpdateTaskFactory { get; set; }
        = (task, _) => Task.FromResult(task);

    public int DashboardCalls;
    public int StatusCalls;
    public int HistoryCalls;
    public int ListTasksCalls;

    public List<Guid> GetTaskCalls { get; } = [];

    public List<TaskDto> CreatedTasks { get; } = [];

    public List<TaskDto> UpdatedTasks { get; } = [];

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
    {
        Interlocked.Increment(ref ListTasksCalls);
        return ListTasksFactory(cancellationToken);
    }

    public Task<TaskDto> GetTaskAsync(Guid taskId, CancellationToken cancellationToken = default)
    {
        GetTaskCalls.Add(taskId);
        return GetTaskFactory(taskId, cancellationToken);
    }

    public Task<IReadOnlyList<TimeWindowDto>> GetTimeWindowsAsync(
        Guid taskId,
        CancellationToken cancellationToken = default)
        => Task.FromResult<IReadOnlyList<TimeWindowDto>>([]);

    public Task<IReadOnlyList<RunHistory>> GetHistoryAsync(
        Guid taskId,
        int limit = 100,
        CancellationToken cancellationToken = default)
    {
        Interlocked.Increment(ref HistoryCalls);
        return HistoryFactory(taskId, limit, cancellationToken);
    }

    public async Task<TaskDto> CreateTaskAsync(TaskDto task, CancellationToken cancellationToken = default)
    {
        var result = await CreateTaskFactory(task, cancellationToken).ConfigureAwait(false);
        CreatedTasks.Add(task);
        return result;
    }

    public async Task<TaskDto> UpdateTaskAsync(TaskDto task, CancellationToken cancellationToken = default)
    {
        var result = await UpdateTaskFactory(task, cancellationToken).ConfigureAwait(false);
        UpdatedTasks.Add(task);
        return result;
    }

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
