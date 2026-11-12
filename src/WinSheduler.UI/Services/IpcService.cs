using WinSheduler.Shared;
using WinSheduler.Shared.Models;

namespace WinSheduler.UI.Services;

/// <summary>
/// Реализация <see cref="IIpcService"/> поверх <see cref="IpcClient"/>.
/// Имена действий и payload соответствуют контракту IPC.md.
/// </summary>
public sealed class IpcService : IIpcService
{
    private readonly IpcClient _client;

    public IpcService()
        : this(new IpcClient())
    {
    }

    public IpcService(IpcClient client)
    {
        _client = client ?? throw new ArgumentNullException(nameof(client));
    }

    /// <inheritdoc />
    public Task<PingResult> PingAsync(CancellationToken cancellationToken = default)
        => _client.CallAsync<PingResult>("Ping", null, idempotent: true, cancellationToken);

    /// <inheritdoc />
    public Task<ServiceStatus> GetStatusAsync(CancellationToken cancellationToken = default)
        => _client.CallAsync<ServiceStatus>("GetStatus", null, idempotent: true, cancellationToken);

    /// <inheritdoc />
    public Task<DashboardDto> GetDashboardAsync(CancellationToken cancellationToken = default)
        => _client.CallAsync<DashboardDto>("GetDashboard", null, idempotent: true, cancellationToken);

    /// <inheritdoc />
    public async Task<IReadOnlyList<TaskDto>> ListTasksAsync(CancellationToken cancellationToken = default)
        => await _client.CallAsync<List<TaskDto>>("ListTasks", null, idempotent: true, cancellationToken)
            .ConfigureAwait(false);

    /// <inheritdoc />
    public Task<TaskDto> GetTaskAsync(Guid taskId, CancellationToken cancellationToken = default)
        => _client.CallAsync<TaskDto>("GetTask", new { taskId }, idempotent: true, cancellationToken);

    /// <inheritdoc />
    public async Task<IReadOnlyList<TimeWindowDto>> GetTimeWindowsAsync(
        Guid taskId,
        CancellationToken cancellationToken = default)
        => await _client.CallAsync<List<TimeWindowDto>>("GetTimeWindows", new { taskId }, idempotent: true, cancellationToken)
            .ConfigureAwait(false);

    /// <inheritdoc />
    public async Task<IReadOnlyList<RunHistory>> GetHistoryAsync(
        Guid taskId,
        int limit = 100,
        CancellationToken cancellationToken = default)
    {
        var safeLimit = Math.Clamp(limit, 1, 1000);
        return await _client.CallAsync<List<RunHistory>>(
                "GetHistory",
                new { taskId, limit = safeLimit },
                idempotent: true,
                cancellationToken)
            .ConfigureAwait(false);
    }

    /// <inheritdoc />
    public Task<TaskDto> CreateTaskAsync(TaskDto task, CancellationToken cancellationToken = default)
        => _client.CallAsync<TaskDto>("CreateTask", task, idempotent: false, cancellationToken);

    /// <inheritdoc />
    public Task<TaskDto> UpdateTaskAsync(TaskDto task, CancellationToken cancellationToken = default)
        => _client.CallAsync<TaskDto>("UpdateTask", task, idempotent: false, cancellationToken);

    /// <inheritdoc />
    public Task DeleteTaskAsync(Guid taskId, CancellationToken cancellationToken = default)
        => _client.CallAsync("DeleteTask", new { taskId }, idempotent: false, cancellationToken);

    /// <inheritdoc />
    public Task SetTaskEnabledAsync(Guid taskId, bool enabled, CancellationToken cancellationToken = default)
        => _client.CallAsync("SetTaskEnabled", new { taskId, enabled }, idempotent: false, cancellationToken);

    /// <inheritdoc />
    public Task RunNowAsync(Guid taskId, CancellationToken cancellationToken = default)
        => _client.CallAsync("RunNow", new { taskId }, idempotent: false, cancellationToken);

    /// <inheritdoc />
    public Task StopTaskAsync(Guid taskId, CancellationToken cancellationToken = default)
        => _client.CallAsync("StopTask", new { taskId }, idempotent: false, cancellationToken);

    /// <inheritdoc />
    public async Task<IReadOnlyList<AuditEntryDto>> GetAuditAsync(
        DateTimeOffset from,
        DateTimeOffset to,
        int limit = 500,
        CancellationToken cancellationToken = default)
    {
        var safeLimit = Math.Clamp(limit, 1, 1000);
        return await _client.CallAsync<List<AuditEntryDto>>(
                "GetAudit",
                new { from = from.UtcDateTime, to = to.UtcDateTime, limit = safeLimit },
                idempotent: true,
                cancellationToken)
            .ConfigureAwait(false);
    }

    /// <inheritdoc />
    public async Task<IReadOnlyDictionary<string, string>> GetSettingsAsync(
        CancellationToken cancellationToken = default)
        => await _client.CallAsync<Dictionary<string, string>>("GetSettings", null, idempotent: true, cancellationToken)
            .ConfigureAwait(false);

    /// <inheritdoc />
    public Task SetSettingsAsync(
        IReadOnlyDictionary<string, string> settings,
        CancellationToken cancellationToken = default)
        => _client.CallAsync("SetSettings", settings, idempotent: false, cancellationToken);

    /// <inheritdoc />
    public ValueTask DisposeAsync() => _client.DisposeAsync();
}
