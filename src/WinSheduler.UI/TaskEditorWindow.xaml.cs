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

    public TaskEditorWindow(IpcClient ipc, TaskDefinitionViewModel? existing)
    {
        InitializeComponent();
        _ipc = ipc;
        _existing = existing;

        WindowStyleBox.SelectedIndex = 0;
        OnErrorBox.SelectedIndex = 0;
        OnOverlapBox.SelectedIndex = 0;

        if (existing != null)
        {
            Title = "Редактирование задачи";
            LoadExistingAsync(existing);
        }
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
        }

        if (task.OnError == Shared.OnErrorAction.Retry)
        {
            RetryLabel.Visibility = Visibility.Visible;
            RetryCountBox.Visibility = Visibility.Visible;
        }

        var windowsResp = await _ipc.SendAsync(new IpcRequest { Action = "GetTimeWindows", Payload = existing.Id.ToString() });
        if (windowsResp.Success)
        {
            var windows = JsonSerializer.Deserialize<List<TimeWindowViewModel>>(windowsResp.Payload, JsonOpts) ?? [];
            foreach (var w in windows)
            {
                var model = new TimeWindowEditModel
                {
                    ExactTimesStr = string.Join(", ", w.ExactTimes ?? []),
                    StartTimeStr = w.StartTime ?? "",
                    EndTimeStr = w.EndTime ?? "",
                    RepeatIntervalStr = w.RepeatIntervalMinutes?.ToString() ?? "",
                    SelectedDays = w.DaysOfWeek?.Select(d => d).ToList() ?? [],
                    StartDate = w.StartDate?.ToString("yyyy-MM-dd"),
                    EndDate = w.EndDate?.ToString("yyyy-MM-dd"),
                    RepeatUntil = w.RepeatUntil,
                };

                if (w.Type == "Interval")
                {
                    model.Type = TimeWindowEditType.Interval;
                }
                else if (w.SpecificDates?.Count > 0)
                {
                    model.Type = TimeWindowEditType.Once;
                    model.OnceDate = w.SpecificDates[0];
                    model.OnceTimeStr = w.ExactTimes?.FirstOrDefault() ?? "";
                }
                else if (w.DaysOfMonth?.Count > 0)
                {
                    model.Type = TimeWindowEditType.ExactMonthly;
                    model.MonthDay = w.DaysOfMonth[0];
                }
                else if (w.DaysOfWeek?.Count > 0)
                {
                    model.Type = TimeWindowEditType.ExactWeekly;
                }
                else
                {
                    model.Type = TimeWindowEditType.Exact;
                }

                _windows.Add(model);
            }
            UpdateTimeWindowList();
        }
        else
        {
            MessageBox.Show($"Ошибка загрузки расписания: {windowsResp.Error}", "Ошибка", MessageBoxButton.OK, MessageBoxImage.Error);
        }
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
            _windows.Add(new TimeWindowEditModel
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
            });
            UpdateTimeWindowList();
        }
    }

    private void RemoveTimeBtn_Click(object sender, RoutedEventArgs e)
    {
        if (TimeWindowList.SelectedItem is TimeWindowEditModel model)
        {
            _windows.Remove(model);
            UpdateTimeWindowList();
        }
    }

    private void EditTimeBtn_Click(object sender, RoutedEventArgs e)
    {
        if (TimeWindowList.SelectedItem is TimeWindowEditModel model)
            EditTimeWindow(model);
    }

    private void TimeWindowList_MouseDoubleClick(object sender, System.Windows.Input.MouseButtonEventArgs e)
    {
        if (TimeWindowList.SelectedItem is TimeWindowEditModel model)
            EditTimeWindow(model);
    }

    private void EditTimeWindow(TimeWindowEditModel model)
    {
        var dlg = new TimeWindowDialog(model);
        if (dlg.ShowDialog() == true)
        {
            var idx = _windows.IndexOf(model);
            _windows[idx] = new TimeWindowEditModel
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
            };
            UpdateTimeWindowList();
        }
    }

    private void UpdateTimeWindowList()
    {
        TimeWindowList.ItemsSource = null;
        TimeWindowList.ItemsSource = _windows;
    }

    private async void SaveBtn_Click(object sender, RoutedEventArgs e)
    {
        if (string.IsNullOrWhiteSpace(NameBox.Text) || string.IsNullOrWhiteSpace(PathBox.Text))
        {
            MessageBox.Show("Название и путь к программе обязательны", "Ошибка", MessageBoxButton.OK, MessageBoxImage.Warning);
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
            RunAsPasswordEncrypted = RunAsPasswordBox.SecurePassword.Length > 0 ? CryptoHelper.Encrypt(RunAsPasswordBox.Password) : null,
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
                Type = w.Type switch
                {
                    TimeWindowEditType.Once => Shared.TimeWindowType.ExactTimes,
                    TimeWindowEditType.Exact => Shared.TimeWindowType.ExactTimes,
                    TimeWindowEditType.ExactWeekly => Shared.TimeWindowType.ExactTimes,
                    TimeWindowEditType.ExactMonthly => Shared.TimeWindowType.ExactTimes,
                    TimeWindowEditType.Interval => Shared.TimeWindowType.Interval,
                    _ => Shared.TimeWindowType.ExactTimes,
                },
                ExactTimes = w.Type is TimeWindowEditType.Once or TimeWindowEditType.Exact or TimeWindowEditType.ExactWeekly or TimeWindowEditType.ExactMonthly
                    ? (w.ExactTimesStr?.Split(',', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries) ?? []).ToList()
                    : [],
                StartTime = w.Type == TimeWindowEditType.Interval ? w.StartTimeStr : null,
                EndTime = w.Type == TimeWindowEditType.Interval ? w.EndTimeStr : null,
                RepeatIntervalMinutes = w.Type == TimeWindowEditType.Interval
                    ? (int.TryParse(w.RepeatIntervalStr, out var ri) ? ri : null)
                    : null,
                StartDate = w.StartDate != null && DateTime.TryParse(w.StartDate, out var sd) ? sd : null,
                EndDate = w.EndDate != null && DateTime.TryParse(w.EndDate, out var ed) ? ed : null,
                RepeatUntil = w.RepeatUntil,
                DaysOfWeek = w.Type == TimeWindowEditType.Once ? [] : w.SelectedDays,
                SpecificDates = w.Type == TimeWindowEditType.Once && w.OnceDate.HasValue
                    ? [w.OnceDate.Value]
                    : [],
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
}

public enum TimeWindowEditType { Once, Exact, ExactWeekly, ExactMonthly, Interval }

public class TimeWindowEditModel
{
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

    public string Summary
    {
        get
        {
            var days = SelectedDays.Count > 0 ? string.Join(",", SelectedDays.Select(d => d.ToString()[..2])) + " " : "";
            return Type switch
            {
                TimeWindowEditType.Once => $"{OnceDate:yyyy-MM-dd} {OnceTimeStr}",
                TimeWindowEditType.Exact => $"Каждые {DayInterval}дн {ExactTimesStr}",
                TimeWindowEditType.ExactWeekly => $"Каждые {WeekInterval}нед {days}{ExactTimesStr}",
                TimeWindowEditType.ExactMonthly => $"День {MonthDay} кажд. {MonthInterval}мес {ExactTimesStr}",
                TimeWindowEditType.Interval => $"{days}{StartTimeStr}-{EndTimeStr} кажд. {RepeatIntervalStr}мин",
                _ => ""
            };
        }
    }
}
