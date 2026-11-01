using System.Globalization;
using System.Text;
using System.Text.Json;
using System.Windows;
using System.Windows.Media;
using WinSheduler.Shared;

namespace WinSheduler.UI;

public partial class DiagnosticsWindow : Window
{
    private static readonly CultureInfo Invariant = CultureInfo.InvariantCulture;
    private static readonly Brush SuccessBrush = new SolidColorBrush(Color.FromRgb(0x10, 0x7C, 0x10));
    private static readonly Brush ErrorBrush = new SolidColorBrush(Color.FromRgb(0xC4, 0x2B, 0x1C));

    private readonly IpcClient _ipc;
    private ServiceStatus? _status;
    private bool _connected;
    private string? _lastError;

    public DiagnosticsWindow(IpcClient ipc)
    {
        InitializeComponent();
        _ipc = ipc;
        MaxHeight = Math.Max(360, SystemParameters.WorkArea.Height - 24);

        UiVersionText.Text = typeof(DiagnosticsWindow).Assembly.GetName().Version?.ToString(3) ?? "—";

        Loaded += async (_, _) => await RefreshAsync();
    }

    private async Task RefreshAsync()
    {
        RefreshBtn.IsEnabled = false;
        BannerText.Visibility = Visibility.Collapsed;
        ConnectionText.Text = "Проверка...";
        ConnectionText.Foreground = SystemColors.GrayTextBrush;

        try
        {
            var resp = await _ipc.SendAsync(new IpcRequest { Action = "GetStatus" });
            if (resp.Success)
            {
                _status = JsonSerializer.Deserialize<ServiceStatus>(resp.Payload);
                _connected = _status?.Running == true;
                _lastError = null;
            }
            else
            {
                _status = null;
                _connected = false;
                _lastError = resp.Error;
            }
        }
        catch (Exception ex)
        {
            _status = null;
            _connected = false;
            _lastError = ex.Message;
        }
        finally
        {
            RefreshBtn.IsEnabled = true;
        }

        Render();
    }

    private void Render()
    {
        if (_connected && _status != null)
        {
            ConnectionText.Text = "Подключено";
            ConnectionText.Foreground = SuccessBrush;
            ServiceStateText.Text = "Работает";
            ServiceVersionText.Text = string.IsNullOrWhiteSpace(_status.ServiceVersion) ? "не сообщается" : _status.ServiceVersion;
            TaskCountText.Text = $"{_status.TaskCount} / {_status.RunningTaskCount}";
            RoleText.Text = _status.IsAdmin
                ? "Администратор"
                : _status.IsOperator ? "Оператор" : "не сообщается службой";
            UptimeText.Text = FormatUptime(_status.StartedAt);
            BannerText.Visibility = Visibility.Collapsed;
            return;
        }

        ConnectionText.Text = "Служба недоступна";
        ConnectionText.Foreground = ErrorBrush;
        ServiceStateText.Text = "Остановлена или не установлена";
        ServiceVersionText.Text = "—";
        UptimeText.Text = "—";
        TaskCountText.Text = "—";
        RoleText.Text = "—";

        BannerText.Text =
            "Не удалось получить состояние службы" +
            (string.IsNullOrWhiteSpace(_lastError) ? "." : $": {_lastError}") +
            "\nЗапустите службу «WinSheduler» через оснастку «Службы» или командой " +
            "«sc start WinSheduler» от имени администратора.";
        BannerText.Visibility = Visibility.Visible;
    }

    private static string FormatUptime(DateTime startedAt)
    {
        if (startedAt == default) return "—";

        var started = startedAt.Kind switch
        {
            DateTimeKind.Utc => startedAt.ToLocalTime(),
            DateTimeKind.Unspecified => DateTime.SpecifyKind(startedAt, DateTimeKind.Utc).ToLocalTime(),
            _ => startedAt
        };

        var uptime = DateTime.Now - started;
        if (uptime < TimeSpan.Zero) return "—";

        return uptime.Days > 0
            ? $"{uptime.Days} д {uptime.ToString(@"hh\:mm\:ss", Invariant)}"
            : uptime.ToString(@"hh\:mm\:ss", Invariant);
    }

    private async void RefreshBtn_Click(object sender, RoutedEventArgs e)
    {
        await RefreshAsync();
    }

    private void CopyBtn_Click(object sender, RoutedEventArgs e)
    {
        var sb = new StringBuilder();
        sb.AppendLine("WinSheduler — диагностика");
        sb.AppendLine($"Дата: {DateTime.Now.ToString("dd.MM.yyyy HH:mm:ss", Invariant)}");
        sb.AppendLine($"Подключение: {ConnectionText.Text}");
        sb.AppendLine($"Служба: {ServiceStateText.Text}");
        sb.AppendLine($"Версия службы: {ServiceVersionText.Text}");
        sb.AppendLine($"Версия интерфейса: {UiVersionText.Text}");
        sb.AppendLine($"Время работы службы: {UptimeText.Text}");
        sb.AppendLine($"Задач / выполняется: {TaskCountText.Text}");
        sb.AppendLine($"Роль: {RoleText.Text}");

        try
        {
            Clipboard.SetText(sb.ToString());
        }
        catch (Exception ex)
        {
            BannerText.Text = $"Не удалось скопировать сведения: {ex.Message}";
            BannerText.Visibility = Visibility.Visible;
        }
    }

    private void CloseBtn_Click(object sender, RoutedEventArgs e)
    {
        Close();
    }
}
