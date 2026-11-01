using System.Collections.ObjectModel;
using System.ComponentModel;
using System.Windows.Data;
using WinSheduler.Shared;
using WinSheduler.UI.Localization;
using WinSheduler.UI.Services;

namespace WinSheduler.UI.ViewModels;

/// <summary>Фильтр списка задач (контракт UI.md: «Все / Мои / Включённые / Отключённые»).</summary>
public enum TaskFilter
{
    All,
    Mine,
    Enabled,
    Disabled,
}

/// <summary>
/// Состояние главного списка: batch-обновление одним запросом <c>GetDashboard</c>,
/// прогресс, отмена и защита от повторного входа (задача D-7).
///
/// Список обновляется на месте, без пересоздания <see cref="ObservableCollection"/>.
/// Ошибки показываются через <see cref="ErrorText"/> (баннер), а не модальным окном.
/// </summary>
public sealed class DashboardViewModel : ViewModelBase
{
    private readonly IIpcService _ipc;
    private readonly IElevationService? _elevation;
    private readonly WindowStateService? _windowState;
    private readonly ICollectionView _tasksView;

    private CancellationTokenSource? _refreshCts;
    private int _refreshInProgress;
    private string _searchText = string.Empty;
    private TaskFilter _filter = TaskFilter.All;
    private TaskListItemViewModel? _selectedTask;
    private bool _isLoading;
    private string? _errorText;
    private string? _errorCode;
    private DateTimeOffset? _lastRefreshedAt;
    private ServiceStatus? _status;

    public DashboardViewModel(
        IIpcService ipc,
        IElevationService? elevation = null,
        WindowStateService? windowState = null)
    {
        _ipc = ipc ?? throw new ArgumentNullException(nameof(ipc));
        _elevation = elevation;
        _windowState = windowState;

        _tasksView = CollectionViewSource.GetDefaultView(Tasks);
        _tasksView.Filter = FilterTask;
        _tasksView.SortDescriptions.Add(
            new SortDescription(nameof(TaskListItemViewModel.Name), ListSortDirection.Ascending));

        if (_elevation is not null)
        {
            _elevation.StateChanged += (_, _) => OnPropertiesChanged(
                nameof(IsAdmin),
                nameof(CanManageSystemTasks),
                nameof(RoleText));
        }

        RefreshCommand = new AsyncCommand(
            RefreshAsync,
            () => !IsLoading,
            HandleCommandError);

        CancelRefreshCommand = new RelayCommand(() => _refreshCts?.Cancel(), () => IsLoading);

        RefreshStatusCommand = new AsyncCommand(
            RefreshStatusAsync,
            () => !IsLoading,
            HandleCommandError);
    }

    /// <summary>Все задачи, доступные текущему пользователю.</summary>
    public ObservableCollection<TaskListItemViewModel> Tasks { get; } = [];

    /// <summary>Представление с учётом поиска и фильтра.</summary>
    public ICollectionView TasksView => _tasksView;

    /// <summary>Обновление списка одним batch-запросом.</summary>
    public AsyncCommand RefreshCommand { get; }

    /// <summary>Отмена текущего обновления.</summary>
    public RelayCommand CancelRefreshCommand { get; }

    /// <summary>Обновление статуса службы (роль, счётчики).</summary>
    public AsyncCommand RefreshStatusCommand { get; }

    /// <summary>Строка поиска по названию и пути программы.</summary>
    public string SearchText
    {
        get => _searchText;
        set
        {
            if (SetProperty(ref _searchText, value ?? string.Empty))
            {
                ApplyFilter();
            }
        }
    }

    /// <summary>Текущий фильтр списка.</summary>
    public TaskFilter Filter
    {
        get => _filter;
        set
        {
            if (SetProperty(ref _filter, value))
            {
                ApplyFilter();
            }
        }
    }

    /// <summary>Выбранная задача; выбор запоминается между запусками.</summary>
    public TaskListItemViewModel? SelectedTask
    {
        get => _selectedTask;
        set
        {
            if (SetProperty(ref _selectedTask, value))
            {
                _windowState?.RememberSelection(value?.Id);
            }
        }
    }

    /// <summary>Идёт обновление: кнопки недоступны, виден прогресс-индикатор.</summary>
    public bool IsLoading
    {
        get => _isLoading;
        private set
        {
            if (SetProperty(ref _isLoading, value))
            {
                RefreshCommand.RaiseCanExecuteChanged();
                CancelRefreshCommand.RaiseCanExecuteChanged();
                RefreshStatusCommand.RaiseCanExecuteChanged();
            }
        }
    }

    /// <summary>Текст ошибки для баннера; <c>null</c> — ошибки нет.</summary>
    public string? ErrorText
    {
        get => _errorText;
        private set
        {
            if (SetProperty(ref _errorText, value))
            {
                OnPropertyChanged(nameof(HasError));
            }
        }
    }

    /// <summary>Код ошибки из контракта, например <c>AUTH_DENIED</c>.</summary>
    public string? ErrorCode
    {
        get => _errorCode;
        private set
        {
            if (SetProperty(ref _errorCode, value))
            {
                OnPropertyChanged(nameof(IsServiceUnavailable));
            }
        }
    }

    /// <summary>Есть ошибка для показа в баннере.</summary>
    public bool HasError => !string.IsNullOrEmpty(ErrorText);

    /// <summary>Ошибка связана с недоступностью службы — показывается инструкция запуска.</summary>
    public bool IsServiceUnavailable => _errorCode == nameof(IpcErrorCode.Unavailable);

    /// <summary>Время последнего успешного обновления.</summary>
    public DateTimeOffset? LastRefreshedAt
    {
        get => _lastRefreshedAt;
        private set => SetProperty(ref _lastRefreshedAt, value);
    }

    /// <summary>Всего задач в списке.</summary>
    public int TaskCount => Tasks.Count;

    /// <summary>Задач после фильтрации.</summary>
    public int VisibleTaskCount => _tasksView.Cast<object>().Count();

    /// <summary>Статус службы.</summary>
    public ServiceStatus? Status
    {
        get => _status;
        private set
        {
            if (SetProperty(ref _status, value))
            {
                _elevation?.Update(value);
                OnPropertiesChanged(nameof(IsAdmin), nameof(CanManageSystemTasks), nameof(RoleText));
            }
        }
    }

    /// <summary>Текущий пользователь — администратор.</summary>
    public bool IsAdmin => _elevation?.IsAdmin == true;

    /// <summary>Разрешены привилегированные действия (SYSTEM, RunAs).</summary>
    public bool CanManageSystemTasks => _elevation?.CanManageSystemTasks == true;

    /// <summary>Подпись роли для статус-бара.</summary>
    public string RoleText => _elevation?.RoleText ?? Strings.Get("Status_Operator");

    /// <summary>Обновление завершено (успешно или с ошибкой).</summary>
    public event EventHandler? RefreshCompleted;

    /// <summary>
    /// Обновляет список. Повторный вызов во время выполнения игнорируется,
    /// предыдущий запрос отменяется только через <see cref="CancelRefreshCommand"/>.
    /// </summary>
    public async Task RefreshAsync(CancellationToken cancellationToken = default)
    {
        if (Interlocked.CompareExchange(ref _refreshInProgress, 1, 0) != 0)
        {
            return;
        }

        _refreshCts?.Dispose();
        _refreshCts = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
        var token = _refreshCts.Token;

        IsLoading = true;
        ErrorText = null;
        ErrorCode = null;

        try
        {
            var dashboard = await _ipc.GetDashboardAsync(token).ConfigureAwait(true);
            ApplyDashboard(dashboard);
            LastRefreshedAt = DateTimeOffset.Now;
        }
        catch (OperationCanceledException) when (token.IsCancellationRequested)
        {
            // Отмена — штатный сценарий, баннер не показываем.
        }
        catch (IpcException ex)
        {
            SetError(ex);
        }
        catch (Exception ex)
        {
            SetError(ex);
        }
        finally
        {
            IsLoading = false;
            Volatile.Write(ref _refreshInProgress, 0);
            RefreshCompleted?.Invoke(this, EventArgs.Empty);
        }
    }

    /// <summary>Обновляет статус службы и роль пользователя.</summary>
    public async Task RefreshStatusAsync(CancellationToken cancellationToken = default)
    {
        try
        {
            Status = await _ipc.GetStatusAsync(cancellationToken).ConfigureAwait(true);
        }
        catch (OperationCanceledException) when (cancellationToken.IsCancellationRequested)
        {
        }
        catch (IpcException ex)
        {
            SetError(ex);
        }
        catch (Exception ex)
        {
            SetError(ex);
        }
    }

    /// <summary>Сбрасывает ошибку (например, при повторной попытке).</summary>
    public void ClearError()
    {
        ErrorText = null;
        ErrorCode = null;
    }

    private void ApplyDashboard(DashboardDto dashboard)
    {
        var incoming = dashboard.Tasks.ToDictionary(static task => task.Id);

        // Удаляем задачи, которых больше нет.
        for (var index = Tasks.Count - 1; index >= 0; index--)
        {
            if (!incoming.ContainsKey(Tasks[index].Id))
            {
                Tasks.RemoveAt(index);
            }
        }

        // Обновляем существующие на месте и добавляем новые.
        var existing = Tasks.ToDictionary(static task => task.Id);
        foreach (var task in dashboard.Tasks)
        {
            if (existing.TryGetValue(task.Id, out var item))
            {
                item.Apply(task);
            }
            else
            {
                Tasks.Add(new TaskListItemViewModel(task));
            }
        }

        _tasksView.Refresh();
        OnPropertiesChanged(nameof(TaskCount), nameof(VisibleTaskCount));

        RestoreSelection();
    }

    private void RestoreSelection()
    {
        if (SelectedTask is not null && Tasks.Contains(SelectedTask))
        {
            return;
        }

        var remembered = _windowState?.State.LastSelectedTaskId;
        if (remembered is { } taskId)
        {
            SelectedTask = Tasks.FirstOrDefault(task => task.Id == taskId);
        }
    }

    private void ApplyFilter()
    {
        _tasksView.Refresh();
        OnPropertyChanged(nameof(VisibleTaskCount));
    }

    private bool FilterTask(object item)
    {
        if (item is not TaskListItemViewModel task)
        {
            return false;
        }

        if (Filter == TaskFilter.Mine && !task.IsOwn)
        {
            return false;
        }

        if (Filter == TaskFilter.Enabled && !task.Enabled)
        {
            return false;
        }

        if (Filter == TaskFilter.Disabled && task.Enabled)
        {
            return false;
        }

        var query = SearchText.Trim();
        if (query.Length == 0)
        {
            return true;
        }

        return task.Name.Contains(query, StringComparison.CurrentCultureIgnoreCase)
            || task.ProgramPath.Contains(query, StringComparison.OrdinalIgnoreCase);
    }

    private void SetError(IpcException exception)
    {
        ErrorCode = exception.Code == IpcErrorCode.Unknown ? exception.RawCode : exception.Code.ToString();
        ErrorText = IpcErrorText.Describe(exception);
    }

    private void SetError(Exception exception)
    {
        ErrorCode = null;
        ErrorText = IpcErrorText.Describe(exception);
    }

    private void HandleCommandError(Exception exception)
    {
        if (exception is IpcException ipcException)
        {
            SetError(ipcException);
            return;
        }

        SetError(exception);
    }
}
