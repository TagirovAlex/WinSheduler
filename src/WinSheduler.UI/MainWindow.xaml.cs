using System.Text.Json;
using System.Text.Json.Serialization;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using WinSheduler.Shared;

namespace WinSheduler.UI;

public partial class MainWindow : Window
{
    private static readonly JsonSerializerOptions JsonOpts = new()
    {
        Converters = { new JsonStringEnumConverter() }
    };

    private static readonly System.Windows.Media.Brush SuccessBrush =
        new System.Windows.Media.SolidColorBrush(System.Windows.Media.Color.FromRgb(0x10, 0x7C, 0x10));
    private static readonly System.Windows.Media.Brush ErrorBrush =
        new System.Windows.Media.SolidColorBrush(System.Windows.Media.Color.FromRgb(0xC4, 0x2B, 0x1C));

    private readonly IpcClient _ipc = new();
    private List<TaskDefinitionViewModel> _tasks = [];
    private bool _offline;

    public MainWindow()
    {
        InitializeComponent();
        ApplyWorkAreaLimits();
        Loaded += async (_, _) => await Refresh();
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

    private async Task Refresh()
    {
        try
        {
            StatusBarText.Text = "Загрузка...";

            var statusResp = await _ipc.SendAsync(new IpcRequest { Action = "GetStatus" });
            ServiceStatus? status = null;
            if (statusResp.Success)
                status = JsonSerializer.Deserialize<ServiceStatus>(statusResp.Payload);

            if (status?.Running != true)
            {
                StatusText.Text = "Служба: недоступна";
                StatusText.Foreground = ErrorBrush;
                SetOffline(true, statusResp.Success
                    ? "Служба не сообщает о готовности."
                    : statusResp.Error);
                return;
            }

            StatusText.Text = $"Служба: работает (задач: {status.TaskCount}, выполняется: {status.RunningTaskCount})";
            StatusText.Foreground = SuccessBrush;

            var tasksResp = await _ipc.SendAsync(new IpcRequest { Action = "ListTasks" });
            if (!tasksResp.Success)
            {
                SetOffline(true, tasksResp.Error);
                StatusBarText.Text = "Не удалось загрузить список задач";
                return;
            }

            var tasks = JsonSerializer.Deserialize<List<TaskDefinitionViewModel>>(tasksResp.Payload) ?? [];
            foreach (var t in tasks)
            {
                t.UpdateDerivedFields();
                var windowsResp = await _ipc.SendAsync(new IpcRequest { Action = "GetTimeWindows", Payload = t.Id.ToString() });
                if (windowsResp.Success)
                {
                    var windows = JsonSerializer.Deserialize<List<TimeWindowViewModel>>(windowsResp.Payload, JsonOpts) ?? [];
                    t.ScheduleSummary = windows.Count > 0
                        ? string.Join(", ", windows.Select(w => w.Summary))
                        : "Нет расписания";
                }

                var historyResp = await _ipc.SendAsync(new IpcRequest { Action = "GetHistory", Payload = t.Id.ToString() });
                if (historyResp.Success)
                {
                    var history = JsonSerializer.Deserialize<List<RunHistoryViewModel>>(historyResp.Payload) ?? [];
                    if (history.Count > 0)
                    {
                        var last = history[0].StartTime;
                        if (last.Kind != DateTimeKind.Local)
                            last = DateTime.SpecifyKind(last, DateTimeKind.Utc).ToLocalTime();
                        t.LastRun = last.ToString("dd.MM.yyyy HH:mm");
                    }
                }
            }
            _tasks = tasks;
            TaskListView.ItemsSource = _tasks;

            SetOffline(false, null);
            StatusBarText.Text = $"Готов. Задач: {_tasks.Count} · обновлено {DateTime.Now:dd.MM.yyyy HH:mm:ss}";
        }
        catch (Exception ex)
        {
            StatusText.Text = "Служба: ошибка подключения";
            StatusText.Foreground = ErrorBrush;
            SetOffline(true, ex.Message);
        }
    }

    /// <summary>Переключает интерфейс в режим «служба недоступна»: баннер, инструкция, блокировка действий.</summary>
    private void SetOffline(bool offline, string? detail)
    {
        _offline = offline;
        OfflineBanner.Visibility = offline ? Visibility.Visible : Visibility.Collapsed;
        TaskListView.IsEnabled = !offline;
        AddBtn.IsEnabled = !offline;

        if (offline)
        {
            OfflineDetail.Text =
                "Если служба не установлена — установите WinSheduler. Если установлена — запустите её " +
                "через «Службы» (services.msc) или командой «sc start WinSheduler» от имени администратора." +
                (string.IsNullOrWhiteSpace(detail) ? "" : $"\nПричина: {detail}");
            EmptyState.Visibility = Visibility.Collapsed;
            StatusBarText.Text = $"Нет связи со службой · обновлено {DateTime.Now:dd.MM.yyyy HH:mm:ss}";
        }
        else
        {
            EmptyState.Visibility = _tasks.Count == 0 ? Visibility.Visible : Visibility.Collapsed;
        }

        UpdateActionButtons();
    }

    private void UpdateActionButtons()
    {
        var hasSelection = !_offline && GetSelected() != null;
        EditBtn.IsEnabled = hasSelection;
        DeleteBtn.IsEnabled = hasSelection;
        HistoryBtn.IsEnabled = hasSelection;
        RunNowBtn.IsEnabled = hasSelection;
    }

    private void TaskListView_SelectionChanged(object sender, SelectionChangedEventArgs e) => UpdateActionButtons();

    private async void RetryBtn_Click(object sender, RoutedEventArgs e)
    {
        await Refresh();
    }

    private void OpenServicesBtn_Click(object sender, RoutedEventArgs e)
    {
        try
        {
            System.Diagnostics.Process.Start(new System.Diagnostics.ProcessStartInfo("services.msc")
            {
                UseShellExecute = true
            });
        }
        catch (Exception ex)
        {
            StatusBarText.Text = $"Не удалось открыть «Службы»: {ex.Message}";
        }
    }

    private void DiagnosticsBtn_Click(object sender, RoutedEventArgs e)
    {
        var window = new DiagnosticsWindow(_ipc) { Owner = this };
        window.ShowDialog();
    }

    private async void RefreshBtn_Click(object sender, RoutedEventArgs e)
    {
        try { await Refresh(); }
        catch (Exception ex) { StatusBarText.Text = $"Ошибка: {ex.Message}"; }
    }
    private async void AddBtn_Click(object sender, RoutedEventArgs e)
    {
        try { await OpenEditor(null); }
        catch (Exception ex) { MessageBox.Show(ex.Message, "Ошибка", MessageBoxButton.OK, MessageBoxImage.Error); }
    }
    private async void EditBtn_Click(object sender, RoutedEventArgs e)
    {
        try { await OpenEditor(GetSelected()); }
        catch (Exception ex) { MessageBox.Show(ex.Message, "Ошибка", MessageBoxButton.OK, MessageBoxImage.Error); }
    }
    private async void TaskListView_MouseDoubleClick(object sender, MouseButtonEventArgs e)
    {
        try { await OpenEditor(GetSelected()); }
        catch (Exception ex) { MessageBox.Show(ex.Message, "Ошибка", MessageBoxButton.OK, MessageBoxImage.Error); }
    }

    private TaskDefinitionViewModel? GetSelected() => TaskListView.SelectedItem as TaskDefinitionViewModel;

    private async Task OpenEditor(TaskDefinitionViewModel? task)
    {
        var editor = new TaskEditorWindow(_ipc, task);
        if (editor.ShowDialog() == true)
            await Refresh();
    }

    private async void DeleteBtn_Click(object sender, RoutedEventArgs e)
    {
        var task = GetSelected();
        if (task == null) return;

        var result = MessageBox.Show($"Удалить задачу \"{task.Name}\"?", "Подтверждение",
            MessageBoxButton.YesNo, MessageBoxImage.Question);
        if (result == MessageBoxResult.Yes)
        {
            try
            {
                await _ipc.SendAsync(new IpcRequest { Action = "DeleteTask", Payload = task.Id.ToString() });
                await Refresh();
            }
            catch (Exception ex) { MessageBox.Show(ex.Message, "Ошибка", MessageBoxButton.OK, MessageBoxImage.Error); }
        }
    }

    private async void RunNowBtn_Click(object sender, RoutedEventArgs e)
    {
        var task = GetSelected();
        if (task == null) return;
        try
        {
            var resp = await _ipc.SendAsync(new IpcRequest { Action = "RunNow", Payload = task.Id.ToString() });
            if (resp.Success)
                MessageBox.Show($"Задача \"{task.Name}\" запущена", "Запуск", MessageBoxButton.OK, MessageBoxImage.Information);
            else
                MessageBox.Show($"Ошибка запуска: {resp.Error}", "Ошибка", MessageBoxButton.OK, MessageBoxImage.Error);
        }
        catch (Exception ex) { MessageBox.Show(ex.Message, "Ошибка", MessageBoxButton.OK, MessageBoxImage.Error); }
    }

    private void HistoryBtn_Click(object sender, RoutedEventArgs e)
    {
        var task = GetSelected();
        if (task == null) return;
        var history = new HistoryWindow(_ipc, task.Id, task.Name);
        history.ShowDialog();
    }
}

public class TaskDefinitionViewModel
{
    public Guid Id { get; set; }
    public string Name { get; set; } = "";
    public string Description { get; set; } = "";
    public bool Enabled { get; set; } = true;
    public string ProgramPath { get; set; } = "";
    public string ScheduleSummary { get; set; } = "";
    public string LastRun { get; set; } = "-";
    public void UpdateDerivedFields() { }
}

public class TimeWindowViewModel
{
    public string Summary
    {
        get
        {
            if (Type == "Interval")
                return $"Интервал: {StartTime}-{EndTime} кажд. {RepeatIntervalMinutes}мин";

            var times = ExactTimes?.Count > 0 ? string.Join(", ", ExactTimes) : "?";
            var parts = new List<string>();

            if (DaysOfWeek?.Count > 0)
                parts.Add(string.Join("/", DaysOfWeek.Select(d => d.ToString()[..2])));
            if (DaysOfMonth?.Count > 0)
                parts.Add("дни " + string.Join(",", DaysOfMonth));
            if (SpecificDates?.Count > 0)
                parts.Add(string.Join(", ", SpecificDates.Select(d => d.ToString("dd.MM"))));

            return parts.Count > 0
                ? $"{times} [{string.Join(" ", parts)}]"
                : $"Точно: {times}";
        }
    }
    public string Type { get; set; } = "ExactTimes";
    public List<string> ExactTimes { get; set; } = [];
    public string? StartTime { get; set; }
    public string? EndTime { get; set; }
    public int? RepeatIntervalMinutes { get; set; }
    public List<DayOfWeek>? DaysOfWeek { get; set; }
    public List<int>? DaysOfMonth { get; set; }
    public List<DateTime>? SpecificDates { get; set; }
    public DateTime? StartDate { get; set; }
    public DateTime? EndDate { get; set; }
    public string? RepeatUntil { get; set; }
}

public class RunHistoryViewModel
{
    public DateTime StartTime { get; set; }
    public DateTime? EndTime { get; set; }
    public string Status { get; set; } = "";
    public int? ExitCode { get; set; }
    public string ErrorMessage { get; set; } = "";
}
