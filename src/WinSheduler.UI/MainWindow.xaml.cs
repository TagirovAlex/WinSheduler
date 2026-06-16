using System.Text.Json;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using WinSheduler.Shared;

namespace WinSheduler.UI;

public partial class MainWindow : Window
{
    private readonly IpcClient _ipc = new();
    private List<TaskDefinitionViewModel> _tasks = [];

    public MainWindow()
    {
        InitializeComponent();
        Loaded += async (_, _) => await Refresh();
    }

    private async Task Refresh()
    {
        try
        {
            StatusBarText.Text = "Загрузка...";

            var statusResp = await _ipc.SendAsync(new IpcRequest { Action = "GetStatus" });
            if (statusResp.Success)
            {
                var status = JsonSerializer.Deserialize<ServiceStatus>(statusResp.Payload);
                StatusText.Text = status?.Running == true
                    ? $"✅ Работает (задач: {status.TaskCount}, активных: {status.RunningTaskCount})"
                    : "❌ Остановлена";
                StatusText.Foreground = status?.Running == true
                    ? System.Windows.Media.Brushes.Green
                    : System.Windows.Media.Brushes.Red;
            }
            else
            {
                StatusText.Text = "❌ Служба недоступна";
                StatusText.Foreground = System.Windows.Media.Brushes.Red;
            }

            var tasksResp = await _ipc.SendAsync(new IpcRequest { Action = "ListTasks" });
            if (tasksResp.Success)
            {
                var tasks = JsonSerializer.Deserialize<List<TaskDefinitionViewModel>>(tasksResp.Payload) ?? [];
                foreach (var t in tasks)
                {
                    t.UpdateDerivedFields();
                    var windowsResp = await _ipc.SendAsync(new IpcRequest { Action = "GetTimeWindows", Payload = t.Id.ToString() });
                    if (windowsResp.Success)
                    {
                        var windows = JsonSerializer.Deserialize<List<TimeWindowViewModel>>(windowsResp.Payload) ?? [];
                        t.ScheduleSummary = windows.Count > 0
                            ? string.Join(", ", windows.Select(w => w.Summary))
                            : "Нет расписания";
                    }

                    var historyResp = await _ipc.SendAsync(new IpcRequest { Action = "GetHistory", Payload = t.Id.ToString() });
                    if (historyResp.Success)
                    {
                        var history = JsonSerializer.Deserialize<List<RunHistoryViewModel>>(historyResp.Payload) ?? [];
                        t.LastRun = history.Count > 0 ? history[0].StartTime.ToString("dd.MM.yy HH:mm") : "-";
                    }
                }
                _tasks = tasks;
                TaskListView.ItemsSource = _tasks;
            }

            StatusBarText.Text = $"Готов. Задач: {_tasks.Count}";
        }
        catch (Exception ex)
        {
            StatusText.Text = "❌ Ошибка подключения к службе";
            StatusBarText.Text = $"Ошибка: {ex.Message}";
        }
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
    public string Summary => Type == "ExactTimes"
        ? $"Точно: {string.Join(", ", ExactTimes)}"
        : $"Интервал: {StartTime}-{EndTime} кажд. {RepeatIntervalMinutes}мин";
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
