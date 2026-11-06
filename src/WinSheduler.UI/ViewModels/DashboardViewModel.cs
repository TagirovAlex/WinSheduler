using System.Collections.ObjectModel;
using System.ComponentModel;
using System.Windows.Data;
using WinSheduler.Shared;
using WinSheduler.Shared.Models;
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
/// Параметры цикла сопровождения запущенных задач (ROADMAP 2.1, раздел 6.2).
/// Значения по умолчанию заданы в конструкторе; тесты подставляют короткие интервалы.
/// </summary>
public sealed class RunFollowOptions
{
    /// <summary>Интервал опроса в первые <see cref="ShortPhase"/>.</summary>
    public TimeSpan ShortInterval { get; init; } = TimeSpan.FromSeconds(2);

    /// <summary>Интервал опроса после <see cref="ShortPhase"/>.</summary>
    public TimeSpan LongInterval { get; init; } = TimeSpan.FromSeconds(5);

    /// <summary>Длительность фазы частого опроса.</summary>
    public TimeSpan ShortPhase { get; init; } = TimeSpan.FromSeconds(30);

    /// <summary>Защита от гонки старта: не завершать сопровождение раньше этого времени.</summary>
    public TimeSpan StartGrace { get; init; } = TimeSpan.FromSeconds(5);

    /// <summary>Предельное время сопровождения одной задачи.</summary>
    public TimeSpan MaxDuration { get; init; } = TimeSpan.FromHours(24);
}

/// <summary>
/// Запуск завершился. Тексты для пользователя формирует UI-слой:
/// ViewModel передаёт только данные.
/// </summary>
public sealed class RunFinishedEventArgs : EventArgs
{
    public Guid TaskId { get; init; }

    public string Name { get; init; } = string.Empty;

    public RunStatus RunStatus { get; init; }

    public int? ExitCode { get; init; }

    public string ErrorMessage { get; init; } = string.Empty;
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
    private readonly RunFollowOptions _followOptions;

    // Сопровождение запусков: множество Id и один общий цикл опроса.
    private readonly object _followSync = new();
    private readonly Dictionary<Guid, FollowState> _followed = new();
    private CancellationTokenSource? _followCts;
    private bool _followLoopRunning;
    private bool _followUnavailableReported;

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
        WindowStateService? windowState = null,
        RunFollowOptions? runFollowOptions = null)
    {
        _ipc = ipc ?? throw new ArgumentNullException(nameof(ipc));
        _elevation = elevation;
        _windowState = windowState;
        _followOptions = runFollowOptions ?? new RunFollowOptions();

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
    /// Сопровождаемый запуск завершился. UI-слой сам формирует баннер и тексты.
    /// </summary>
    public event EventHandler<RunFinishedEventArgs>? RunFinished;

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
            TrackRunningTasks(dashboard);
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

    /// <summary>
    /// «Тихое» обновление без <see cref="IsLoading"/>: не блокирует кнопки,
    /// не показывает прогресс и не затирает текущую ошибку пользователя.
    /// Служебные сбои этого обновления остаются незаметными — их покажет
    /// следующий ручной <see cref="RefreshAsync"/>.
    /// </summary>
    public async Task RefreshSilentlyAsync(CancellationToken cancellationToken = default)
    {
        // Ручное обновление уже идёт — не мешаем ему и не дублируем запрос.
        if (Volatile.Read(ref _refreshInProgress) != 0)
        {
            return;
        }

        try
        {
            var dashboard = await _ipc.GetDashboardAsync(cancellationToken).ConfigureAwait(true);
            ApplyDashboard(dashboard);
            LastRefreshedAt = DateTimeOffset.Now;
            TrackRunningTasks(dashboard);
        }
        catch (OperationCanceledException) when (cancellationToken.IsCancellationRequested)
        {
            // Отмена — штатный сценарий.
        }
        catch (Exception)
        {
            // Тихое обновление не трогает баннер: ошибку покажет ручное обновление.
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

    /// <summary>
    /// Ставит задачу на сопровождение сразу после <c>RunNow</c>, не дожидаясь
    /// обновления дашборда. Повторный вызов для той же задачи начинает grace заново.
    /// </summary>
    public void TrackRun(Guid taskId)
    {
        lock (_followSync)
        {
            _followed[taskId] = new FollowState { StartedAt = DateTimeOffset.UtcNow };
        }

        EnsureFollowLoop();
    }

    /// <summary>
    /// Останавливает сопровождение всех задач и отменяет цикл опроса.
    /// Вызывается при закрытии окна.
    /// </summary>
    public void StopFollowing()
    {
        CancellationTokenSource? cts;
        lock (_followSync)
        {
            _followed.Clear();
            cts = _followCts;
        }

        cts?.Cancel();
    }

    /// <summary>
    /// После любого успешного обновления дашборда ставит на сопровождение
    /// все задачи с признаком <c>running=true</c> (ручные и по расписанию).
    /// </summary>
    private void TrackRunningTasks(DashboardDto dashboard)
    {
        var now = DateTimeOffset.UtcNow;
        lock (_followSync)
        {
            foreach (var task in dashboard.Tasks)
            {
                if (!task.Running || _followed.ContainsKey(task.Id))
                {
                    continue;
                }

                _followed[task.Id] = new FollowState { StartedAt = now, EverRunning = true };
            }
        }

        EnsureFollowLoop();
    }

    /// <summary>Запускает единственный цикл опроса, если он ещё не работает.</summary>
    private void EnsureFollowLoop()
    {
        CancellationToken token;
        lock (_followSync)
        {
            if (_followLoopRunning || _followed.Count == 0)
            {
                return;
            }

            _followCts?.Dispose();
            _followCts = new CancellationTokenSource();
            token = _followCts.Token;
            _followLoopRunning = true;
            _followUnavailableReported = false;
        }

        _ = FollowLoopAsync(token);
    }

    /// <summary>
    /// Один цикл на все сопровождаемые задачи: 2 с первые 30 с, затем 5 с.
    /// </summary>
    private async Task FollowLoopAsync(CancellationToken token)
    {
        var loopStartedAt = DateTimeOffset.UtcNow;
        try
        {
            while (!token.IsCancellationRequested)
            {
                Guid[] ids;
                lock (_followSync)
                {
                    ids = [.. _followed.Keys];
                }

                if (ids.Length == 0)
                {
                    break;
                }

                foreach (var taskId in ids)
                {
                    if (token.IsCancellationRequested)
                    {
                        break;
                    }

                    await PollTaskAsync(taskId, token).ConfigureAwait(true);
                }

                if (token.IsCancellationRequested)
                {
                    break;
                }

                var interval = DateTimeOffset.UtcNow - loopStartedAt < _followOptions.ShortPhase
                    ? _followOptions.ShortInterval
                    : _followOptions.LongInterval;
                await Task.Delay(interval, token).ConfigureAwait(true);
            }
        }
        catch (OperationCanceledException) when (token.IsCancellationRequested)
        {
            // Остановка сопровождения — штатный сценарий.
        }
        catch (Exception)
        {
            // Цикл не должен падать из-за одной ошибки; следующий запуск начнёт заново.
        }
        finally
        {
            var restart = false;
            lock (_followSync)
            {
                _followLoopRunning = false;
                restart = _followed.Count > 0;
            }

            if (restart)
            {
                EnsureFollowLoop();
            }
        }
    }

    /// <summary>Один опрос истории сопровождаемой задачи.</summary>
    private async Task PollTaskAsync(Guid taskId, CancellationToken token)
    {
        FollowState? state;
        lock (_followSync)
        {
            _followed.TryGetValue(taskId, out state);
        }

        if (state is null)
        {
            return;
        }

        var now = DateTimeOffset.UtcNow;
        if (now - state.StartedAt >= _followOptions.MaxDuration)
        {
            StopFollowing(taskId);
            return;
        }

        IReadOnlyList<RunHistory> history;
        try
        {
            history = await _ipc.GetHistoryAsync(taskId, 1, token).ConfigureAwait(true);
        }
        catch (OperationCanceledException) when (token.IsCancellationRequested)
        {
            return;
        }
        catch (IpcException ex) when (ex.Code == IpcErrorCode.NotFound)
        {
            // Задача удалена — сопровождение прекращаем без баннера.
            StopFollowing(taskId);
            return;
        }
        catch (IpcException ex) when (ex.Code == IpcErrorCode.Unavailable)
        {
            // Служба пропала: останавливаем весь цикл и один раз показываем ошибку.
            StopFollowingForUnavailable(ex);
            return;
        }
        catch (Exception)
        {
            // Прочие сбои опроса не спамят баннером: ждём следующий опрос.
            return;
        }

        if (token.IsCancellationRequested)
        {
            return;
        }

        var run = history.Count > 0 ? history[0] : null;
        if (run is null)
        {
            // Записи ещё нет: запуск мог не успеть попасть в историю.
            return;
        }

        if (run.Status == RunStatus.Running)
        {
            state.EverRunning = true;
            state.NotRunningPolls = 0;
            if (!TryApplyRunStatus(taskId, run))
            {
                StopFollowing(taskId);
            }

            return;
        }

        // Grace-период: первый Running=false может быть гонкой старта.
        if (DateTimeOffset.UtcNow - state.StartedAt < _followOptions.StartGrace)
        {
            return;
        }

        // Запись истории могла остаться от прошлого запуска: ждём новый.
        if (!state.EverRunning && !IsRunOfCurrentTracking(state, run))
        {
            return;
        }

        state.NotRunningPolls++;
        if (state.NotRunningPolls < 2)
        {
            return;
        }

        if (!TryApplyRunStatus(taskId, run))
        {
            StopFollowing(taskId);
            return;
        }

        var item = FindTask(taskId);
        StopFollowing(taskId);

        if (item is not null)
        {
            RunFinished?.Invoke(this, new RunFinishedEventArgs
            {
                TaskId = taskId,
                Name = item.Name,
                RunStatus = run.Status,
                ExitCode = run.ExitCode,
                ErrorMessage = run.ErrorMessage,
            });
        }
    }

    /// <summary>
    /// Отличает запись нового запуска от прошлой истории: допускаем небольшое
    /// расхождение часов UI и службы (запуск мог начаться до вызова <see cref="TrackRun"/>).
    /// </summary>
    private static bool IsRunOfCurrentTracking(FollowState state, RunHistory run)
    {
        if (run.StartTime == default)
        {
            return true;
        }

        return new DateTimeOffset(run.StartTime) >= state.StartedAt.AddSeconds(-2);
    }

    private bool TryApplyRunStatus(Guid taskId, RunHistory run)
    {
        var item = FindTask(taskId);
        if (item is null)
        {
            return false;
        }

        item.ApplyRunStatus(run);
        return true;
    }

    private TaskListItemViewModel? FindTask(Guid taskId)
        => Tasks.FirstOrDefault(task => task.Id == taskId);

    private void StopFollowing(Guid taskId)
    {
        lock (_followSync)
        {
            _followed.Remove(taskId);
        }
    }

    private void StopFollowingForUnavailable(IpcException exception)
    {
        StopFollowing();

        if (_followUnavailableReported)
        {
            return;
        }

        _followUnavailableReported = true;

        // Не перекрываем уже показанную пользователю ошибку.
        if (!HasError)
        {
            SetError(exception);
        }
    }

    /// <summary>Состояние сопровождения одной задачи.</summary>
    private sealed class FollowState
    {
        public DateTimeOffset StartedAt { get; init; }

        /// <summary>Хотя бы один опрос подтвердил состояние <c>Running</c>.</summary>
        public bool EverRunning { get; set; }

        public int NotRunningPolls { get; set; }
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
