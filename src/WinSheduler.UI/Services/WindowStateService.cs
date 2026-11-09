using System.IO;
using System.Text.Json;
using System.Windows;

namespace WinSheduler.UI.Services;

/// <summary>Сохраняемое состояние UI.</summary>
public sealed class UiState
{
    public double? Width { get; set; }
    public double? Height { get; set; }
    public double? Left { get; set; }
    public double? Top { get; set; }
    public bool Maximized { get; set; }

    /// <summary>Идентификатор последней выбранной задачи.</summary>
    public Guid? LastSelectedTaskId { get; set; }

    /// <summary>Режим темы: <c>System</c>, <c>Light</c>, <c>Dark</c>.</summary>
    public string? Theme { get; set; }

    /// <summary>Время последней успешной проверки обновлений (UTC).</summary>
    public DateTimeOffset? LastUpdateCheckUtc { get; set; }

    /// <summary>Версия, установку которой нужно продолжить после перезапуска с правами.</summary>
    public string? PendingUpdateVersion { get; set; }
}

/// <summary>
/// Сохранение размера, позиции окна и последнего выбора в
/// <c>%LOCALAPPDATA%/WinSheduler/ui-state.json</c>. Файл принадлежит пользователю,
/// секретов не содержит. Ошибки чтения и записи не ломают работу UI.
/// </summary>
public sealed class WindowStateService
{
    private const double MinVisiblePart = 100;

    private readonly string _path;
    private readonly object _sync = new();
    private Window? _window;

    public WindowStateService(string? path = null)
    {
        _path = path ?? Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
            "WinSheduler",
            "ui-state.json");

        State = Load();
    }

    /// <summary>Текущее состояние.</summary>
    public UiState State { get; private set; }

    /// <summary>Подключает окно: восстанавливает геометрию и сохраняет её при закрытии.</summary>
    public void Attach(Window window, double minWidth, double minHeight)
    {
        _window = window ?? throw new ArgumentNullException(nameof(window));

        if (minWidth > 0)
        {
            window.MinWidth = Math.Max(window.MinWidth, minWidth);
        }

        if (minHeight > 0)
        {
            window.MinHeight = Math.Max(window.MinHeight, minHeight);
        }

        Restore(window);
        window.Closing += (_, _) => Save();
    }

    /// <summary>Восстанавливает геометрию, если она попадает в видимую область экрана.</summary>
    public void Restore(Window window)
    {
        var state = State;

        if (state.Width is > 0 and var width && state.Height is > 0 and var height)
        {
            window.Width = Math.Max(width, window.MinWidth);
            window.Height = Math.Max(height, window.MinHeight);
        }

        if (state.Left is { } left && state.Top is { } top && IsOnScreen(left, top, window.Width, window.Height))
        {
            window.WindowStartupLocation = WindowStartupLocation.Manual;
            window.Left = left;
            window.Top = top;
        }

        if (state.Maximized)
        {
            window.WindowState = WindowState.Maximized;
        }
    }

    /// <summary>Сохраняет текущую геометрию окна и последний выбор.</summary>
    public void Save()
    {
        var window = _window;
        if (window is not null)
        {
            var bounds = window.WindowState == WindowState.Normal
                ? new Rect(window.Left, window.Top, window.Width, window.Height)
                : window.RestoreBounds;

            State.Width = bounds.Width;
            State.Height = bounds.Height;
            State.Left = bounds.Left;
            State.Top = bounds.Top;
            State.Maximized = window.WindowState == WindowState.Maximized;
        }

        Persist();
    }

    /// <summary>Запоминает последнюю выбранную задачу и сразу сохраняет состояние.</summary>
    public void RememberSelection(Guid? taskId)
    {
        State.LastSelectedTaskId = taskId;
        Persist();
    }

    /// <summary>Запоминает время успешной проверки обновлений.</summary>
    public void RememberUpdateCheck(DateTimeOffset checkedAt)
    {
        State.LastUpdateCheckUtc = checkedAt;
        Persist();
    }

    /// <summary>Запоминает версию для продолжения установки после перезапуска.</summary>
    public void RememberPendingUpdate(string version)
    {
        State.PendingUpdateVersion = version;
        Persist();
    }

    /// <summary>Сбрасывает отложенную установку.</summary>
    public void ClearPendingUpdate()
    {
        if (State.PendingUpdateVersion is null)
        {
            return;
        }

        State.PendingUpdateVersion = null;
        Persist();
    }

    private UiState Load()
    {
        try
        {
            if (!File.Exists(_path))
            {
                return new UiState();
            }

            var json = File.ReadAllText(_path);
            return JsonSerializer.Deserialize<UiState>(json) ?? new UiState();
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException or JsonException)
        {
            return new UiState();
        }
    }

    private void Persist()
    {
        lock (_sync)
        {
            try
            {
                var directory = Path.GetDirectoryName(_path);
                if (!string.IsNullOrEmpty(directory))
                {
                    Directory.CreateDirectory(directory);
                }

                File.WriteAllText(
                    _path,
                    JsonSerializer.Serialize(State, new JsonSerializerOptions { WriteIndented = true }));
            }
            catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
            {
                // Состояние — вспомогательные данные: сбой записи не критичен.
            }
        }
    }

    private static bool IsOnScreen(double left, double top, double width, double height)
    {
        var virtualScreen = new Rect(
            SystemParameters.VirtualScreenLeft,
            SystemParameters.VirtualScreenTop,
            SystemParameters.VirtualScreenWidth,
            SystemParameters.VirtualScreenHeight);

        var windowRect = new Rect(left, top, Math.Max(width, MinVisiblePart), Math.Max(height, MinVisiblePart));
        var intersection = Rect.Intersect(virtualScreen, windowRect);

        return !intersection.IsEmpty &&
               intersection.Width >= MinVisiblePart &&
               intersection.Height >= MinVisiblePart;
    }
}
