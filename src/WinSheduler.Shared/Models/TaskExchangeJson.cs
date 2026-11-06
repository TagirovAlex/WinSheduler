using System.Globalization;
using System.Text.Json;
using System.Text.Json.Serialization;

namespace WinSheduler.Shared.Models;

/// <summary>
/// Настройки и лимиты формата обмена задачами (контракт EXCHANGE.md).
/// </summary>
public static class TaskExchangeJson
{
    /// <summary>Значение поля <c>format</c>.</summary>
    public const string FormatName = "winsheduler.tasks";

    /// <summary>Поддерживаемая версия формата.</summary>
    public const int CurrentVersion = 1;

    /// <summary>Максимальный размер файла — 8 МиБ.</summary>
    public const long MaxFileBytes = 8L * 1024 * 1024;

    /// <summary>Максимальное число задач в файле.</summary>
    public const int MaxTasks = 5000;

    /// <summary>Максимальное число триггеров на задачу.</summary>
    public const int MaxTriggersPerTask = 1000;

    /// <summary>Максимальная длина строки — как лимит IPC (64 КиБ).</summary>
    public const int MaxStringLength = 64 * 1024;

    /// <summary>
    /// Сериализация обмена: camelCase, перечисления строками, отступы,
    /// null-поля не пишутся. Неизвестные поля при чтении игнорируются.
    /// </summary>
    public static JsonSerializerOptions Options { get; } = new()
    {
        PropertyNamingPolicy = JsonNamingPolicy.CamelCase,
        PropertyNameCaseInsensitive = true,
        WriteIndented = true,
        DefaultIgnoreCondition = JsonIgnoreCondition.WhenWritingNull,
        Converters = { new JsonStringEnumConverter(), new TaskExchangeDateConverter() },
    };

    /// <summary>Создаёт документ с обязательными полями шапки и текущим временем UTC.</summary>
    public static TaskExchangeDocument CreateDocument(string appVersion, IEnumerable<TaskExchangeTask> tasks)
    {
        ArgumentNullException.ThrowIfNull(tasks);

        return new TaskExchangeDocument
        {
            Format = FormatName,
            FormatVersion = CurrentVersion,
            ExportedAt = DateTime.UtcNow.ToString("yyyy-MM-ddTHH:mm:ss'Z'", CultureInfo.InvariantCulture),
            AppVersion = appVersion ?? "",
            Tasks = [.. tasks],
        };
    }
}

/// <summary>
/// Даты в обмене: только дата — <c>yyyy-MM-dd</c>, иначе ISO 8601.
/// Соответствует представлению <c>TimeWindowDto</c> в контракте IPC.md.
/// </summary>
internal sealed class TaskExchangeDateConverter : JsonConverter<DateTime>
{
    private static readonly string[] ReadFormats =
    [
        "yyyy-MM-dd",
        "yyyy-MM-ddTHH:mm",
        "yyyy-MM-ddTHH:mm:ss",
        "yyyy-MM-ddTHH:mm:ssZ",
        "yyyy-MM-ddTHH:mm:ss.FFFFFFFK",
    ];

    public override DateTime Read(ref Utf8JsonReader reader, Type typeToConvert, JsonSerializerOptions options)
    {
        var text = reader.GetString();
        if (string.IsNullOrWhiteSpace(text))
        {
            return default;
        }

        if (DateTime.TryParseExact(
                text,
                ReadFormats,
                CultureInfo.InvariantCulture,
                DateTimeStyles.RoundtripKind,
                out var exact))
        {
            return exact;
        }

        return DateTime.TryParse(
            text,
            CultureInfo.InvariantCulture,
            DateTimeStyles.RoundtripKind,
            out var parsed)
            ? parsed
            : default;
    }

    public override void Write(Utf8JsonWriter writer, DateTime value, JsonSerializerOptions options)
    {
        if (value.TimeOfDay == TimeSpan.Zero)
        {
            writer.WriteStringValue(value.ToString("yyyy-MM-dd", CultureInfo.InvariantCulture));
            return;
        }

        var format = value.Kind == DateTimeKind.Utc
            ? "yyyy-MM-ddTHH:mm:ss'Z'"
            : "yyyy-MM-ddTHH:mm:ss";

        writer.WriteStringValue(value.ToString(format, CultureInfo.InvariantCulture));
    }
}
