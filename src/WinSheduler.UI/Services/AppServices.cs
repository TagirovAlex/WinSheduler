using WinSheduler.UI.ViewModels;

namespace WinSheduler.UI.Services;

/// <summary>
/// Композиционный корень UI: один экземпляр клиента IPC, диалогов, темы,
/// состояния прав и настроек окна. Создаётся в <see cref="App.OnStartup"/>.
/// </summary>
public sealed class AppServices : IAsyncDisposable
{
    public AppServices()
    {
        Logger = new AppLogger();
        Theme = new ThemeService();
        Dialogs = new DialogService();
        Elevation = new ElevationService();
        Ipc = new IpcService();
        WindowState = new WindowStateService();
        Updates = new UpdateService();
    }

    /// <summary>Файловый журнал UI.</summary>
    public AppLogger Logger { get; }

    /// <summary>Светлая и тёмная тема.</summary>
    public ThemeService Theme { get; }

    /// <summary>Диалоги представления.</summary>
    public IDialogService Dialogs { get; }

    /// <summary>Права пользователя и перезапуск с повышением.</summary>
    public IElevationService Elevation { get; }

    /// <summary>Клиент службы.</summary>
    public IIpcService Ipc { get; }

    /// <summary>Размер, позиция окна и последний выбор.</summary>
    public WindowStateService WindowState { get; }

    /// <summary>Проверка обновлений по адресу, заданному при сборке.</summary>
    public UpdateService Updates { get; }

    /// <summary>Создаёт ViewModel главного экрана с общими сервисами.</summary>
    public DashboardViewModel CreateDashboardViewModel()
        => new(Ipc, Elevation, WindowState);

    /// <summary>Применяет и запоминает тему оформления.</summary>
    public void SetTheme(ThemeMode mode)
    {
        Theme.Apply(mode);
        WindowState.State.Theme = mode.ToString();
        WindowState.Save();
    }

    /// <inheritdoc />
    public ValueTask DisposeAsync() => Ipc.DisposeAsync();
}
