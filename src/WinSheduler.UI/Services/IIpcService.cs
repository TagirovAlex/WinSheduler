using WinSheduler.Shared;
using WinSheduler.Shared.Models;

namespace WinSheduler.UI.Services;

/// <summary>
/// Типизированный клиент службы WinSheduler. Реализует контракт IPC.md:
/// одно соединение, кадры «длина + JSON», последовательные запросы.
///
/// Ошибки: <see cref="IpcException"/> с типизированным кодом, для недоступной
/// службы — <see cref="IpcUnavailableException"/>. Для показа пользователю
/// используйте <see cref="IpcErrorText"/>.
///
/// Все методы принимают <see cref="CancellationToken"/>; отмена не оставляет
/// соединение в неопределённом состоянии.
/// </summary>
public interface IIpcService : IAsyncDisposable
{
    /// <summary>Проверка доступности службы.</summary>
    Task<PingResult> PingAsync(CancellationToken cancellationToken = default);

    /// <summary>Состояние службы и роль текущего пользователя.</summary>
    Task<ServiceStatus> GetStatusAsync(CancellationToken cancellationToken = default);

    /// <summary>
    /// Один batch-запрос для главного экрана: задачи, расписание и последний запуск.
    /// Заменяет серию <c>ListTasks + N × GetTimeWindows + N × GetHistory</c>.
    /// </summary>
    Task<DashboardDto> GetDashboardAsync(CancellationToken cancellationToken = default);

    /// <summary>Список задач, доступных текущему пользователю.</summary>
    Task<IReadOnlyList<TaskDto>> ListTasksAsync(CancellationToken cancellationToken = default);

    /// <summary>Полная задача по идентификатору.</summary>
    Task<TaskDto> GetTaskAsync(Guid taskId, CancellationToken cancellationToken = default);

    /// <summary>Триггеры задачи.</summary>
    Task<IReadOnlyList<TimeWindowDto>> GetTimeWindowsAsync(Guid taskId, CancellationToken cancellationToken = default);

    /// <summary>История запусков задачи (не более 1000 записей).</summary>
    Task<IReadOnlyList<RunHistory>> GetHistoryAsync(
        Guid taskId,
        int limit = 100,
        CancellationToken cancellationToken = default);

    /// <summary>Создаёт задачу и возвращает сохранённый вариант.</summary>
    Task<TaskDto> CreateTaskAsync(TaskDto task, CancellationToken cancellationToken = default);

    /// <summary>Обновляет задачу целиком, включая набор триггеров.</summary>
    Task<TaskDto> UpdateTaskAsync(TaskDto task, CancellationToken cancellationToken = default);

    /// <summary>Удаляет задачу.</summary>
    Task DeleteTaskAsync(Guid taskId, CancellationToken cancellationToken = default);

    /// <summary>Включает или отключает задачу.</summary>
    Task SetTaskEnabledAsync(Guid taskId, bool enabled, CancellationToken cancellationToken = default);

    /// <summary>Немедленный запуск задачи через планировщик.</summary>
    Task RunNowAsync(Guid taskId, CancellationToken cancellationToken = default);

    /// <summary>Аудит за период. Только администратор.</summary>
    Task<IReadOnlyList<AuditEntryDto>> GetAuditAsync(
        DateTimeOffset from,
        DateTimeOffset to,
        int limit = 500,
        CancellationToken cancellationToken = default);

    /// <summary>Настройки службы. Только администратор.</summary>
    Task<IReadOnlyDictionary<string, string>> GetSettingsAsync(CancellationToken cancellationToken = default);

    /// <summary>Изменение настроек службы. Только администратор.</summary>
    Task SetSettingsAsync(
        IReadOnlyDictionary<string, string> settings,
        CancellationToken cancellationToken = default);
}
