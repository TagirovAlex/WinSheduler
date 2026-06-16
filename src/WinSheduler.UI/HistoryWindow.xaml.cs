using System.Text.Json;
using System.Windows;
using WinSheduler.Shared;

namespace WinSheduler.UI;

public partial class HistoryWindow : Window
{
    public HistoryWindow(IpcClient ipc, Guid taskId, string taskName)
    {
        InitializeComponent();
        TitleText.Text = $"Журнал запусков: {taskName}";
        LoadHistoryAsync(ipc, taskId);
    }

    private async void LoadHistoryAsync(IpcClient ipc, Guid taskId)
    {
        try
        {
            var resp = await ipc.SendAsync(new IpcRequest { Action = "GetHistory", Payload = taskId.ToString() });
            if (resp.Success)
            {
                var history = JsonSerializer.Deserialize<List<HistoryItem>>(resp.Payload) ?? [];
                HistoryList.ItemsSource = history;
            }
        }
        catch { }
    }
}

public class HistoryItem
{
    public string StartTime { get; set; } = "";
    public string EndTime { get; set; } = "";
    public string Status { get; set; } = "";
    public int? ExitCode { get; set; }
    public string ErrorMessage { get; set; } = "";
}
