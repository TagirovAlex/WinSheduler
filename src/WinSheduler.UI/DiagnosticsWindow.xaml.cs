using System.Globalization;
using System.Text;
using System.Windows;
using System.Windows.Media;
using WinSheduler.Shared;
using WinSheduler.UI.Localization;
using WinSheduler.UI.Services;

namespace WinSheduler.UI;

public partial class DiagnosticsWindow : Window
{
    private static readonly CultureInfo Invariant = CultureInfo.InvariantCulture;

    private readonly IIpcService _ipc;
    private readonly IElevationService _elevation;
    private ServiceStatus? _status;
    private bool _connected;

    public DiagnosticsWindow(IIpcService ipc, IElevationService elevation)
    {
        InitializeComponent();
        _ipc = ipc ?? throw new ArgumentNullException(nameof(ipc));
        _elevation = elevation ?? throw new ArgumentNullException(nameof(elevation));

        MaxHeight = Math.Max(360, SystemParameters.WorkArea.Height - 24);
        UiVersionText.Text = typeof(DiagnosticsWindow).Assembly.GetName().Version?.ToString(3)
                             ?? Strings.Get("Common_Dash");

        AuditFromPicker.SelectedDate = DateTime.Today.AddDays(-1);
        AuditToPicker.SelectedDate = DateTime.Today;

        Loaded += async (_, _) => await RefreshAsync();
    }

    private async Task RefreshAsync()
    {
        RefreshBtn.IsEnabled = false;
        Banner.Visibility = Visibility.Collapsed;
        ConnectionText.Text = Strings.Get("Status_Loading");
        ConnectionText.Foreground = (Brush)FindResource("App_SecondaryTextBrush");

        try
        {
            _status = await _ipc.GetStatusAsync();
            _elevation.Update(_status);
            _connected = _status.Running;
        }
        catch (IpcException)
        {
            _status = null;
            _connected = false;
        }
        catch (Exception)
        {
            _status = null;
            _connected = false;
        }
        finally
        {
            RefreshBtn.IsEnabled = true;
        }

        Render();
    }

    private void Render()
    {
        var successBrush = (Brush)FindResource("Status_SuccessBrush");
        var errorBrush = (Brush)FindResource("Status_ErrorBrush");

        if (_connected && _status is not null)
        {
            ConnectionText.Text = Strings.Get("Diagnostics_Connected");
            ConnectionText.Foreground = successBrush;
            ServiceStateText.Text = Strings.Get("Diagnostics_ServiceRunning");
            ServiceVersionText.Text = string.IsNullOrWhiteSpace(_status.ServiceVersion)
                ? Strings.Get("Diagnostics_NotReported")
                : _status.ServiceVersion;
            TaskCountText.Text = $"{_status.TaskCount} / {_status.RunningTaskCount}";
            RoleText.Text = _elevation.RoleText;
            UptimeText.Text = FormatUptime(_status.StartedAt);
            Banner.Visibility = Visibility.Collapsed;
        }
        else
        {
            ConnectionText.Text = Strings.Get("Diagnostics_Disconnected");
            ConnectionText.Foreground = errorBrush;
            ServiceStateText.Text = Strings.Get("Diagnostics_ServiceStopped");
            ServiceVersionText.Text = Strings.Get("Common_Dash");
            UptimeText.Text = Strings.Get("Common_Dash");
            TaskCountText.Text = Strings.Get("Common_Dash");
            RoleText.Text = Strings.Get("Common_Dash");

            BannerText.Text = Strings.Get("Diagnostics_UnavailableBanner");
            Banner.Visibility = Visibility.Visible;
        }

        var isAdmin = _elevation.IsAdmin;
        PathsGroup.Visibility = isAdmin ? Visibility.Visible : Visibility.Collapsed;
        PathsText.Text = Strings.Get("Diagnostics_PathsUnavailable");

        AuditGroup.Visibility = isAdmin ? Visibility.Visible : Visibility.Collapsed;
        if (isAdmin)
        {
            AuditStatusText.Text = Strings.Get("Diagnostics_AuditHint");
        }
    }

    private static string FormatUptime(DateTime startedAt)
    {
        if (startedAt == default) return Strings.Get("Common_Dash");

        var started = startedAt.Kind switch
        {
            DateTimeKind.Utc => startedAt.ToLocalTime(),
            DateTimeKind.Unspecified => DateTime.SpecifyKind(startedAt, DateTimeKind.Utc).ToLocalTime(),
            _ => startedAt
        };

        var uptime = DateTime.Now - started;
        if (uptime < TimeSpan.Zero) return Strings.Get("Common_Dash");

        return uptime.Days > 0
            ? $"{uptime.Days} {Strings.Get("Time_DurationDays")} {uptime.ToString(@"hh\:mm\:ss", Invariant)}"
            : uptime.ToString(@"hh\:mm\:ss", Invariant);
    }

    private async void RefreshBtn_Click(object sender, RoutedEventArgs e)
    {
        await RefreshAsync();
    }

    private async void AuditLoadBtn_Click(object sender, RoutedEventArgs e)
    {
        var from = AuditFromPicker.SelectedDate ?? DateTime.Today.AddDays(-1);
        var to = (AuditToPicker.SelectedDate ?? DateTime.Today).AddDays(1);

        AuditLoadBtn.IsEnabled = false;
        AuditStatusText.Text = Strings.Get("Common_Loading");
        AuditGrid.ItemsSource = null;

        try
        {
            var entries = await _ipc.GetAuditAsync(new DateTimeOffset(from), new DateTimeOffset(to), 500);
            AuditGrid.ItemsSource = entries;
            AuditStatusText.Text = entries.Count == 0
                ? Strings.Get("Diagnostics_AuditEmpty")
                : Strings.Format("Diagnostics_AuditCount", entries.Count);
        }
        catch (IpcException ex)
        {
            AuditStatusText.Text = Strings.Format("Diagnostics_AuditError", IpcErrorText.Describe(ex));
        }
        catch (Exception ex)
        {
            AuditStatusText.Text = Strings.Format("Diagnostics_AuditError", IpcErrorText.Describe(ex));
        }
        finally
        {
            AuditLoadBtn.IsEnabled = true;
        }
    }

    private void CopyBtn_Click(object sender, RoutedEventArgs e)
    {
        var builder = new StringBuilder();
        builder.AppendLine(Strings.Get("Diagnostics_Title"));
        builder.AppendLine($"{Strings.Get("Diagnostics_Connection")} {ConnectionText.Text}");
        builder.AppendLine($"{Strings.Get("Diagnostics_Service")} {ServiceStateText.Text}");
        builder.AppendLine($"{Strings.Get("Diagnostics_ServiceVersion")} {ServiceVersionText.Text}");
        builder.AppendLine($"{Strings.Get("Diagnostics_UiVersion")} {UiVersionText.Text}");
        builder.AppendLine($"{Strings.Get("Diagnostics_Uptime")} {UptimeText.Text}");
        builder.AppendLine($"{Strings.Get("Diagnostics_Tasks")} {TaskCountText.Text}");
        builder.AppendLine($"{Strings.Get("Diagnostics_Role")} {RoleText.Text}");

        try
        {
            Clipboard.SetText(builder.ToString());
        }
        catch (Exception ex)
        {
            BannerText.Text = Strings.Format("Diagnostics_CopyError", ex.Message);
            Banner.Visibility = Visibility.Visible;
        }
    }

    private void CloseBtn_Click(object sender, RoutedEventArgs e) => Close();
}
