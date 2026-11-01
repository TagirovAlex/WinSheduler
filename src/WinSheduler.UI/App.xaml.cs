using System.Threading;
using System.Windows;
using System.Windows.Threading;
using WinSheduler.UI.Localization;
using WinSheduler.UI.Services;

namespace WinSheduler.UI;

/// <summary>
/// Точка входа UI: единственный экземпляр, глобальные обработчики исключений,
/// журнал, тема и DPI (манифест PerMonitorV2). Экраны создаются здесь,
/// чтобы Track E мог менять окна, не трогая хост.
/// </summary>
public partial class App : Application
{
    private const string SingleInstanceMutexName = @"Local\WinSheduler.UI.SingleInstance";
    private const string ActivateEventName = @"Local\WinSheduler.UI.Activate";

    private Mutex? _instanceMutex;
    private EventWaitHandle? _activateEvent;
    private CancellationTokenSource? _activationCts;

    /// <summary>Сервисы приложения. Доступны после <see cref="OnStartup"/>.</summary>
    public static AppServices Services { get; private set; } = null!;

    private bool _servicesReady;

    protected override void OnStartup(StartupEventArgs e)
    {
        base.OnStartup(e);

        Services = new AppServices();
        _servicesReady = true;

        Services.Logger.Info(
            "App",
            $"Запуск UI {typeof(App).Assembly.GetName().Version}; журнал: {Services.Logger.DirectoryPath}");

        Services.Theme.Apply(ParseTheme(Services.WindowState.State.Theme));

        if (!TakeSingleInstance())
        {
            // Второй экземпляр: показать уже открытое окно и выйти.
            SignalExistingInstance();
            Shutdown();
            return;
        }

        RegisterExceptionHandlers();
        StartActivationLoop();

        var window = new MainWindow();
        MainWindow = window;
        Services.WindowState.Attach(window, 1024, 700);
        window.Show();
    }

    protected override void OnExit(ExitEventArgs e)
    {
        if (_servicesReady)
        {
            Services.Logger.Info("App", "Завершение UI");
        }

        _activationCts?.Cancel();
        _activateEvent?.Set();
        _activateEvent?.Dispose();

        if (_instanceMutex is not null)
        {
            try
            {
                _instanceMutex.ReleaseMutex();
            }
            catch (ApplicationException)
            {
                // Мьютекс уже не принадлежит процессу.
            }

            _instanceMutex.Dispose();
        }

        if (_servicesReady)
        {
            Services.DisposeAsync().AsTask().GetAwaiter().GetResult();
        }

        base.OnExit(e);
    }

    private bool TakeSingleInstance()
    {
        _instanceMutex = new Mutex(initiallyOwned: true, SingleInstanceMutexName, out var createdNew);
        return createdNew;
    }

    private void SignalExistingInstance()
    {
        try
        {
            using var handle = EventWaitHandle.OpenExisting(ActivateEventName);
            handle.Set();
        }
        catch (WaitHandleCannotBeOpenedException)
        {
            // Первый экземпляр ещё не создал событие — просто выходим.
        }
    }

    private void StartActivationLoop()
    {
        _activateEvent = new EventWaitHandle(false, EventResetMode.AutoReset, ActivateEventName);
        _activationCts = new CancellationTokenSource();
        var token = _activationCts.Token;

        _ = Task.Run(
            () =>
            {
                var handles = new WaitHandle[] { _activateEvent, token.WaitHandle };

                while (!token.IsCancellationRequested)
                {
                    var index = WaitHandle.WaitAny(handles);
                    if (index != 0)
                    {
                        break;
                    }

                    Dispatcher.BeginInvoke(new Action(BringToFront));
                }
            },
            token);
    }

    private void BringToFront()
    {
        if (MainWindow is not { } window)
        {
            return;
        }

        if (window.WindowState == WindowState.Minimized)
        {
            window.WindowState = WindowState.Normal;
        }

        window.Show();
        window.Activate();
        window.Topmost = true;
        window.Topmost = false;
    }

    private void RegisterExceptionHandlers()
    {
        DispatcherUnhandledException += OnDispatcherUnhandledException;
        AppDomain.CurrentDomain.UnhandledException += OnDomainUnhandledException;
        TaskScheduler.UnobservedTaskException += OnUnobservedTaskException;
    }

    private void OnDispatcherUnhandledException(object sender, DispatcherUnhandledExceptionEventArgs e)
    {
        Services.Logger.Error("Dispatcher", "Необработанное исключение", e.Exception);

        if (e.Exception is OutOfMemoryException or StackOverflowException)
        {
            // Такие ошибки нельзя безопасно продолжить.
            e.Handled = false;
            return;
        }

        e.Handled = true;

        try
        {
            Services.Dialogs.ShowError(Strings.Get("Error_Unexpected"));
        }
        catch (Exception dialogError)
        {
            Services.Logger.Error("Dispatcher", "Не удалось показать сообщение об ошибке", dialogError);
        }
    }

    private void OnDomainUnhandledException(object sender, UnhandledExceptionEventArgs e)
    {
        var exception = e.ExceptionObject as Exception;
        Services.Logger.Error(
            "AppDomain",
            $"Необработанное исключение, завершение процесса: {e.IsTerminating}",
            exception);
    }

    private void OnUnobservedTaskException(object? sender, UnobservedTaskExceptionEventArgs e)
    {
        Services.Logger.Error("Task", "Необработанное исключение задачи", e.Exception);
        e.SetObserved();
    }

    private static ThemeMode ParseTheme(string? value)
        => Enum.TryParse<ThemeMode>(value, ignoreCase: true, out var mode) ? mode : ThemeMode.System;
}
