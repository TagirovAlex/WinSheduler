using System.Globalization;
using System.IO;
using System.Text;
using System.Text.Json;
using WinSheduler.Shared;
using WinSheduler.Shared.Models;
using WinSheduler.UI.Localization;

namespace WinSheduler.UI.Services;

/// <summary>
/// Ошибка обмена задачами: повреждённый файл, неподдерживаемая версия формата
/// или превышение лимитов. Сообщение уже пригодно для показа пользователю.
/// </summary>
public sealed class TaskExchangeException : Exception
{
    public TaskExchangeException(string message, Exception? innerException = null)
        : base(message, innerException)
    {
    }
}

/// <summary>Результат чтения файла обмена для предпросмотра импорта.</summary>
public sealed class TaskImportPreview
{
    /// <summary>Путь к файлу, из которого прочитан документ.</summary>
    public string FilePath { get; init; } = "";

    /// <summary>Разобранный документ; задачи уже нормализованы.</summary>
    public TaskExchangeDocument Document { get; init; } = new();

    /// <summary>Общие предупреждения без секретов и полного содержимого файла.</summary>
    public IReadOnlyList<string> Warnings { get; init; } = [];

    /// <summary>Задачи файла.</summary>
    public IReadOnlyList<TaskExchangeTask> Tasks => Document.Tasks;
}

/// <summary>
/// Экспорт и импорт задач через существующие действия IPC (контракт EXCHANGE.md).
/// Сервис не зависит от WPF: файлы, JSON и вызовы <see cref="IIpcService"/>.
/// </summary>
public sealed class TaskExchangeService
{
    private static readonly HashSet<string> ForbiddenFields = new(StringComparer.OrdinalIgnoreCase)
    {
        "password",
        "encryptedPassword",
        "runAsPasswordEncrypted",
        "owner",
        "createdAt",
        "modifiedAt",
    };

    private readonly IIpcService _ipc;
    private readonly AppLogger? _logger;

    public TaskExchangeService(IIpcService ipc, AppLogger? logger = null)
    {
        _ipc = ipc ?? throw new ArgumentNullException(nameof(ipc));
        _logger = logger;
    }

    /// <summary>Версия приложения-экспортёра для шапки документа.</summary>
    private static string AppVersion
        => typeof(TaskExchangeService).Assembly.GetName().Version?.ToString(3) ?? "2.1.0";

    /// <summary>
    /// Экспортирует выбранные задачи в JSON. Документ собирается в памяти полностью:
    /// ошибка любой задачи прерывает экспорт до создания файла.
    /// </summary>
    public async Task ExportAsync(
        IEnumerable<Guid> taskIds,
        string path,
        CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(taskIds);
        if (string.IsNullOrWhiteSpace(path))
        {
            throw new ArgumentException(Strings.Get("Exchange_ErrorPath"), nameof(path));
        }

        var tasks = new List<TaskExchangeTask>();
        foreach (var taskId in taskIds)
        {
            cancellationToken.ThrowIfCancellationRequested();

            var dto = await _ipc.GetTaskAsync(taskId, cancellationToken).ConfigureAwait(false);
            tasks.Add(TaskExchangeTask.FromTaskDto(dto));
        }

        var document = TaskExchangeJson.CreateDocument(AppVersion, tasks);
        var json = JsonSerializer.Serialize(document, TaskExchangeJson.Options);

        // UTF-8 без BOM: контракт EXCHANGE.md, раздел 2.
        await File.WriteAllTextAsync(
                path,
                json,
                new UTF8Encoding(encoderShouldEmitUTF8Identifier: false),
                cancellationToken)
            .ConfigureAwait(false);

        _logger?.Info("exchange", $"Экспортировано задач: {tasks.Count}.");
    }

    /// <summary>Импорт с режимом и лимитами по умолчанию.</summary>
    public Task<TaskImportReport> ImportAsync(
        string path,
        TaskImportMode mode,
        IProgress<TaskImportItemResult>? progress = null,
        CancellationToken cancellationToken = default)
        => ImportAsync(path, new TaskExchangeOptions { Mode = mode }, progress, cancellationToken);

    /// <summary>
    /// Импортирует задачи из файла. Ошибки шапки прерывают импорт целиком,
    /// ошибки отдельной задачи попадают в отчёт, остальные задачи продолжают обрабатываться.
    /// </summary>
    public async Task<TaskImportReport> ImportAsync(
        string path,
        TaskExchangeOptions options,
        IProgress<TaskImportItemResult>? progress = null,
        CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(options);
        if (string.IsNullOrWhiteSpace(path))
        {
            throw new ArgumentException(Strings.Get("Exchange_ErrorPath"), nameof(path));
        }

        var (document, loadWarnings) = await LoadAsync(path, options, cancellationToken).ConfigureAwait(false);

        var report = new TaskImportReport { Mode = options.Mode };
        foreach (var warning in loadWarnings)
        {
            AddWarning(report.Warnings, warning);
        }

        IReadOnlySet<Guid>? visibleIds = null;
        if (options.Mode == TaskImportMode.Skip)
        {
            var visible = await _ipc.ListTasksAsync(cancellationToken).ConfigureAwait(false);
            visibleIds = visible.Select(static task => task.Id).ToHashSet();
        }

        foreach (var exchange in document.Tasks)
        {
            cancellationToken.ThrowIfCancellationRequested();

            if (exchange is null)
            {
                AddResult(
                    report,
                    new TaskImportItemResult
                    {
                        Outcome = TaskImportOutcome.Failed,
                        ErrorCode = "VALIDATION",
                        Message = Strings.Get("Exchange_ValidationTask"),
                    },
                    progress);
                continue;
            }

            Normalize(exchange);

            if (exchange.HasPassword)
            {
                AddWarning(report.Warnings, Strings.Format("Exchange_WarningPassword", exchange.Name));
            }

            if (!TryValidate(exchange, options, out var reason))
            {
                AddResult(
                    report,
                    new TaskImportItemResult
                    {
                        SourceId = exchange.Id,
                        Name = exchange.Name,
                        Outcome = TaskImportOutcome.Failed,
                        ErrorCode = "VALIDATION",
                        Message = reason,
                    },
                    progress);
                continue;
            }

            var item = new TaskImportItemResult
            {
                SourceId = exchange.Id,
                Name = exchange.Name,
            };

            try
            {
                var operation = await ResolveOperationAsync(exchange, options.Mode, visibleIds, cancellationToken)
                    .ConfigureAwait(false);

                switch (operation.Kind)
                {
                    case ImportOperationKind.Skip:
                        item.Outcome = TaskImportOutcome.Skipped;
                        item.Message = Strings.Get("Exchange_SkipMessage");
                        break;

                    case ImportOperationKind.Update:
                        await _ipc.UpdateTaskAsync(BuildTaskDto(exchange, operation.Id), cancellationToken)
                            .ConfigureAwait(false);
                        item.Outcome = TaskImportOutcome.Updated;
                        break;

                    default:
                        await _ipc.CreateTaskAsync(BuildTaskDto(exchange, operation.Id), cancellationToken)
                            .ConfigureAwait(false);
                        item.Outcome = TaskImportOutcome.Imported;
                        break;
                }
            }
            catch (OperationCanceledException)
            {
                throw;
            }
            catch (IpcException ex) when (ex.Code == IpcErrorCode.Unavailable)
            {
                item.Outcome = TaskImportOutcome.Failed;
                item.ErrorCode = ErrorCodeOf(ex);
                item.Message = IpcErrorText.Describe(ex);
                AddResult(report, item, progress);
                AddWarning(report.Warnings, Strings.Get("Exchange_ImportAborted"));
                break;
            }
            catch (IpcException ex)
            {
                item.Outcome = TaskImportOutcome.Failed;
                item.ErrorCode = ErrorCodeOf(ex);
                item.Message = IpcErrorText.Describe(ex);
            }
            catch (Exception ex)
            {
                item.Outcome = TaskImportOutcome.Failed;
                item.ErrorCode = "INTERNAL";
                item.Message = IpcErrorText.Describe(ex);
            }

            AddResult(report, item, progress);
        }

        _logger?.Info(
            "exchange",
            $"Импорт: создано {report.Imported}, пропущено {report.Skipped}, обновлено {report.Updated}, ошибок {report.Failed}.");

        return report;
    }

    /// <summary>Читает файл и готовит предпросмотр без вызова CreateTask/UpdateTask.</summary>
    public Task<TaskImportPreview> PreviewAsync(
        string path,
        CancellationToken cancellationToken = default)
        => PreviewAsync(path, new TaskExchangeOptions(), cancellationToken);

    /// <summary>Читает файл с заданными лимитами и готовит предпросмотр.</summary>
    public async Task<TaskImportPreview> PreviewAsync(
        string path,
        TaskExchangeOptions options,
        CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(options);
        if (string.IsNullOrWhiteSpace(path))
        {
            throw new ArgumentException(Strings.Get("Exchange_ErrorPath"), nameof(path));
        }

        var (document, warnings) = await LoadAsync(path, options, cancellationToken).ConfigureAwait(false);
        return new TaskImportPreview
        {
            FilePath = path,
            Document = document,
            Warnings = warnings,
        };
    }

    private async Task<(TaskExchangeDocument Document, List<string> Warnings)> LoadAsync(
        string path,
        TaskExchangeOptions options,
        CancellationToken cancellationToken)
    {
        var info = new FileInfo(path);
        if (!info.Exists)
        {
            throw new TaskExchangeException(Strings.Get("Exchange_ErrorFileMissing"));
        }

        if (info.Length > options.MaxFileBytes)
        {
            throw new TaskExchangeException(
                Strings.Format("Exchange_ErrorFileTooLarge", options.MaxFileBytes));
        }

        string text;
        try
        {
            text = await File.ReadAllTextAsync(path, Encoding.UTF8, cancellationToken).ConfigureAwait(false);
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
        {
            throw new TaskExchangeException(Strings.Format("Exchange_ErrorRead", ex.Message), ex);
        }

        JsonDocument json;
        try
        {
            json = JsonDocument.Parse(text);
        }
        catch (JsonException ex)
        {
            throw new TaskExchangeException(Strings.Get("Exchange_ErrorJson"), ex);
        }

        using (json)
        {
            var root = json.RootElement;
            if (root.ValueKind != JsonValueKind.Object)
            {
                throw new TaskExchangeException(Strings.Get("Exchange_ErrorFormat"));
            }

            TaskExchangeDocument? document;
            try
            {
                document = root.Deserialize<TaskExchangeDocument>(TaskExchangeJson.Options);
            }
            catch (JsonException ex)
            {
                throw new TaskExchangeException(Strings.Get("Exchange_ErrorJson"), ex);
            }
            catch (NotSupportedException ex)
            {
                throw new TaskExchangeException(Strings.Get("Exchange_ErrorJson"), ex);
            }

            if (document is null
                || !string.Equals(document.Format, TaskExchangeJson.FormatName, StringComparison.Ordinal))
            {
                throw new TaskExchangeException(Strings.Get("Exchange_ErrorFormat"));
            }

            if (document.FormatVersion > TaskExchangeJson.CurrentVersion)
            {
                throw new TaskExchangeException(Strings.Get("Exchange_ErrorNewerVersion"));
            }

            if (document.FormatVersion < 1)
            {
                throw new TaskExchangeException(Strings.Get("Exchange_ErrorFormat"));
            }

            document.Tasks ??= [];
            if (document.Tasks.Count > options.MaxTasks)
            {
                throw new TaskExchangeException(
                    Strings.Format("Exchange_ErrorTooManyTasks", document.Tasks.Count, options.MaxTasks));
            }

            var warnings = new List<string>();
            if (HasForbiddenFields(root))
            {
                AddWarning(warnings, Strings.Get("Exchange_WarningSecretFields"));
            }

            foreach (var task in document.Tasks)
            {
                if (task is null)
                {
                    continue;
                }

                Normalize(task);
                AddPreviewWarnings(task, warnings);
            }

            return (document, warnings);
        }
    }

    private async Task<ImportOperation> ResolveOperationAsync(
        TaskExchangeTask exchange,
        TaskImportMode mode,
        IReadOnlySet<Guid>? visibleIds,
        CancellationToken cancellationToken)
    {
        switch (mode)
        {
            case TaskImportMode.Skip:
                return exchange.Id != Guid.Empty
                       && visibleIds is not null
                       && visibleIds.Contains(exchange.Id)
                    ? new ImportOperation(ImportOperationKind.Skip, exchange.Id)
                    : new ImportOperation(ImportOperationKind.Create, exchange.Id);

            case TaskImportMode.Overwrite:
                if (exchange.Id == Guid.Empty)
                {
                    return new ImportOperation(ImportOperationKind.Create, Guid.NewGuid());
                }

                try
                {
                    await _ipc.GetTaskAsync(exchange.Id, cancellationToken).ConfigureAwait(false);
                    return new ImportOperation(ImportOperationKind.Update, exchange.Id);
                }
                catch (IpcException ex) when (ex.Code is IpcErrorCode.NotFound or IpcErrorCode.AuthDenied)
                {
                    // Задача не наша или уже удалена: создаём копию с новым идентификатором.
                    return new ImportOperation(ImportOperationKind.Create, Guid.NewGuid());
                }

            default:
                return new ImportOperation(ImportOperationKind.Create, Guid.NewGuid());
        }
    }

    private static TaskDto BuildTaskDto(TaskExchangeTask exchange, Guid id)
    {
        var dto = exchange.ToTaskDto(id);

        // Пароль из файла не импортируется: задача с паролем создаётся выключенной.
        if (exchange.HasPassword)
        {
            dto.Enabled = false;
        }

        return dto;
    }

    private static bool TryValidate(
        TaskExchangeTask task,
        TaskExchangeOptions options,
        out string reason)
    {
        reason = "";

        if (string.IsNullOrWhiteSpace(task.Name))
        {
            reason = Strings.Get("Exchange_ValidationName");
            return false;
        }

        if (task.TimeWindows.Count > options.MaxTriggersPerTask)
        {
            reason = Strings.Format("Exchange_ValidationTriggers", options.MaxTriggersPerTask);
            return false;
        }

        foreach (var (field, value) in EnumerateStrings(task))
        {
            if (value is not null && value.Length > options.MaxStringLength)
            {
                reason = Strings.Format("Exchange_ValidationString", field, options.MaxStringLength);
                return false;
            }
        }

        if (!Enum.IsDefined(typeof(WindowStyle), task.WindowStyle)
            || !Enum.IsDefined(typeof(SessionMode), task.SessionMode)
            || !Enum.IsDefined(typeof(OnErrorAction), task.OnError)
            || !Enum.IsDefined(typeof(OnOverlapAction), task.OnOverlap))
        {
            reason = Strings.Format("Exchange_ValidationEnum", "windowStyle/sessionMode/onError/onOverlap");
            return false;
        }

        if (task.RetryCount is < 0 or > 10)
        {
            reason = Strings.Format("Exchange_ValidationRange", "retryCount");
            return false;
        }

        if (task.TimeoutMinutes is < 0 or > 10080)
        {
            reason = Strings.Format("Exchange_ValidationRange", "timeoutMinutes");
            return false;
        }

        if (task.MaxHistoryRecords is < 1 or > 10000)
        {
            reason = Strings.Format("Exchange_ValidationRange", "maxHistoryRecords");
            return false;
        }

        foreach (var window in task.TimeWindows)
        {
            if (!Enum.IsDefined(typeof(TimeWindowType), window.Type)
                || !Enum.IsDefined(typeof(TriggerKind), window.Kind))
            {
                reason = Strings.Format("Exchange_ValidationEnum", "timeWindows.type/kind");
                return false;
            }

            if (window.ExactTimes.Any(static value => string.IsNullOrEmpty(value) || !IsValidTime(value)))
            {
                reason = Strings.Format("Exchange_ValidationTime", "exactTimes");
                return false;
            }

            if (!IsValidTime(window.StartTime)
                || !IsValidTime(window.EndTime)
                || !IsValidTime(window.RepeatUntil))
            {
                reason = Strings.Format("Exchange_ValidationTime", "startTime/endTime/repeatUntil");
                return false;
            }

            if (window.DaysOfWeek.Any(static day => !Enum.IsDefined(typeof(DayOfWeek), day)))
            {
                reason = Strings.Format("Exchange_ValidationRange", "daysOfWeek");
                return false;
            }

            if (window.DaysOfMonth.Any(static day => day is < 1 or > 31))
            {
                reason = Strings.Format("Exchange_ValidationRange", "daysOfMonth");
                return false;
            }

            if (window.RepeatIntervalMinutes is < 1 or > 1440
                || window.DayInterval < 1
                || window.WeekInterval < 1
                || window.MonthInterval < 1)
            {
                reason = Strings.Format("Exchange_ValidationRange", "timeWindows");
                return false;
            }
        }

        return true;
    }

    private static IEnumerable<(string Field, string? Value)> EnumerateStrings(TaskExchangeTask task)
    {
        yield return ("name", task.Name);
        yield return ("description", task.Description);
        yield return ("programPath", task.ProgramPath);
        yield return ("arguments", task.Arguments);
        yield return ("workingDirectory", task.WorkingDirectory);
        yield return ("runAsUser", task.RunAsUser);
        yield return ("runAsDomain", task.RunAsDomain);

        foreach (var window in task.TimeWindows)
        {
            yield return ("timeWindows.startTime", window.StartTime);
            yield return ("timeWindows.endTime", window.EndTime);
            yield return ("timeWindows.repeatUntil", window.RepeatUntil);

            foreach (var value in window.ExactTimes)
            {
                yield return ("timeWindows.exactTimes", value);
            }
        }
    }

    private static bool IsValidTime(string? value)
        => string.IsNullOrEmpty(value)
           || DateTime.TryParseExact(
               value,
               "HH:mm",
               CultureInfo.InvariantCulture,
               DateTimeStyles.None,
               out _);

    private static void Normalize(TaskExchangeTask task)
    {
        task.Name ??= "";
        task.Description ??= "";
        task.ProgramPath ??= "";
        task.Arguments ??= "";
        task.WorkingDirectory ??= "";

        task.TimeWindows ??= [];
        task.TimeWindows.RemoveAll(static window => window is null);

        foreach (var window in task.TimeWindows)
        {
            window.DaysOfWeek ??= [];
            window.DaysOfMonth ??= [];
            window.SpecificDates ??= [];
            window.ExactTimes ??= [];
        }
    }

    private static void AddPreviewWarnings(TaskExchangeTask task, List<string> warnings)
    {
        if (task.HasPassword)
        {
            AddWarning(warnings, Strings.Format("Exchange_WarningPassword", task.Name));
        }

        if (!string.IsNullOrWhiteSpace(task.ProgramPath) && !File.Exists(task.ProgramPath))
        {
            AddWarning(
                warnings,
                Strings.Format("Exchange_WarningProgramMissing", task.Name, task.ProgramPath));
        }

        if (!string.IsNullOrWhiteSpace(task.WorkingDirectory) && !Directory.Exists(task.WorkingDirectory))
        {
            AddWarning(
                warnings,
                Strings.Format("Exchange_WarningWorkingDirectoryMissing", task.Name, task.WorkingDirectory));
        }
    }

    private static bool HasForbiddenFields(JsonElement root)
    {
        if (ContainsForbiddenField(root))
        {
            return true;
        }

        if (root.TryGetProperty("tasks", out var tasks) && tasks.ValueKind == JsonValueKind.Array)
        {
            foreach (var task in tasks.EnumerateArray())
            {
                if (ContainsForbiddenField(task))
                {
                    return true;
                }
            }
        }

        return false;
    }

    private static bool ContainsForbiddenField(JsonElement element)
    {
        if (element.ValueKind != JsonValueKind.Object)
        {
            return false;
        }

        foreach (var property in element.EnumerateObject())
        {
            if (ForbiddenFields.Contains(property.Name))
            {
                return true;
            }
        }

        return false;
    }

    private static void AddResult(
        TaskImportReport report,
        TaskImportItemResult item,
        IProgress<TaskImportItemResult>? progress)
    {
        report.Items.Add(item);

        switch (item.Outcome)
        {
            case TaskImportOutcome.Imported:
                report.Imported++;
                break;
            case TaskImportOutcome.Skipped:
                report.Skipped++;
                break;
            case TaskImportOutcome.Updated:
                report.Updated++;
                break;
            case TaskImportOutcome.Failed:
                report.Failed++;
                break;
        }

        progress?.Report(item);
    }

    private static void AddWarning(List<string> warnings, string warning)
    {
        if (!warnings.Contains(warning, StringComparer.Ordinal))
        {
            warnings.Add(warning);
        }
    }

    private static string ErrorCodeOf(IpcException exception)
    {
        if (!string.IsNullOrWhiteSpace(exception.RawCode))
        {
            return exception.RawCode;
        }

        return exception.Code switch
        {
            IpcErrorCode.AuthDenied => "AUTH_DENIED",
            IpcErrorCode.NotFound => "NOT_FOUND",
            IpcErrorCode.Validation => "VALIDATION",
            IpcErrorCode.Conflict => "CONFLICT",
            IpcErrorCode.TooLarge => "TOO_LARGE",
            IpcErrorCode.UnsupportedVersion => "UNSUPPORTED_VERSION",
            IpcErrorCode.Database => "DB",
            IpcErrorCode.Unavailable => "UNAVAILABLE",
            IpcErrorCode.Timeout => "INTERNAL",
            _ => "INTERNAL",
        };
    }

    private enum ImportOperationKind
    {
        Create,
        Skip,
        Update,
    }

    private readonly record struct ImportOperation(ImportOperationKind Kind, Guid Id);
}
