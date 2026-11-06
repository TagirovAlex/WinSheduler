using WinSheduler.Shared;

namespace WinSheduler.Shared.Models;

/// <summary>Режим импорта задач (контракт EXCHANGE.md, раздел 6).</summary>
public enum TaskImportMode
{
    /// <summary>Всегда создавать новую задачу с новым GUID; имя сохраняется.</summary>
    Copy,

    /// <summary>Пропускать задачи, чей id уже есть среди видимых; остальные создавать.</summary>
    Skip,

    /// <summary>Обновлять свои задачи с совпавшим id; остальные создавать с новым GUID.</summary>
    Overwrite,
}

/// <summary>Итог импорта одной задачи (контракт EXCHANGE.md, раздел 7).</summary>
public enum TaskImportOutcome
{
    /// <summary>Задача создана.</summary>
    Imported,

    /// <summary>Задача пропущена по режиму <see cref="TaskImportMode.Skip"/>.</summary>
    Skipped,

    /// <summary>Существующая своя задача обновлена.</summary>
    Updated,

    /// <summary>Задача не импортирована из-за ошибки.</summary>
    Failed,
}

/// <summary>
/// Документ обмена задачами (контракт EXCHANGE.md). Содержит только поля,
/// допустимые для создания и обновления; секреты и служебные поля сюда не входят.
/// </summary>
public sealed class TaskExchangeDocument
{
    /// <summary>Маркер формата; при экспорте равен <see cref="TaskExchangeJson.FormatName"/>.</summary>
    public string Format { get; set; } = "";

    /// <summary>Версия формата; поддерживается только <see cref="TaskExchangeJson.CurrentVersion"/>.</summary>
    public int FormatVersion { get; set; }

    /// <summary>Момент экспорта в UTC ISO 8601 с суффиксом Z.</summary>
    public string ExportedAt { get; set; } = "";

    /// <summary>Версия приложения-экспортёра; информационное поле.</summary>
    public string AppVersion { get; set; } = "";

    /// <summary>Экспортированные задачи.</summary>
    public List<TaskExchangeTask> Tasks { get; set; } = [];
}

/// <summary>
/// Задача в файле обмена. Намеренно не переиспользует <see cref="TaskDto"/>,
/// чтобы пароль и зашифрованный пароль не могли попасть в сериализацию.
/// </summary>
public sealed class TaskExchangeTask
{
    /// <summary>Исходный идентификатор; нужен для поиска конфликтов, но не является привилегией.</summary>
    public Guid Id { get; set; }

    public string Name { get; set; } = "";
    public string Description { get; set; } = "";
    public bool Enabled { get; set; } = true;
    public string ProgramPath { get; set; } = "";
    public string Arguments { get; set; } = "";
    public string WorkingDirectory { get; set; } = "";
    public WindowStyle WindowStyle { get; set; } = WindowStyle.Normal;
    public SessionMode SessionMode { get; set; } = SessionMode.Headless;
    public string? RunAsUser { get; set; }
    public string? RunAsDomain { get; set; }

    /// <summary>Только информационный флаг: сам пароль в файл не попадает.</summary>
    public bool HasPassword { get; set; }

    public OnErrorAction OnError { get; set; } = OnErrorAction.SkipNext;
    public int RetryCount { get; set; }
    public OnOverlapAction OnOverlap { get; set; } = OnOverlapAction.WaitComplete;
    public int? TimeoutMinutes { get; set; }
    public bool KillOnTimeout { get; set; }
    public bool LogOutput { get; set; }
    public int MaxHistoryRecords { get; set; } = 1000;

    /// <summary>Триггеры; id при импорте очищаются, служба создаёт новые GUID.</summary>
    public List<TimeWindowDto> TimeWindows { get; set; } = [];

    /// <summary>Собирает модель обмена из ответа IPC, отбрасывая секреты и служебные поля.</summary>
    public static TaskExchangeTask FromTaskDto(TaskDto task)
    {
        ArgumentNullException.ThrowIfNull(task);

        return new TaskExchangeTask
        {
            Id = task.Id,
            Name = task.Name,
            Description = task.Description,
            Enabled = task.Enabled,
            ProgramPath = task.ProgramPath,
            Arguments = task.Arguments,
            WorkingDirectory = task.WorkingDirectory,
            WindowStyle = task.WindowStyle,
            SessionMode = task.SessionMode,
            RunAsUser = task.RunAsUser,
            RunAsDomain = task.RunAsDomain,
            HasPassword = task.HasPassword,
            OnError = task.OnError,
            RetryCount = task.RetryCount,
            OnOverlap = task.OnOverlap,
            TimeoutMinutes = task.TimeoutMinutes,
            KillOnTimeout = task.KillOnTimeout,
            LogOutput = task.LogOutput,
            MaxHistoryRecords = task.MaxHistoryRecords,
            TimeWindows = [.. task.TimeWindows.Select(window => CloneTimeWindow(window, window.Id))],
        };
    }

    /// <summary>
    /// Готовит <see cref="TaskDto"/> для CreateTask/UpdateTask. Секреты не заполняются,
    /// идентификаторы триггеров всегда очищаются (служба сгенерирует новые).
    /// </summary>
    /// <param name="id">Целевой id задачи; null — использовать исходный id из файла.</param>
    public TaskDto ToTaskDto(Guid? id = null)
    {
        return new TaskDto
        {
            Id = id ?? Id,
            Name = Name,
            Description = Description,
            Enabled = Enabled,
            ProgramPath = ProgramPath,
            Arguments = Arguments,
            WorkingDirectory = WorkingDirectory,
            WindowStyle = WindowStyle,
            SessionMode = SessionMode,
            RunAsUser = RunAsUser,
            RunAsDomain = RunAsDomain,
            HasPassword = HasPassword,
            OnError = OnError,
            RetryCount = RetryCount,
            OnOverlap = OnOverlap,
            TimeoutMinutes = TimeoutMinutes,
            KillOnTimeout = KillOnTimeout,
            LogOutput = LogOutput,
            MaxHistoryRecords = MaxHistoryRecords,
            TimeWindows = [.. TimeWindows.Select(window => CloneTimeWindow(window, Guid.Empty))],

            // Password, RunAsPasswordEncrypted и OwnerSid намеренно не заполняются:
            // пароли из файла не импортируются, владельца назначает служба.
        };
    }

    private static TimeWindowDto CloneTimeWindow(TimeWindowDto source, Guid id)
    {
        return new TimeWindowDto
        {
            Id = id,
            Type = source.Type,
            Kind = source.Kind,
            StartDate = source.StartDate,
            EndDate = source.EndDate,
            DaysOfWeek = [.. source.DaysOfWeek],
            DaysOfMonth = [.. source.DaysOfMonth],
            SpecificDates = [.. source.SpecificDates],
            ExactTimes = [.. source.ExactTimes],
            StartTime = source.StartTime,
            EndTime = source.EndTime,
            RepeatIntervalMinutes = source.RepeatIntervalMinutes,
            RepeatUntil = source.RepeatUntil,
            DayInterval = source.DayInterval,
            WeekInterval = source.WeekInterval,
            MonthInterval = source.MonthInterval,
        };
    }
}

/// <summary>Параметры импорта; лимиты можно уменьшить в тестах.</summary>
public sealed class TaskExchangeOptions
{
    /// <summary>Режим импорта; по умолчанию копия.</summary>
    public TaskImportMode Mode { get; init; } = TaskImportMode.Copy;

    /// <summary>Максимальный размер файла в байтах.</summary>
    public long MaxFileBytes { get; init; } = TaskExchangeJson.MaxFileBytes;

    /// <summary>Максимальное число задач в файле.</summary>
    public int MaxTasks { get; init; } = TaskExchangeJson.MaxTasks;

    /// <summary>Максимальное число триггеров на задачу.</summary>
    public int MaxTriggersPerTask { get; init; } = TaskExchangeJson.MaxTriggersPerTask;

    /// <summary>Максимальная длина строки в символах.</summary>
    public int MaxStringLength { get; init; } = TaskExchangeJson.MaxStringLength;
}

/// <summary>Результат импорта одной задачи для итогового отчёта.</summary>
public sealed class TaskImportItemResult
{
    /// <summary>Исходный id из файла; пустой, если id отсутствовал.</summary>
    public Guid SourceId { get; set; }

    /// <summary>Имя задачи из файла (для отображения в отчёте).</summary>
    public string Name { get; set; } = "";

    public TaskImportOutcome Outcome { get; set; }

    /// <summary>Код ошибки IPC либо локальной валидации; null при успехе.</summary>
    public string? ErrorCode { get; set; }

    /// <summary>Причина ошибки или предупреждение; без секретов.</summary>
    public string Message { get; set; } = "";
}

/// <summary>Итоговый отчёт импорта: счётчики, результаты по задачам и предупреждения.</summary>
public sealed class TaskImportReport
{
    public TaskImportMode Mode { get; set; } = TaskImportMode.Copy;

    public List<TaskImportItemResult> Items { get; set; } = [];

    /// <summary>Общие предупреждения (например, про пароль или лишние поля файла).</summary>
    public List<string> Warnings { get; set; } = [];

    public int Imported { get; set; }
    public int Skipped { get; set; }
    public int Updated { get; set; }
    public int Failed { get; set; }
}
