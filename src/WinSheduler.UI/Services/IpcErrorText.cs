using WinSheduler.UI.Localization;

namespace WinSheduler.UI.Services;

/// <summary>
/// Русский текст ошибки IPC. Контракт UI.md, раздел 6: текст выбирается по
/// <c>code</c>, сообщение службы — fallback для неизвестных кодов.
/// </summary>
public static class IpcErrorText
{
    /// <summary>Короткий текст по коду ошибки.</summary>
    public static string Describe(IpcException exception)
    {
        var key = exception.Code switch
        {
            IpcErrorCode.AuthDenied => "Error_AuthDenied",
            IpcErrorCode.NotFound => "Error_NotFound",
            IpcErrorCode.Validation => "Error_Validation",
            IpcErrorCode.Conflict => "Error_Conflict",
            IpcErrorCode.TooLarge => "Error_TooLarge",
            IpcErrorCode.UnsupportedVersion => "Error_UnsupportedVersion",
            IpcErrorCode.Database => "Error_Database",
            IpcErrorCode.Internal => "Error_Internal",
            IpcErrorCode.Unavailable => "Error_Unavailable",
            IpcErrorCode.Timeout => "Error_Timeout",
            _ => null,
        };

        if (key is not null)
        {
            return Strings.Get(key);
        }

        return string.IsNullOrWhiteSpace(exception.Message)
            ? Strings.Get("Error_Unknown")
            : exception.Message;
    }

    /// <summary>Текст по коду и, если он отличается, уточнение службы.</summary>
    public static string DescribeDetailed(IpcException exception)
    {
        var text = Describe(exception);
        var detail = exception.Message;

        return string.IsNullOrWhiteSpace(detail) || string.Equals(detail, text, StringComparison.Ordinal)
            ? text
            : $"{text} {detail}";
    }

    /// <summary>Текст для любого исключения.</summary>
    public static string Describe(Exception exception)
        => exception is IpcException ipc
            ? Describe(ipc)
            : string.IsNullOrWhiteSpace(exception.Message)
                ? Strings.Get("Error_Unknown")
                : exception.Message;
}
