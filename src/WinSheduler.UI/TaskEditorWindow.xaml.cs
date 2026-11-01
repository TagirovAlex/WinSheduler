using System.ComponentModel;
using System.Globalization;
using System.IO;
using System.Text.Json;
using System.Text.Json.Serialization;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using WinSheduler.Shared;
using WinSheduler.Shared.Models;
using WinSheduler.UI.Localization;
using WinSheduler.UI.Services;

namespace WinSheduler.UI;

public partial class TaskEditorWindow : Window
{
    private static readonly CultureInfo Invariant = CultureInfo.InvariantCulture;

    private static readonly JsonSerializerOptions SnapshotOptions = new()
    {
        PropertyNameCaseInsensitive = true,
        Converters = { new JsonStringEnumConverter() },
    };

    private readonly IIpcService _ipc;
    private readonly Guid? _taskId;
    private readonly List<TimeWindowEditModel> _windows = [];

    private bool _loaded;
    private bool _busy;
    private bool _allowClose;
    private bool _enabled = true;
    private bool _hasStoredPassword;
    private bool _clearPasswordRequested;
    private string _baseline = string.Empty;

    public TaskEditorWindow(IIpcService ipc, Guid? taskId, string? taskName = null)
    {
        InitializeComponent();
        _ipc = ipc ?? throw new ArgumentNullException(nameof(ipc));
        _taskId = taskId;

        ApplyWorkAreaLimits();
        InitializeCombos();
        ConfigureElevation();

        Title = taskId is null
            ? Strings.Get("Editor_TitleNew")
            : string.IsNullOrWhiteSpace(taskName)
                ? Strings.Get("Editor_TitleEdit")
                : Strings.Format("Editor_TitleEditNamed", taskName);

        Loaded += Editor_Loaded;
        UpdatePasswordState();
        UpdateTimeWindowList();
    }

    /// <summary>Не даёт окну выйти за рабочую область при DPI 100–200 %.</summary>
    private void ApplyWorkAreaLimits()
    {
        var area = SystemParameters.WorkArea;
        MaxWidth = area.Width;
        MaxHeight = area.Height;
        Width = Math.Min(Width, Math.Max(MinWidth, area.Width - 32));
        Height = Math.Min(Height, Math.Max(MinHeight, area.Height - 32));
    }

    private void InitializeCombos()
    {
        WindowStyleBox.ItemsSource = Enum.GetValues<Shared.WindowStyle>();
        OnErrorBox.ItemsSource = Enum.GetValues<OnErrorAction>();
        OnOverlapBox.ItemsSource = Enum.GetValues<OnOverlapAction>();

        WindowStyleBox.SelectedItem = Shared.WindowStyle.Normal;
        OnErrorBox.SelectedItem = OnErrorAction.SkipNext;
        OnOverlapBox.SelectedItem = OnOverlapAction.WaitComplete;
        MaxHistoryBox.Text = "1000";
        RetryCountBox.Text = "0";
    }

    private void ConfigureElevation()
    {
        var canManage = App.Services?.Elevation.CanManageSystemTasks == true;
        RunAsGroup.IsEnabled = canManage;
        AdminBadge.Visibility = canManage ? Visibility.Collapsed : Visibility.Visible;
    }

    private async void Editor_Loaded(object sender, RoutedEventArgs e)
    {
        if (_taskId is not { } id)
        {
            // Новая задача: форма готова сразу.
            _loaded = true;
            _baseline = Snapshot();
            SaveBtn.IsEnabled = true;
            return;
        }

        SetBusy(true);
        try
        {
            var task = await _ipc.GetTaskAsync(id);
            IReadOnlyList<TimeWindowDto> windows = task.TimeWindows;
            if (windows.Count == 0)
            {
                windows = await _ipc.GetTimeWindowsAsync(id);
            }

            ApplyTask(task, windows);
            _loaded = true;
            _baseline = Snapshot();
            HideError();
        }
        catch (IpcException ex)
        {
            ShowError(IpcErrorText.Describe(ex));
        }
        catch (Exception ex)
        {
            ShowError(IpcErrorText.Describe(ex));
        }
        finally
        {
            SetBusy(false);
            SaveBtn.IsEnabled = _loaded;
        }
    }

    private void ApplyTask(TaskDto task, IReadOnlyList<TimeWindowDto> windows)
    {
        NameBox.Text = task.Name;
        DescBox.Text = task.Description;
        PathBox.Text = task.ProgramPath;
        ArgsBox.Text = task.Arguments;
        WorkDirBox.Text = task.WorkingDirectory;
        WindowStyleBox.SelectedItem = task.WindowStyle;
        OnErrorBox.SelectedItem = task.OnError;
        OnOverlapBox.SelectedItem = task.OnOverlap;
        RetryCountBox.Text = task.RetryCount.ToString(Invariant);
        TimeoutBox.Text = task.TimeoutMinutes?.ToString(Invariant) ?? string.Empty;
        KillOnTimeoutBox.IsChecked = task.KillOnTimeout;
        LogOutputBox.IsChecked = task.LogOutput;
        MaxHistoryBox.Text = task.MaxHistoryRecords.ToString(Invariant);
        _enabled = task.Enabled;
        _hasStoredPassword = task.HasPassword;

        RunAsUserBox.Text = task.RunAsUser ?? string.Empty;
        RunAsDomainBox.Text = task.RunAsDomain ?? string.Empty;

        _windows.Clear();
        foreach (var window in windows)
        {
            _windows.Add(ToEditModel(window));
        }

        UpdateTimeWindowList();
        UpdateRetryVisibility();
        UpdatePasswordState();
    }

    private void SetBusy(bool busy)
    {
        _busy = busy;
        SaveBtn.IsEnabled = !busy && _loaded;
        CancelBtn.IsEnabled = true;
    }

    private void BrowseBtn_Click(object sender, RoutedEventArgs e)
    {
        var dialogs = App.Services?.Dialogs;
        var path = dialogs?.PickFile(
            Strings.Get("Dialog_FileFilterProgram"),
            initialDirectory: null,
            Strings.Get("Dialog_BrowseProgram"));

        if (path is null) return;
        PathBox.Text = path;
    }

    private void OnErrorBox_SelectionChanged(object sender, SelectionChangedEventArgs e)
    {
        if (RetryLabel is null) return;
        UpdateRetryVisibility();
        if (OnErrorBox.SelectedItem is OnErrorAction.Retry &&
            string.IsNullOrWhiteSpace(RetryCountBox.Text))
        {
            RetryCountBox.Text = "3";
        }
    }

    private void UpdateRetryVisibility()
    {
        var isRetry = OnErrorBox.SelectedItem is OnErrorAction.Retry;
        var visibility = isRetry ? Visibility.Visible : Visibility.Collapsed;
        RetryLabel.Visibility = visibility;
        RetryCountBox.Visibility = visibility;
    }

    private void RunAsPasswordBox_PasswordChanged(object sender, RoutedEventArgs e)
    {
        if (RunAsPasswordBox.SecurePassword.Length > 0)
        {
            _clearPasswordRequested = false;
        }

        UpdatePasswordState();
    }

    private void ClearPasswordBtn_Click(object sender, RoutedEventArgs e)
    {
        _clearPasswordRequested = true;
        RunAsPasswordBox.Clear();
        UpdatePasswordState();
    }

    private void UpdatePasswordState()
    {
        if (PasswordStateText is null) return;

        var hasNew = RunAsPasswordBox.SecurePassword.Length > 0;
        PasswordStateText.Text = hasNew || (_hasStoredPassword && !_clearPasswordRequested)
            ? Strings.Get("Editor_PasswordSet")
            : _clearPasswordRequested
                ? Strings.Get("Editor_PasswordWillClear")
                : Strings.Get("Editor_PasswordNotSet");
    }

    // ===== Триггеры =====

    private void AddTimeBtn_Click(object sender, RoutedEventArgs e)
    {
        var dialog = new TimeWindowDialog();
        if (dialog.ShowDialog() == true)
        {
            _windows.Add(ToEditModel(dialog));
            UpdateTimeWindowList();
        }
    }

    private void EditTimeBtn_Click(object sender, RoutedEventArgs e)
    {
        if (TimeWindowList.SelectedItem is TimeWindowEditModel model)
        {
            EditTimeWindow(model);
        }
    }

    private void DuplicateTimeBtn_Click(object sender, RoutedEventArgs e)
    {
        if (TimeWindowList.SelectedItem is not TimeWindowEditModel model) return;

        _windows.Add(Clone(model));
        UpdateTimeWindowList();
        TimeWindowList.SelectedIndex = _windows.Count - 1;
    }

    private void RemoveTimeBtn_Click(object sender, RoutedEventArgs e)
    {
        if (TimeWindowList.SelectedItem is not TimeWindowEditModel model) return;

        _windows.Remove(model);
        UpdateTimeWindowList();
    }

    private void TimeWindowList_MouseDoubleClick(object sender, MouseButtonEventArgs e)
    {
        if (TimeWindowList.SelectedItem is TimeWindowEditModel model)
        {
            EditTimeWindow(model);
        }
    }

    private void TimeWindowList_SelectionChanged(object sender, SelectionChangedEventArgs e)
    {
        var hasSelection = TimeWindowList.SelectedItem is TimeWindowEditModel;
        EditTimeBtn.IsEnabled = hasSelection;
        DuplicateTimeBtn.IsEnabled = hasSelection;
        RemoveTimeBtn.IsEnabled = hasSelection;
    }

    private void TimeWindowList_PreviewKeyDown(object sender, KeyEventArgs e)
    {
        if (TimeWindowList.SelectedItem is not TimeWindowEditModel) return;

        if (e.Key == Key.Delete)
        {
            RemoveTimeBtn_Click(sender, e);
            e.Handled = true;
        }
        else if (e.Key == Key.Enter)
        {
            EditTimeBtn_Click(sender, e);
            e.Handled = true;
        }
    }

    /// <summary>Правый клик по строке выделяет её, чтобы контекстное меню применялось к нужному триггеру.</summary>
    private void TimeWindowList_PreviewMouseRightButtonDown(object sender, MouseButtonEventArgs e)
    {
        var item = FindAncestor<ListViewItem>(e.OriginalSource as DependencyObject);
        if (item != null) item.IsSelected = true;
    }

    private static T? FindAncestor<T>(DependencyObject? current) where T : DependencyObject
    {
        while (current != null)
        {
            if (current is T match) return match;
            current = System.Windows.Media.VisualTreeHelper.GetParent(current);
        }

        return null;
    }

    private void EditTimeWindow(TimeWindowEditModel model)
    {
        var dialog = new TimeWindowDialog(model);
        if (dialog.ShowDialog() != true) return;

        var index = _windows.IndexOf(model);
        if (index < 0) return;

        _windows[index] = ToEditModel(dialog);
        UpdateTimeWindowList();
    }

    private void UpdateTimeWindowList()
    {
        TimeWindowList.ItemsSource = null;
        TimeWindowList.ItemsSource = _windows;
        TimeWindowEmptyText.Visibility = _windows.Count == 0 ? Visibility.Visible : Visibility.Collapsed;

        var hasSelection = TimeWindowList.SelectedItem is TimeWindowEditModel;
        EditTimeBtn.IsEnabled = hasSelection;
        DuplicateTimeBtn.IsEnabled = hasSelection;
        RemoveTimeBtn.IsEnabled = hasSelection;
    }

    private static TimeWindowEditModel ToEditModel(TimeWindowDialog dialog) => new()
    {
        Type = dialog.SelectedType,
        ExactTimesStr = dialog.ExactTimes,
        StartTimeStr = dialog.StartTime,
        EndTimeStr = dialog.EndTime,
        RepeatIntervalStr = dialog.RepeatInterval,
        SelectedDays = dialog.SelectedDays,
        StartDate = dialog.StartDate,
        EndDate = dialog.EndDate,
        RepeatUntil = dialog.RepeatUntil,
        DayInterval = dialog.DayInterval,
        WeekInterval = dialog.WeekInterval,
        MonthDay = dialog.MonthDay,
        MonthInterval = dialog.MonthInterval,
        OnceDate = ParseLocalDate(dialog.OnceDate),
        OnceTimeStr = dialog.OnceTime,
        TriggerEnabled = dialog.TriggerEnabled,
    };

    private static TimeWindowEditModel ToEditModel(TimeWindowDto window)
    {
        var model = new TimeWindowEditModel
        {
            Type = MapEditType(window),
            ExactTimesStr = string.Join(", ", window.ExactTimes),
            StartTimeStr = window.StartTime ?? string.Empty,
            EndTimeStr = window.EndTime ?? string.Empty,
            RepeatIntervalStr = window.RepeatIntervalMinutes?.ToString(Invariant) ?? string.Empty,
            SelectedDays = [.. window.DaysOfWeek],
            StartDate = window.StartDate?.ToString("yyyy-MM-dd", Invariant),
            EndDate = window.EndDate?.ToString("yyyy-MM-dd", Invariant),
            RepeatUntil = window.RepeatUntil,
            DayInterval = window.DayInterval,
            WeekInterval = window.WeekInterval,
            MonthDay = window.DaysOfMonth.FirstOrDefault(1),
            MonthInterval = window.MonthInterval,
            OnceDate = window.SpecificDates.FirstOrDefault(),
            OnceTimeStr = window.ExactTimes.FirstOrDefault(),
        };

        return model;
    }

    private static TimeWindowEditModel Clone(TimeWindowEditModel model) => new()
    {
        Type = model.Type,
        ExactTimesStr = model.ExactTimesStr,
        StartTimeStr = model.StartTimeStr,
        EndTimeStr = model.EndTimeStr,
        RepeatIntervalStr = model.RepeatIntervalStr,
        SelectedDays = [.. model.SelectedDays],
        StartDate = model.StartDate,
        EndDate = model.EndDate,
        RepeatUntil = model.RepeatUntil,
        DayInterval = model.DayInterval,
        WeekInterval = model.WeekInterval,
        MonthDay = model.MonthDay,
        MonthInterval = model.MonthInterval,
        OnceDate = model.OnceDate,
        OnceTimeStr = model.OnceTimeStr,
        TriggerEnabled = model.TriggerEnabled,
    };

    /// <summary>Определяет тип редактора по данным службы: новый <see cref="TriggerKind"/> и legacy-поля.</summary>
    private static TimeWindowEditType MapEditType(TimeWindowDto window)
    {
        if (window.Kind == TriggerKind.Interval || window.Type == Shared.TimeWindowType.Interval)
            return TimeWindowEditType.Interval;
        if (window.SpecificDates.Count > 0)
            return TimeWindowEditType.Once;
        if (window.DaysOfMonth.Count > 0)
            return TimeWindowEditType.ExactMonthly;
        if (window.DaysOfWeek.Count > 0)
            return TimeWindowEditType.ExactWeekly;
        return TimeWindowEditType.Exact;
    }

    // ===== Сохранение =====

    private void SaveBtn_Click(object sender, RoutedEventArgs e)
    {
        if (!_loaded || _busy) return;
        if (!Validate()) return;

        _ = SaveAsync();
    }

    private async Task SaveAsync()
    {
        SetBusy(true);
        try
        {
            var dto = BuildDto();
            if (_taskId is null)
            {
                await _ipc.CreateTaskAsync(dto);
            }
            else
            {
                await _ipc.UpdateTaskAsync(dto);
            }

            _allowClose = true;
            try
            {
                DialogResult = true;
            }
            catch (InvalidOperationException)
            {
                // Окно показано не как модальное — просто закрываем.
                Close();
            }
        }
        catch (IpcException ex)
        {
            ShowError(IpcErrorText.Describe(ex));
        }
        catch (Exception ex)
        {
            ShowError(IpcErrorText.Describe(ex));
        }
        finally
        {
            SetBusy(false);
        }
    }

    private TaskDto BuildDto()
    {
        var canManage = App.Services?.Elevation.CanManageSystemTasks == true;
        var runAsUser = canManage && !string.IsNullOrWhiteSpace(RunAsUserBox.Text)
            ? RunAsUserBox.Text.Trim()
            : null;

        var password = canManage && runAsUser is not null
            ? RunAsPasswordBox.SecurePassword.Length > 0
                ? RunAsPasswordBox.Password
                : _clearPasswordRequested ? string.Empty : null
            : null;

        return new TaskDto
        {
            Id = _taskId ?? Guid.Empty,
            Name = NameBox.Text.Trim(),
            Description = DescBox.Text.Trim(),
            Enabled = _enabled,
            ProgramPath = NormalizePath(PathBox.Text),
            Arguments = ArgsBox.Text.Trim(),
            WorkingDirectory = NormalizePath(WorkDirBox.Text),
            WindowStyle = WindowStyleBox.SelectedItem is Shared.WindowStyle style ? style : Shared.WindowStyle.Normal,
            SessionMode = SessionMode.Headless,
            RunAsUser = runAsUser,
            RunAsDomain = runAsUser is null ? null : NullIfWhiteSpace(RunAsDomainBox.Text),
            Password = password,
            HasPassword = RunAsPasswordBox.SecurePassword.Length > 0 ||
                          (_hasStoredPassword && !_clearPasswordRequested),
            OnError = OnErrorBox.SelectedItem is OnErrorAction onError ? onError : OnErrorAction.SkipNext,
            RetryCount = ParseInt(RetryCountBox.Text),
            OnOverlap = OnOverlapBox.SelectedItem is OnOverlapAction overlap
                ? overlap
                : OnOverlapAction.WaitComplete,
            TimeoutMinutes = string.IsNullOrWhiteSpace(TimeoutBox.Text) ? null : ParseInt(TimeoutBox.Text),
            KillOnTimeout = KillOnTimeoutBox.IsChecked == true,
            LogOutput = LogOutputBox.IsChecked == true,
            MaxHistoryRecords = ParseInt(MaxHistoryBox.Text),
            TimeWindows = _windows.Select(ToDto).ToList(),
        };
    }

    private static TimeWindowDto ToDto(TimeWindowEditModel window) => new()
    {
        Id = Guid.Empty,
        Type = window.Type == TimeWindowEditType.Interval
            ? Shared.TimeWindowType.Interval
            : Shared.TimeWindowType.ExactTimes,
        Kind = window.Type switch
        {
            TimeWindowEditType.Once => TriggerKind.Once,
            TimeWindowEditType.ExactWeekly => TriggerKind.Weekly,
            TimeWindowEditType.ExactMonthly => TriggerKind.Monthly,
            TimeWindowEditType.Interval => TriggerKind.Interval,
            _ => TriggerKind.Daily,
        },
        ExactTimes = ParseTimes(window),
        StartTime = window.Type == TimeWindowEditType.Interval ? window.StartTimeStr : null,
        EndTime = window.Type == TimeWindowEditType.Interval ? window.EndTimeStr : null,
        RepeatIntervalMinutes = window.Type == TimeWindowEditType.Interval
            ? int.TryParse(window.RepeatIntervalStr, out var repeat) ? repeat : null
            : null,
        StartDate = ParseLocalDate(window.StartDate),
        EndDate = ParseLocalDate(window.EndDate),
        RepeatUntil = string.IsNullOrWhiteSpace(window.RepeatUntil) ? null : window.RepeatUntil,
        DaysOfWeek = window.Type == TimeWindowEditType.ExactWeekly ? window.SelectedDays : [],
        DaysOfMonth = window.Type == TimeWindowEditType.ExactMonthly ? [window.MonthDay] : [],
        SpecificDates = window.Type == TimeWindowEditType.Once && window.OnceDate.HasValue
            ? [window.OnceDate.Value.Date]
            : [],
        DayInterval = window.Type == TimeWindowEditType.Exact ? window.DayInterval : 1,
        WeekInterval = window.Type == TimeWindowEditType.ExactWeekly ? window.WeekInterval : 1,
        MonthInterval = window.Type == TimeWindowEditType.ExactMonthly ? window.MonthInterval : 1,
    };

    private static List<string> ParseTimes(TimeWindowEditModel window)
    {
        if (window.Type == TimeWindowEditType.Once)
        {
            return string.IsNullOrWhiteSpace(window.OnceTimeStr) ? [] : [window.OnceTimeStr];
        }

        return window.ExactTimesStr
            .Split([',', ';', ' '], StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries)
            .ToList();
    }

    private static DateTime? ParseLocalDate(string? value)
    {
        if (string.IsNullOrWhiteSpace(value)) return null;

        return DateTime.TryParseExact(
            value,
            "yyyy-MM-dd",
            Invariant,
            DateTimeStyles.None,
            out var exact)
            ? exact
            : DateTime.TryParse(value, out var parsed) ? parsed.Date : null;
    }

    private static string NormalizePath(string value) => value.Trim().Replace('\\', '/');

    private static string? NullIfWhiteSpace(string value)
        => string.IsNullOrWhiteSpace(value) ? null : value.Trim();

    private static int ParseInt(string value)
        => int.TryParse(value.Trim(), NumberStyles.Integer, Invariant, out var parsed) ? parsed : 0;

    // ===== Валидация =====

    private bool Validate()
    {
        HideError();

        if (string.IsNullOrWhiteSpace(NameBox.Text))
        {
            return Fail(Strings.Get("Editor_ValidationName"), NameBox);
        }

        var programPath = PathBox.Text.Trim();
        if (programPath.Length == 0)
        {
            return Fail(Strings.Get("Editor_ValidationProgram"), PathBox);
        }

        if (!IsValidLocalPath(programPath, allowDriveRoot: false, out var pathError))
        {
            return Fail(Strings.Format("Editor_ValidationPath", pathError), PathBox);
        }

        var workDir = WorkDirBox.Text.Trim();
        if (workDir.Length > 0 && !IsValidLocalPath(workDir, allowDriveRoot: false, out var workDirError))
        {
            return Fail(Strings.Format("Editor_ValidationWorkingDirectory", workDirError), WorkDirBox);
        }

        if (OnErrorBox.SelectedItem is OnErrorAction.Retry &&
            !TryParseRange(RetryCountBox.Text, 0, 10, out _))
        {
            return Fail(Strings.Format("Editor_ValidationRetry"), RetryCountBox);
        }

        if (!string.IsNullOrWhiteSpace(TimeoutBox.Text) &&
            !TryParseRange(TimeoutBox.Text, 0, 10080, out _))
        {
            return Fail(Strings.Format("Editor_ValidationTimeout"), TimeoutBox);
        }

        if (!TryParseRange(MaxHistoryBox.Text, 1, 10000, out _))
        {
            return Fail(Strings.Format("Editor_ValidationHistory"), MaxHistoryBox);
        }

        if (App.Services?.Elevation.CanManageSystemTasks == true &&
            !string.IsNullOrWhiteSpace(RunAsUserBox.Text) &&
            RunAsPasswordBox.SecurePassword.Length == 0 &&
            !_hasStoredPassword)
        {
            return Fail(Strings.Get("Editor_ValidationPassword"), RunAsPasswordBox);
        }

        return true;
    }

    /// <summary>Проверяет локальный абсолютный путь по правилам SECURITY.md, раздел 5.</summary>
    private static bool IsValidLocalPath(string value, bool allowDriveRoot, out string error)
    {
        error = string.Empty;
        var normalized = value.Replace('\\', '/');

        if (!Path.IsPathRooted(value))
        {
            error = Strings.Get("Editor_PathNotAbsolute");
            return false;
        }

        if (normalized.StartsWith("//", StringComparison.Ordinal))
        {
            error = Strings.Get("Editor_PathUnc");
            return false;
        }

        if (normalized.Contains("//?/", StringComparison.Ordinal) ||
            normalized.Contains("//./", StringComparison.Ordinal))
        {
            error = Strings.Get("Editor_PathDevice");
            return false;
        }

        if (normalized.Split('/').Any(segment => segment == ".."))
        {
            error = Strings.Get("Editor_PathTraversal");
            return false;
        }

        var colon = normalized.IndexOf(':');
        if (colon >= 0 && colon != 1)
        {
            error = Strings.Get("Editor_PathStream");
            return false;
        }

        if (!allowDriveRoot)
        {
            var root = Path.GetPathRoot(value)?.Replace('\\', '/').TrimEnd('/');
            if (!string.IsNullOrEmpty(root) &&
                string.Equals(normalized.TrimEnd('/'), root, StringComparison.OrdinalIgnoreCase))
            {
                error = Strings.Get("Editor_PathDriveRoot");
                return false;
            }
        }

        return true;
    }

    private static bool TryParseRange(string value, int min, int max, out int parsed)
    {
        parsed = 0;
        return int.TryParse(value.Trim(), NumberStyles.Integer, Invariant, out parsed) &&
               parsed >= min && parsed <= max;
    }

    private bool Fail(string message, Control control)
    {
        ShowError(message);
        control.Focus();
        return false;
    }

    private void ShowError(string message)
    {
        ErrorText.Text = message;
        ErrorBanner.Visibility = Visibility.Visible;
    }

    private void HideError()
    {
        ErrorText.Text = string.Empty;
        ErrorBanner.Visibility = Visibility.Collapsed;
    }

    // ===== Закрытие и dirty tracking =====

    private string Snapshot() => JsonSerializer.Serialize(BuildDto(), SnapshotOptions);

    private bool IsDirty => _loaded && !string.Equals(Snapshot(), _baseline, StringComparison.Ordinal);

    protected override void OnClosing(CancelEventArgs e)
    {
        if (!_allowClose && IsDirty)
        {
            var dialogs = App.Services?.Dialogs;
            var discard = dialogs?.Confirm(Strings.Get("Dialog_UnsavedChanges")) ?? true;
            if (!discard)
            {
                e.Cancel = true;
                base.OnClosing(e);
                return;
            }
        }

        base.OnClosing(e);
    }

    private void CancelBtn_Click(object sender, RoutedEventArgs e) => Close();

    private void Window_PreviewKeyDown(object sender, KeyEventArgs e)
    {
        if (e.Key == Key.D && Keyboard.Modifiers == ModifierKeys.Control)
        {
            DuplicateTimeBtn_Click(sender, e);
            e.Handled = true;
        }
    }
}

public enum TimeWindowEditType { Once, Exact, ExactWeekly, ExactMonthly, Interval }

public class TimeWindowEditModel
{
    private static readonly CultureInfo Invariant = CultureInfo.InvariantCulture;

    public TimeWindowEditType Type { get; set; }
    public string ExactTimesStr { get; set; } = "";
    public string StartTimeStr { get; set; } = "";
    public string EndTimeStr { get; set; } = "";
    public string RepeatIntervalStr { get; set; } = "";
    public List<DayOfWeek> SelectedDays { get; set; } = [];
    public string? StartDate { get; set; }
    public string? EndDate { get; set; }
    public string? RepeatUntil { get; set; }
    public int DayInterval { get; set; } = 1;
    public int WeekInterval { get; set; } = 1;
    public int MonthDay { get; set; } = 1;
    public int MonthInterval { get; set; } = 1;
    public DateTime? OnceDate { get; set; }
    public string? OnceTimeStr { get; set; }
    public bool TriggerEnabled { get; set; } = true;

    public string TypeDisplay => Type switch
    {
        TimeWindowEditType.Once => Strings.Get("TriggerKind_Once"),
        TimeWindowEditType.Exact => Strings.Get("TriggerKind_Daily"),
        TimeWindowEditType.ExactWeekly => Strings.Get("TriggerKind_Weekly"),
        TimeWindowEditType.ExactMonthly => Strings.Get("TriggerKind_Monthly"),
        TimeWindowEditType.Interval => Strings.Get("TriggerKind_Interval"),
        _ => Strings.Get("Common_Unknown")
    };

    /// <summary>Локализованное описание триггера без сырых ISO-дат.</summary>
    public string Summary
    {
        get
        {
            var parts = new List<string>();
            switch (Type)
            {
                case TimeWindowEditType.Once:
                    parts.Add(OnceDate is { } onceDate
                        ? Strings.Format("Trigger_SummaryOnce", onceDate.ToString("dd.MM.yyyy", Invariant), OnceTimeStr)
                        : Strings.Get("Trigger_SummaryOnceNoDate"));
                    break;

                case TimeWindowEditType.Exact:
                    parts.Add(DayInterval > 1
                        ? Strings.Format("Trigger_SummaryDailyEvery", DayInterval)
                        : Strings.Get("Trigger_SummaryDaily"));
                    if (!string.IsNullOrWhiteSpace(ExactTimesStr))
                        parts.Add(Strings.Format("Trigger_SummaryAt", ExactTimesStr));
                    break;

                case TimeWindowEditType.ExactWeekly:
                    parts.Add(WeekInterval > 1
                        ? Strings.Format("Trigger_SummaryWeeklyEvery", WeekInterval)
                        : Strings.Get("Trigger_SummaryWeekly"));
                    var days = OrderedDays();
                    if (days.Count > 0) parts.Add(string.Join(", ", days));
                    if (!string.IsNullOrWhiteSpace(ExactTimesStr))
                        parts.Add(Strings.Format("Trigger_SummaryAt", ExactTimesStr));
                    break;

                case TimeWindowEditType.ExactMonthly:
                    parts.Add(MonthInterval > 1
                        ? Strings.Format("Trigger_SummaryMonthlyEvery", MonthDay, MonthInterval)
                        : Strings.Format("Trigger_SummaryMonthly", MonthDay));
                    if (!string.IsNullOrWhiteSpace(ExactTimesStr))
                        parts.Add(Strings.Format("Trigger_SummaryAt", ExactTimesStr));
                    break;

                case TimeWindowEditType.Interval:
                    parts.Add(Strings.Format("Trigger_SummaryInterval", StartTimeStr, EndTimeStr, RepeatIntervalStr));
                    break;
            }

            if (ParseDate(StartDate) is { } start)
                parts.Add(Strings.Format("Trigger_SummaryFrom", start.ToString("dd.MM.yyyy", Invariant)));
            if (ParseDate(EndDate) is { } end)
                parts.Add(Strings.Format("Trigger_SummaryTo", end.ToString("dd.MM.yyyy", Invariant)));
            if (ParseDateTime(RepeatUntil) is { } until)
                parts.Add(Strings.Format("Trigger_SummaryUntil", until.ToString("dd.MM.yyyy HH:mm", Invariant)));
            if (!TriggerEnabled) parts.Add(Strings.Get("Trigger_SummaryDisabled"));

            return string.Join("; ", parts.Where(part => !string.IsNullOrWhiteSpace(part)));
        }
    }

    private List<string> OrderedDays()
    {
        DayOfWeek[] order =
        [
            DayOfWeek.Monday, DayOfWeek.Tuesday, DayOfWeek.Wednesday, DayOfWeek.Thursday,
            DayOfWeek.Friday, DayOfWeek.Saturday, DayOfWeek.Sunday
        ];

        return order
            .Where(SelectedDays.Contains)
            .Select(day => Strings.Get(day switch
            {
                DayOfWeek.Monday => "DayShort_Monday",
                DayOfWeek.Tuesday => "DayShort_Tuesday",
                DayOfWeek.Wednesday => "DayShort_Wednesday",
                DayOfWeek.Thursday => "DayShort_Thursday",
                DayOfWeek.Friday => "DayShort_Friday",
                DayOfWeek.Saturday => "DayShort_Saturday",
                _ => "DayShort_Sunday",
            }))
            .ToList();
    }

    private static DateTime? ParseDate(string? value)
    {
        if (string.IsNullOrWhiteSpace(value)) return null;

        return DateTime.TryParseExact(value, "yyyy-MM-dd", Invariant, DateTimeStyles.None, out var exact)
            ? exact
            : DateTime.TryParse(value, out var parsed) ? parsed.Date : null;
    }

    private static DateTime? ParseDateTime(string? value)
    {
        if (string.IsNullOrWhiteSpace(value)) return null;

        var formats = new[] { "yyyy-MM-ddTHH:mm", "yyyy-MM-ddTHH:mm:ss", "yyyy-MM-dd HH:mm:ss", "yyyy-MM-dd" };
        return DateTime.TryParseExact(value, formats, Invariant, DateTimeStyles.None, out var parsed)
            ? parsed
            : null;
    }
}
