using System.Windows.Input;

namespace WinSheduler.UI.ViewModels;

/// <summary>Синхронная команда с явным управлением доступностью.</summary>
public sealed class RelayCommand : ICommand
{
    private readonly Action _execute;
    private readonly Func<bool>? _canExecute;

    public RelayCommand(Action execute, Func<bool>? canExecute = null)
    {
        _execute = execute ?? throw new ArgumentNullException(nameof(execute));
        _canExecute = canExecute;
    }

    /// <inheritdoc />
    public event EventHandler? CanExecuteChanged;

    /// <inheritdoc />
    public bool CanExecute(object? parameter) => _canExecute?.Invoke() ?? true;

    /// <inheritdoc />
    public void Execute(object? parameter) => _execute();

    /// <summary>Пересчитывает доступность команды в UI.</summary>
    public void RaiseCanExecuteChanged() => CanExecuteChanged?.Invoke(this, EventArgs.Empty);
}

/// <summary>Синхронная команда с параметром.</summary>
public sealed class RelayCommand<T> : ICommand
{
    private readonly Action<T?> _execute;
    private readonly Func<T?, bool>? _canExecute;

    public RelayCommand(Action<T?> execute, Func<T?, bool>? canExecute = null)
    {
        _execute = execute ?? throw new ArgumentNullException(nameof(execute));
        _canExecute = canExecute;
    }

    /// <inheritdoc />
    public event EventHandler? CanExecuteChanged;

    /// <inheritdoc />
    public bool CanExecute(object? parameter) => _canExecute?.Invoke(Cast(parameter)) ?? true;

    /// <inheritdoc />
    public void Execute(object? parameter) => _execute(Cast(parameter));

    /// <summary>Пересчитывает доступность команды в UI.</summary>
    public void RaiseCanExecuteChanged() => CanExecuteChanged?.Invoke(this, EventArgs.Empty);

    private static T? Cast(object? parameter) => parameter is T value ? value : default;
}

/// <summary>
/// Асинхронная команда. Защищена от повторного входа: пока выполняется
/// предыдущий запуск, команда недоступна. Поддерживает отмену.
/// </summary>
public sealed class AsyncCommand : ICommand
{
    private readonly Func<CancellationToken, Task> _execute;
    private readonly Func<bool>? _canExecute;
    private readonly Action<Exception>? _onError;
    private int _isRunning;
    private CancellationTokenSource? _cts;

    public AsyncCommand(
        Func<CancellationToken, Task> execute,
        Func<bool>? canExecute = null,
        Action<Exception>? onError = null)
    {
        _execute = execute ?? throw new ArgumentNullException(nameof(execute));
        _canExecute = canExecute;
        _onError = onError;
    }

    public AsyncCommand(Func<Task> execute, Func<bool>? canExecute = null, Action<Exception>? onError = null)
        : this(_ => execute(), canExecute, onError)
    {
    }

    /// <inheritdoc />
    public event EventHandler? CanExecuteChanged;

    /// <summary>Идёт выполнение команды.</summary>
    public bool IsRunning => Volatile.Read(ref _isRunning) == 1;

    /// <inheritdoc />
    public bool CanExecute(object? parameter) => !IsRunning && (_canExecute?.Invoke() ?? true);

    /// <inheritdoc />
    public void Execute(object? parameter) => _ = ExecuteAsync();

    /// <summary>
    /// Выполняет команду. Возвращает задачу, которую можно дождаться в тестах.
    /// Повторный вызов во время выполнения игнорируется.
    /// </summary>
    public async Task ExecuteAsync()
    {
        if (!CanExecute(null))
        {
            return;
        }

        if (Interlocked.CompareExchange(ref _isRunning, 1, 0) != 0)
        {
            return;
        }

        using var cts = new CancellationTokenSource();
        _cts = cts;
        RaiseCanExecuteChanged();

        try
        {
            await _execute(cts.Token).ConfigureAwait(true);
        }
        catch (OperationCanceledException) when (cts.IsCancellationRequested)
        {
            // Отмена — штатный сценарий, ошибку не показываем.
        }
        catch (Exception ex)
        {
            _onError?.Invoke(ex);
        }
        finally
        {
            _cts = null;
            Volatile.Write(ref _isRunning, 0);
            RaiseCanExecuteChanged();
        }
    }

    /// <summary>Отменяет текущее выполнение, если оно есть.</summary>
    public void Cancel() => _cts?.Cancel();

    /// <summary>Пересчитывает доступность команды в UI.</summary>
    public void RaiseCanExecuteChanged() => CanExecuteChanged?.Invoke(this, EventArgs.Empty);
}

/// <summary>Асинхронная команда с параметром.</summary>
public sealed class AsyncCommand<T> : ICommand
{
    private readonly Func<T?, CancellationToken, Task> _execute;
    private readonly Func<T?, bool>? _canExecute;
    private readonly Action<Exception>? _onError;
    private int _isRunning;
    private CancellationTokenSource? _cts;

    public AsyncCommand(
        Func<T?, CancellationToken, Task> execute,
        Func<T?, bool>? canExecute = null,
        Action<Exception>? onError = null)
    {
        _execute = execute ?? throw new ArgumentNullException(nameof(execute));
        _canExecute = canExecute;
        _onError = onError;
    }

    /// <inheritdoc />
    public event EventHandler? CanExecuteChanged;

    /// <summary>Идёт выполнение команды.</summary>
    public bool IsRunning => Volatile.Read(ref _isRunning) == 1;

    /// <inheritdoc />
    public bool CanExecute(object? parameter) => !IsRunning && (_canExecute?.Invoke(Cast(parameter)) ?? true);

    /// <inheritdoc />
    public void Execute(object? parameter) => _ = ExecuteAsync(Cast(parameter));

    /// <summary>Выполняет команду с параметром; повторный вход игнорируется.</summary>
    public async Task ExecuteAsync(T? parameter)
    {
        if (!CanExecute(parameter))
        {
            return;
        }

        if (Interlocked.CompareExchange(ref _isRunning, 1, 0) != 0)
        {
            return;
        }

        using var cts = new CancellationTokenSource();
        _cts = cts;
        RaiseCanExecuteChanged();

        try
        {
            await _execute(parameter, cts.Token).ConfigureAwait(true);
        }
        catch (OperationCanceledException) when (cts.IsCancellationRequested)
        {
        }
        catch (Exception ex)
        {
            _onError?.Invoke(ex);
        }
        finally
        {
            _cts = null;
            Volatile.Write(ref _isRunning, 0);
            RaiseCanExecuteChanged();
        }
    }

    /// <summary>Отменяет текущее выполнение, если оно есть.</summary>
    public void Cancel() => _cts?.Cancel();

    /// <summary>Пересчитывает доступность команды в UI.</summary>
    public void RaiseCanExecuteChanged() => CanExecuteChanged?.Invoke(this, EventArgs.Empty);

    private static T? Cast(object? parameter) => parameter is T value ? value : default;
}
