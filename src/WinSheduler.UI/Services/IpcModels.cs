using WinSheduler.Shared;

namespace WinSheduler.UI.Services;

/// <summary>Ответ <c>Ping</c>.</summary>
public sealed class PingResult
{
    public bool Pong { get; init; }
    public string ServiceVersion { get; init; } = string.Empty;
}

/// <summary>
/// Ответ batch-метода <c>GetDashboard</c>: заменяет серию
/// <c>ListTasks + N × GetTimeWindows + N × GetHistory</c> (контракт IPC.md, раздел 4).
/// </summary>
public sealed class DashboardDto
{
    public IReadOnlyList<DashboardTaskDto> Tasks { get; init; } = [];
}

/// <summary>Строка списка задач в дашборде.</summary>
public sealed class DashboardTaskDto
{
    public Guid Id { get; init; }
    public string Name { get; init; } = string.Empty;
    public bool Enabled { get; init; }

    /// <summary>Задача принадлежит текущему пользователю.</summary>
    public bool IsOwn { get; init; }

    /// <summary>Подпись владельца: «Вы» или «Другой пользователь». SID не раскрывается.</summary>
    public string OwnerLabel { get; init; } = string.Empty;

    public string ProgramPath { get; init; } = string.Empty;

    /// <summary>Готовая подпись расписания, например «Ежедневно 02:00».</summary>
    public string ScheduleSummary { get; init; } = string.Empty;

    /// <summary>Последний запуск, если он был.</summary>
    public DashboardLastRunDto? LastRun { get; init; }

    /// <summary>Задача выполняется прямо сейчас.</summary>
    public bool Running { get; init; }
}

/// <summary>Краткие сведения о последнем запуске задачи.</summary>
public sealed class DashboardLastRunDto
{
    public DateTimeOffset? StartTime { get; init; }
    public RunStatus Status { get; init; }
    public int? ExitCode { get; init; }
}

/// <summary>Запись аудита (доступна только администратору).</summary>
public sealed class AuditEntryDto
{
    public DateTimeOffset Timestamp { get; init; }
    public string ActorSid { get; init; } = string.Empty;
    public string Action { get; init; } = string.Empty;
    public string ObjectType { get; init; } = string.Empty;
    public string ObjectId { get; init; } = string.Empty;
    public string Result { get; init; } = string.Empty;
    public string Details { get; init; } = string.Empty;
}
