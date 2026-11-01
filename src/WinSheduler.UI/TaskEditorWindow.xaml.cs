using System.Text.Json;
using System.Text.Json.Serialization;
using System.Windows;
using System.Windows.Controls;
using WinSheduler.Shared;
using WinSheduler.Shared.Models;

namespace WinSheduler.UI;

public partial class TaskEditorWindow : Window
{
    private static readonly JsonSerializerOptions JsonOpts = new()
    {
        Converters = { new JsonStringEnumConverter() }
    };

    private readonly IpcClient _ipc;
    private readonly TaskDefinitionViewModel? _existing;
    private readonly List<TimeWindowEditModel> _windows = [];
    private string? _existingPasswordEncrypted;

    public TaskEditorWindow(IpcClient ipc, TaskDefinitionViewModel? existing)
    {
        InitializeComponent();
        _ipc = ipc;
        _existing = existing;

        ApplyWorkAreaLimits();

        WindowStyleBox.SelectedIndex = 0;
        OnErrorBox.SelectedIndex = 0;
        OnOverlapBox.SelectedIndex = 0;

        if (existing != null)
        {
            Title = "Редактирование задачи";
            LoadExistingAsync(existing);
        }

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

    private async void LoadExistingAsync(TaskDefinitionViewModel existing)
    {
        var resp = await _ipc.SendAsync(new IpcRequest { Action = "GetTask", Payload = existing.Id.ToString() });
        if (!resp.Success)
        {
            MessageBox.Show($"Ошибка загрузки задачи: {resp.Error}", "Ошибка", MessageBoxButton.OK, MessageBoxImage.Error);
            return;
        }

        var task = JsonSerializer.Deserialize<TaskDto>(resp.Payload, JsonOpts);
        if (task == null)
        {
            MessageBox.Show("Не удалось разобрать ответ от службы", "Ошибка", MessageBoxButton.OK, MessageBoxImage.Error);
            return;
        }

        NameBox.Text = task.Name;
        DescBox.Text = task.Description;
        PathBox.Text = task.ProgramPath;
        ArgsBox.Text = task.Arguments;
        WorkDirBox.Text = task.WorkingDirectory;
        WindowStyleBox.SelectedIndex = (int)task.WindowStyle;
        OnErrorBox.SelectedIndex = (int)task.OnError;
        RetryCountBox.Text = task.RetryCount.ToString();
        OnOverlapBox.SelectedIndex = (int)task.OnOverlap;
        TimeoutBox.Text = task.TimeoutMinutes?.ToString() ?? "";
        KillOnTimeoutBox.IsChecked = task.KillOnTimeout;
        LogOutputBox.IsChecked = task.LogOutput;
        MaxHistoryBox.Text = task.MaxHistoryRecords.ToString();

        if (task.RunAsUser != null)
        {
            RunAsUserBox.Text = task.RunAsUser;
            RunAsDomainBox.Text = task.RunAsDomain ?? "";
            _existingPasswordEncrypted = task.RunAsPasswordEncrypted;
        }

        if (task.OnError == Shared.OnErrorAction.Retry)
        {
            RetryLabel.Visibility = Visibility.Visible;
            RetryCountBox.Visibility = Visibility.Visible;
        }

        var windowsResp = await _ipc.SendAsync(new IpcRequest { Action = "GetTimeWindows", Payload = existing.Id.ToString() });
        if (windowsResp.Success)
        {
            var windows = JsonSerializer.Deserialize<List<TimeWindowDto>>(windowsResp.Payload, JsonOpts) ?? [];
            foreach (var w in windows)
            {
                var model = new TimeWindowEditModel
                {
                    Type = MapEditType(w),
                    ExactTimesStr = string.Join(", ", w.ExactTimes ?? []),
                    StartTimeStr = w.StartTime ?? "",
                    EndTimeStr = w.EndTime ?? "",
                    RepeatIntervalStr = w.RepeatIntervalMinutes?.ToString() ?? "",
                    SelectedDays = w.DaysOfWeek?.ToList() ?? [],
                    StartDate = w.StartDate?.ToString("yyyy-MM-dd"),
                    EndDate = w.EndDate?.ToString("yyyy-MM-dd"),
                    RepeatUntil = w.RepeatUntil,
                    DayInterval = w.DayInterval,
                    WeekInterval = w.WeekInterval,
                    MonthDay = w.DaysOfMonth?.FirstOrDefault() ?? 1,
                    MonthInterval = w.MonthInterval,
                    OnceDate = w.SpecificDates?.FirstOrDefault(),
                    OnceTimeStr = w.ExactTimes?.FirstOrDefault(),
                };

                _windows.Add(model);
            }
            UpdateTimeWindowList();
        }
        else
        {
            MessageBox.Show($"Ошибка загрузки расписания: {windowsResp.Error}", "Ошибка", MessageBoxButton.OK, MessageBoxImage.Error);
        }
    }

    /// <summary>Определяет тип редактора по данным службы. Учитывает legacy-поля и новый <see cref="TriggerKind"/>.</summary>
    private static TimeWindowEditType MapEditType(TimeWindowDto w)
    {
        if (w.Kind == TriggerKind.Interval || w.Type == Shared.TimeWindowType.Interval)
            return TimeWindowEditType.Interval;
        if (w.SpecificDates?.Count > 0)
            return TimeWindowEditType.Once;
        if (w.DaysOfMonth?.Count > 0)
            return TimeWindowEditType.ExactMonthly;
        if (w.DaysOfWeek?.Count > 0)
            return TimeWindowEditType.ExactWeekly;
        return TimeWindowEditType.Exact;
    }

    private void BrowseBtn_Click(object sender, RoutedEventArgs e)
    {
        var dlg = new Microsoft.Win32.OpenFileDialog
        {
            Filter = "Executable files (*.exe)|*.exe|All files (*.*)|*.*",
            Title = "Выберите программу"
        };
        if (dlg.ShowDialog() == true)
            PathBox.Text = dlg.FileName;
    }

    private void OnErrorBox_SelectionChanged(object sender, SelectionChangedEventArgs e)
    {
        var isRetry = OnErrorBox.SelectedIndex == 1;
        RetryLabel.Visibility = isRetry ? Visibility.Visible : Visibility.Collapsed;
        RetryCountBox.Visibility = isRetry ? Visibility.Visible : Visibility.Collapsed;
        if (isRetry && string.IsNullOrEmpty(RetryCountBox.Text))
            RetryCountBox.Text = "3";
    }

    private void AddTimeBtn_Click(object sender, RoutedEventArgs e)
    {
        var dlg = new TimeWindowDialog();
        if (dlg.ShowDialog() == true)
        {
            _windows.Add(ToEditModel(dlg));
            UpdateTimeWindowList();
        }
    }

    private void RemoveTimeBtn_Click(object sender, RoutedEventArgs e)
    {
        if (TimeWindowList.SelectedItem is not TimeWindowEditModel model) return;
        _windows.Remove(model);
        UpdateTimeWindowList();
    }

    private void EditTimeBtn_Click(object sender, RoutedEventArgs e)
    {
        if (TimeWindowList.SelectedItem is TimeWindowEditModel model)
            EditTimeWindow(model);
    }

    private void DuplicateTimeBtn_Click(object sender, RoutedEventArgs e)
    {
        if (TimeWindowList.SelectedItem is not TimeWindowEditModel model) return;
        _windows.Add(Clone(model));
        UpdateTimeWindowList();
        TimeWindowList.SelectedIndex = _windows.Count - 1;
    }

    private void TimeWindowList_MouseDoubleClick(object sender, System.Windows.Input.MouseButtonEventArgs e)
    {
        if (TimeWindowList.SelectedItem is TimeWindowEditModel model)
            EditTimeWindow(model);
    }

    private void TimeWindowList_SelectionChanged(object sender, SelectionChangedEventArgs e)
    {
        var hasSelection = TimeWindowList.SelectedItem is TimeWindowEditModel;
        EditTimeBtn.IsEnabled = hasSelection;
        DuplicateTimeBtn.IsEnabled = hasSelection;
        RemoveTimeBtn.IsEnabled = hasSelection;
    }

    private void Window_PreviewKeyDown(object sender, System.Windows.Input.KeyEventArgs e)
    {
        if (e.Key == System.Windows.Input.Key.D && System.Windows.Input.Keyboard.Modifiers == System.Windows.Input.ModifierKeys.Control)
        {
            DuplicateTimeBtn_Click(sender, e);
            e.Handled = true;
        }
    }

    private void TimeWindowList_PreviewKeyDown(object sender, System.Windows.Input.KeyEventArgs e)
    {
        if (TimeWindowList.SelectedItem is not TimeWindowEditModel) return;

        if (e.Key == System.Windows.Input.Key.Delete)
        {
            RemoveTimeBtn_Click(sender, e);
            e.Handled = true;
        }
        else if (e.Key == System.Windows.Input.Key.Enter)
        {
            EditTimeBtn_Click(sender, e);
            e.Handled = true;
        }
    }

    /// <summary>Правый клик по строке выделяет её, чтобы контекстное меню применялось к нужному триггеру.</summary>
    private void TimeWindowList_PreviewMouseRightButtonDown(object sender, System.Windows.Input.MouseButtonEventArgs e)
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

    private static TimeWindowEditModel ToEditModel(TimeWindowDialog dlg) => new()
    {
        Type = dlg.SelectedType,
        ExactTimesStr = dlg.ExactTimes,
        StartTimeStr = dlg.StartTime,
        EndTimeStr = dlg.EndTime,
        RepeatIntervalStr = dlg.RepeatInterval,
        SelectedDays = dlg.SelectedDays,
        StartDate = dlg.StartDate,
        EndDate = dlg.EndDate,
        RepeatUntil = dlg.RepeatUntil,
        DayInterval = dlg.DayInterval,
        WeekInterval = dlg.WeekInterval,
        MonthDay = dlg.MonthDay,
        MonthInterval = dlg.MonthInterval,
        OnceDate = dlg.OnceDate != null && DateTime.TryParse(dlg.OnceDate, out var od) ? od : null,
        OnceTimeStr = dlg.OnceTime,
        TriggerEnabled = dlg.TriggerEnabled,
    };

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

    private void EditTimeWindow(TimeWindowEditModel model)
    {
        var dlg = new TimeWindowDialog(model);
        if (dlg.ShowDialog() == true)
        {
            var idx = _windows.IndexOf(model);
            if (idx < 0) return;
            _windows[idx] = ToEditModel(dlg);
            UpdateTimeWindowList();
        }
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

    private async void SaveBtn_Click(object sender, RoutedEventArgs e)
    {
        if (string.IsNullOrWhiteSpace(NameBox.Text) || string.IsNullOrWhiteSpace(PathBox.Text))
        {
            MessageBox.Show("Название и путь к программе обязательны", "Ошибка", MessageBoxButton.OK, MessageBoxImage.Warning);
            return;
        }

        bool hasRunAsUser = !string.IsNullOrWhiteSpace(RunAsUserBox.Text);
        bool hasNewPassword = RunAsPasswordBox.SecurePassword.Length > 0;
        bool hasExistingPassword = !string.IsNullOrEmpty(_existingPasswordEncrypted);

        if (hasRunAsUser && !hasNewPassword && !hasExistingPassword)
        {
            MessageBox.Show("Указан пользователь для запуска, но пароль не задан. Введите пароль.", "Ошибка", MessageBoxButton.OK, MessageBoxImage.Warning);
            return;
        }

        var dto = new TaskDto
        {
            Id = _existing?.Id ?? Guid.Empty,
            Name = NameBox.Text.Trim(),
            Description = DescBox.Text.Trim(),
            Enabled = _existing?.Enabled ?? true,
            ProgramPath = PathBox.Text.Trim(),
            Arguments = ArgsBox.Text.Trim(),
            WorkingDirectory = WorkDirBox.Text.Trim(),
            WindowStyle = (Shared.WindowStyle)WindowStyleBox.SelectedIndex,
            RunAsUser = string.IsNullOrWhiteSpace(RunAsUserBox.Text) ? null : RunAsUserBox.Text.Trim(),
            RunAsDomain = string.IsNullOrWhiteSpace(RunAsDomainBox.Text) ? null : RunAsDomainBox.Text.Trim(),
            RunAsPasswordEncrypted = RunAsPasswordBox.SecurePassword.Length > 0
                ? CryptoHelper.Encrypt(RunAsPasswordBox.Password)
                : _existingPasswordEncrypted,
            OnError = (Shared.OnErrorAction)OnErrorBox.SelectedIndex,
            RetryCount = int.TryParse(RetryCountBox.Text, out var rc) ? rc : 0,
            OnOverlap = (Shared.OnOverlapAction)OnOverlapBox.SelectedIndex,
            TimeoutMinutes = int.TryParse(TimeoutBox.Text, out var tm) ? tm : null,
            KillOnTimeout = KillOnTimeoutBox.IsChecked == true,
            LogOutput = LogOutputBox.IsChecked == true,
            MaxHistoryRecords = int.TryParse(MaxHistoryBox.Text, out var mh) ? mh : 50,
            TimeWindows = _windows.Select(w => new TimeWindowDto
            {
                Id = Guid.Empty,
                Type = w.Type == TimeWindowEditType.Interval
                    ? Shared.TimeWindowType.Interval
                    : Shared.TimeWindowType.ExactTimes,
                Kind = w.Type switch
                {
                    TimeWindowEditType.Once => TriggerKind.Once,
                    TimeWindowEditType.ExactWeekly => TriggerKind.Weekly,
                    TimeWindowEditType.ExactMonthly => TriggerKind.Monthly,
                    TimeWindowEditType.Interval => TriggerKind.Interval,
                    _ => TriggerKind.Daily,
                },
                ExactTimes = ParseTimesList(w),
                StartTime = w.Type == TimeWindowEditType.Interval ? w.StartTimeStr : null,
                EndTime = w.Type == TimeWindowEditType.Interval ? w.EndTimeStr : null,
                RepeatIntervalMinutes = w.Type == TimeWindowEditType.Interval
                    ? (int.TryParse(w.RepeatIntervalStr, out var ri) ? ri : null)
                    : null,
                StartDate = ParseLocalDate(w.StartDate),
                EndDate = ParseLocalDate(w.EndDate),
                RepeatUntil = string.IsNullOrWhiteSpace(w.RepeatUntil) ? null : w.RepeatUntil,
                DaysOfWeek = w.Type == TimeWindowEditType.ExactWeekly ? w.SelectedDays : [],
                DaysOfMonth = w.Type == TimeWindowEditType.ExactMonthly ? [w.MonthDay] : [],
                SpecificDates = w.Type == TimeWindowEditType.Once && w.OnceDate.HasValue
                    ? [w.OnceDate.Value.Date]
                    : [],
                DayInterval = w.Type == TimeWindowEditType.Exact ? w.DayInterval : 1,
                WeekInterval = w.Type == TimeWindowEditType.ExactWeekly ? w.WeekInterval : 1,
                MonthInterval = w.Type == TimeWindowEditType.ExactMonthly ? w.MonthInterval : 1,
            }).ToList()
        };

        var action = _existing == null ? "CreateTask" : "UpdateTask";
        var resp = await _ipc.SendAsync(new IpcRequest
        {
            Action = action,
            Payload = JsonSerializer.Serialize(dto, JsonOpts)
        });

        if (resp.Success)
        {
            DialogResult = true;
            Close();
        }
        else
        {
            MessageBox.Show($"Ошибка сохранения: {resp.Error}", "Ошибка", MessageBoxButton.OK, MessageBoxImage.Error);
        }
    }

    private void CancelBtn_Click(object sender, RoutedEventArgs e)
    {
        DialogResult = false;
        Close();
    }
    private static List<string> ParseTimesList(TimeWindowEditModel w)
    {
        if (w.Type == TimeWindowEditType.Once)
            return string.IsNullOrWhiteSpace(w.OnceTimeStr) ? [] : [w.OnceTimeStr];

        return (w.ExactTimesStr ?? "")
            .Split([',', ';', ' '], StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries)
            .ToList();
    }

    private static DateTime? ParseLocalDate(string? value)
    {
        if (string.IsNullOrWhiteSpace(value)) return null;
        return DateTime.TryParseExact(value, "yyyy-MM-dd", System.Globalization.CultureInfo.InvariantCulture,
            System.Globalization.DateTimeStyles.None, out var exact)
            ? exact
            : DateTime.TryParse(value, out var parsed) ? parsed.Date : null;
    }
}

public enum TimeWindowEditType { Once, Exact, ExactWeekly, ExactMonthly, Interval }

public class TimeWindowEditModel
{
    private static readonly System.Globalization.CultureInfo Invariant = System.Globalization.CultureInfo.InvariantCulture;
    private static readonly string[] ShortDayNames = ["Вс", "Пн", "Вт", "Ср", "Чт", "Пт", "Сб"];

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
        TimeWindowEditType.Once => "Однократно",
        TimeWindowEditType.Exact => "Ежедневно",
        TimeWindowEditType.ExactWeekly => "Еженедельно",
        TimeWindowEditType.ExactMonthly => "Ежемесячно",
        TimeWindowEditType.Interval => "Интервал",
        _ => "?"
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
                        ? $"Однократно: {onceDate.ToString("dd.MM.yyyy", Invariant)} в {OnceTimeStr}"
                        : "Однократно");
                    break;

                case TimeWindowEditType.Exact:
                    parts.Add(DayInterval > 1 ? $"Ежедневно, каждые {DayInterval} дн." : "Ежедневно");
                    if (!string.IsNullOrWhiteSpace(ExactTimesStr)) parts.Add($"в {ExactTimesStr}");
                    break;

                case TimeWindowEditType.ExactWeekly:
                    parts.Add(WeekInterval > 1 ? $"Еженедельно, каждые {WeekInterval} нед." : "Еженедельно");
                    var days = OrderedDays();
                    if (days.Count > 0) parts.Add(string.Join(", ", days));
                    if (!string.IsNullOrWhiteSpace(ExactTimesStr)) parts.Add($"в {ExactTimesStr}");
                    break;

                case TimeWindowEditType.ExactMonthly:
                    parts.Add(MonthInterval > 1
                        ? $"Ежемесячно, {MonthDay}-го числа, каждые {MonthInterval} мес."
                        : $"Ежемесячно, {MonthDay}-го числа");
                    if (!string.IsNullOrWhiteSpace(ExactTimesStr)) parts.Add($"в {ExactTimesStr}");
                    break;

                case TimeWindowEditType.Interval:
                    parts.Add($"Интервал: с {StartTimeStr} до {EndTimeStr}, каждые {RepeatIntervalStr} мин");
                    break;
            }

            if (ParseDate(StartDate) is { } start) parts.Add($"с {start:dd.MM.yyyy}");
            if (ParseDate(EndDate) is { } end) parts.Add($"по {end:dd.MM.yyyy}");
            if (ParseDateTime(RepeatUntil) is { } until)
                parts.Add($"повторять до {until.ToString("dd.MM.yyyy HH:mm", Invariant)}");
            if (!TriggerEnabled) parts.Add("выключено");

            return string.Join("; ", parts.Where(p => !string.IsNullOrWhiteSpace(p)));
        }
    }

    private List<string> OrderedDays()
    {
        DayOfWeek[] order =
        [
            DayOfWeek.Monday, DayOfWeek.Tuesday, DayOfWeek.Wednesday, DayOfWeek.Thursday,
            DayOfWeek.Friday, DayOfWeek.Saturday, DayOfWeek.Sunday
        ];
        return order.Where(SelectedDays.Contains).Select(d => ShortDayNames[(int)d]).ToList();
    }

    private static DateTime? ParseDate(string? value)
    {
        if (string.IsNullOrWhiteSpace(value)) return null;
        return DateTime.TryParseExact(value, "yyyy-MM-dd", Invariant, System.Globalization.DateTimeStyles.None, out var exact)
            ? exact
            : DateTime.TryParse(value, out var parsed) ? parsed.Date : null;
    }

    private static DateTime? ParseDateTime(string? value)
    {
        if (string.IsNullOrWhiteSpace(value)) return null;
        var formats = new[] { "yyyy-MM-ddTHH:mm", "yyyy-MM-ddTHH:mm:ss", "yyyy-MM-dd HH:mm:ss", "yyyy-MM-dd" };
        return DateTime.TryParseExact(value, formats, Invariant, System.Globalization.DateTimeStyles.None, out var parsed)
            ? parsed
            : null;
    }
}
