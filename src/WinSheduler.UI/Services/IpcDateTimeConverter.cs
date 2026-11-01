using System.Text.Json;
using System.Text.Json.Serialization;

namespace WinSheduler.UI.Services;

/// <summary>
/// Формат даты в IPC: только дата — <c>yyyy-MM-dd</c>, дата и время — ISO 8601
/// (контракт IPC.md, <c>TimeWindowDto</c>). Чтение принимает оба варианта,
/// запись выбирает вид по значению: полночь — дата без времени.
/// </summary>
internal sealed class IpcDateTimeConverter : JsonConverter<DateTime>
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
                System.Globalization.CultureInfo.InvariantCulture,
                System.Globalization.DateTimeStyles.RoundtripKind,
                out var exact))
        {
            return exact;
        }

        return DateTime.TryParse(
            text,
            System.Globalization.CultureInfo.InvariantCulture,
            System.Globalization.DateTimeStyles.RoundtripKind,
            out var parsed)
            ? parsed
            : default;
    }

    public override void Write(Utf8JsonWriter writer, DateTime value, JsonSerializerOptions options)
    {
        if (value.TimeOfDay == TimeSpan.Zero)
        {
            writer.WriteStringValue(value.ToString("yyyy-MM-dd", System.Globalization.CultureInfo.InvariantCulture));
            return;
        }

        var format = value.Kind == DateTimeKind.Utc
            ? "yyyy-MM-ddTHH:mm:ss'Z'"
            : "yyyy-MM-ddTHH:mm:ss";

        writer.WriteStringValue(value.ToString(format, System.Globalization.CultureInfo.InvariantCulture));
    }
}
