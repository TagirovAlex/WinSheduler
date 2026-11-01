namespace WinSheduler.UI.Services;

/// <summary>Типизированные коды ошибок IPC (контракт IPC.md, раздел 6).</summary>
public enum IpcErrorCode
{
    /// <summary>Код не распознан или ответ не соответствует протоколу.</summary>
    Unknown,

    /// <summary>Нет прав на действие (<c>AUTH_DENIED</c>).</summary>
    AuthDenied,

    /// <summary>Объект не найден (<c>NOT_FOUND</c>).</summary>
    NotFound,

    /// <summary>Невалидные поля (<c>VALIDATION</c>).</summary>
    Validation,

    /// <summary>Дубль, гонка или занято (<c>CONFLICT</c>).</summary>
    Conflict,

    /// <summary>Превышен размер кадра (<c>TOO_LARGE</c>).</summary>
    TooLarge,

    /// <summary>Неизвестная версия протокола (<c>UNSUPPORTED_VERSION</c>).</summary>
    UnsupportedVersion,

    /// <summary>Ошибка базы данных (<c>DB</c>).</summary>
    Database,

    /// <summary>Прочая ошибка службы (<c>INTERNAL</c>).</summary>
    Internal,

    /// <summary>Служба недоступна: канал не открывается или оборван.</summary>
    Unavailable,

    /// <summary>Превышено время ожидания ответа.</summary>
    Timeout,
}

/// <summary>
/// Ошибка обмена со службой. <see cref="Exception.Message"/> содержит текст службы
/// (fallback), для показа пользователю используйте <see cref="IpcErrorText.Describe"/>.
/// </summary>
public class IpcException : Exception
{
    public IpcException(
        IpcErrorCode code,
        string? rawCode,
        string message,
        bool isTransient = false,
        Exception? innerException = null)
        : base(message, innerException)
    {
        Code = code;
        RawCode = rawCode ?? string.Empty;
        IsTransient = isTransient;
    }

    /// <summary>Типизированный код ошибки.</summary>
    public IpcErrorCode Code { get; }

    /// <summary>Строковый код из контракта, например <c>AUTH_DENIED</c>.</summary>
    public string RawCode { get; }

    /// <summary>Запрос можно безопасно повторить.</summary>
    public bool IsTransient { get; }

    /// <summary>Сопоставляет строковый код контракта с типизированным значением.</summary>
    public static IpcErrorCode MapCode(string? rawCode) => rawCode switch
    {
        "AUTH_DENIED" => IpcErrorCode.AuthDenied,
        "NOT_FOUND" => IpcErrorCode.NotFound,
        "VALIDATION" => IpcErrorCode.Validation,
        "CONFLICT" => IpcErrorCode.Conflict,
        "TOO_LARGE" => IpcErrorCode.TooLarge,
        "UNSUPPORTED_VERSION" => IpcErrorCode.UnsupportedVersion,
        "DB" => IpcErrorCode.Database,
        "INTERNAL" => IpcErrorCode.Internal,
        _ => IpcErrorCode.Unknown,
    };
}

/// <summary>Служба недоступна: канал не открылся, соединение оборвано или служба не запущена.</summary>
public sealed class IpcUnavailableException : IpcException
{
    public IpcUnavailableException(string message, Exception? innerException = null)
        : base(IpcErrorCode.Unavailable, null, message, isTransient: true, innerException)
    {
    }
}
