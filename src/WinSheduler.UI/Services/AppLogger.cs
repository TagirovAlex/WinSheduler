using System.Globalization;
using System.IO;
using System.Text;

namespace WinSheduler.UI.Services;

/// <summary>Уровень записи в журнал UI.</summary>
public enum LogLevel
{
    Debug,
    Info,
    Warning,
    Error,
}

/// <summary>
/// Простой файловый журнал UI. Каталог — <c>%LOCALAPPDATA%/WinSheduler/logs</c>:
/// у обычного пользователя нет доступа к <c>DATADIR</c> службы, а писать в
/// ProgramData UI не должен.
///
/// Формат строки: <c>время | уровень | компонент | сообщение</c>.
/// Переводы строк из сообщений заменяются пробелом (защита от log injection).
/// Секреты в журнал не попадают — это ответственность вызывающего кода.
/// </summary>
public sealed class AppLogger
{
    private const long DefaultMaxBytes = 5 * 1024 * 1024;
    private const int DefaultMaxFiles = 3;

    private static readonly object SyncRoot = new();

    private readonly long _maxBytes;
    private readonly int _maxFiles;

    public AppLogger(string? directory = null, long maxBytes = DefaultMaxBytes, int maxFiles = DefaultMaxFiles)
    {
        DirectoryPath = directory ?? Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
            "WinSheduler",
            "logs");

        _maxBytes = maxBytes;
        _maxFiles = Math.Max(1, maxFiles);

        try
        {
            Directory.CreateDirectory(DirectoryPath);
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
        {
            // Журнал недоступен — приложение продолжает работу без него.
            IsEnabled = false;
        }
    }

    /// <summary>Каталог журналов.</summary>
    public string DirectoryPath { get; }

    /// <summary>Запись в журнал возможна.</summary>
    public bool IsEnabled { get; private set; } = true;

    /// <summary>Путь к текущему файлу журнала.</summary>
    public string CurrentFilePath => Path.Combine(
        DirectoryPath,
        $"ui-{DateTime.Now.ToString("yyyyMMdd", CultureInfo.InvariantCulture)}.log");

    public void Debug(string component, string message) => Write(LogLevel.Debug, component, message);

    public void Info(string component, string message) => Write(LogLevel.Info, component, message);

    public void Warning(string component, string message) => Write(LogLevel.Warning, component, message);

    public void Error(string component, string message, Exception? exception = null)
        => Write(LogLevel.Error, component, exception is null ? message : $"{message}: {exception.GetType().Name}: {exception.Message}");

    /// <summary>Записывает строку. Ошибки ввода-вывода не пробрасываются наружу.</summary>
    public void Write(LogLevel level, string component, string message)
    {
        if (!IsEnabled)
        {
            return;
        }

        var line = string.Format(
            CultureInfo.InvariantCulture,
            "{0:yyyy-MM-dd HH:mm:ss.fff} | {1,-7} | {2} | {3}{4}",
            DateTime.Now,
            level.ToString().ToUpperInvariant(),
            Sanitize(component),
            Sanitize(message),
            Environment.NewLine);

        lock (SyncRoot)
        {
            try
            {
                RotateIfNeeded();
                File.AppendAllText(CurrentFilePath, line, Encoding.UTF8);
            }
            catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
            {
                IsEnabled = false;
            }
        }
    }

    private void RotateIfNeeded()
    {
        var path = CurrentFilePath;
        if (!File.Exists(path) || new FileInfo(path).Length < _maxBytes)
        {
            return;
        }

        // Сдвигаем архивные файлы: удаляем самый старый, затем .2 → .3, .1 → .2,
        // текущий → .1.
        var oldest = $"{path}.{_maxFiles}";
        if (File.Exists(oldest))
        {
            File.Delete(oldest);
        }

        for (var index = _maxFiles - 1; index >= 1; index--)
        {
            var from = $"{path}.{index}";
            if (File.Exists(from))
            {
                File.Move(from, $"{path}.{index + 1}", overwrite: true);
            }
        }

        File.Move(path, $"{path}.1", overwrite: true);
    }

    private static string Sanitize(string? value)
        => string.IsNullOrEmpty(value)
            ? string.Empty
            : value.Replace('\r', ' ').Replace('\n', ' ');
}
