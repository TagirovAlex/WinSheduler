using System.Collections.ObjectModel;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Text;
using System.Windows;
using System.Windows.Controls;
using WinSheduler.Shared;
using WinSheduler.Shared.Models;
using WinSheduler.UI.Localization;
using WinSheduler.UI.Services;

namespace WinSheduler.UI;

public partial class HistoryWindow : Window
{
    private static readonly CultureInfo Invariant = CultureInfo.InvariantCulture;

    private readonly IIpcService _ipc;
    private readonly Guid _taskId;
    private readonly string _taskName;
    private readonly ObservableCollection<HistoryRow> _visible = [];
    private List<HistoryRow> _all = [];

    public HistoryWindow(IIpcService ipc, Guid taskId, string taskName)
    {
        InitializeComponent();
        _ipc = ipc ?? throw new ArgumentNullException(nameof(ipc));
        _taskId = taskId;
        _taskName = taskName;

        var area = SystemParameters.WorkArea;
        MaxWidth = area.Width;
        MaxHeight = area.Height;
        Width = Math.Min(Width, Math.Max(MinWidth, area.Width - 32));
        Height = Math.Min(Height, Math.Max(MinHeight, area.Height - 32));

        Title = Strings.Format("History_TitleFormat", taskName);
        HistoryGrid.ItemsSource = _visible;

        StatusFilterBox.ItemsSource = new List<HistoryFilter>
        {
            new(Strings.Get("History_FilterAll"), null),
            new(Strings.Get("History_StatusCompleted"), RunStatus.Completed),
            new(Strings.Get("History_StatusFailed"), RunStatus.Failed),
            new(Strings.Get("History_StatusTimeout"), RunStatus.Timeout),
            new(Strings.Get("History_StatusKilled"), RunStatus.Killed),
            new(Strings.Get("History_StatusAborted"), RunStatus.Aborted),
            new(Strings.Get("History_StatusRunning"), RunStatus.Running),
        };
        StatusFilterBox.SelectedIndex = 0;

        Loaded += async (_, _) => await LoadAsync();
    }

    private async Task LoadAsync()
    {
        RefreshBtn.IsEnabled = false;
        HideBanner();
        StatusLine.Text = Strings.Get("Common_Loading");

        try
        {
            var history = await _ipc.GetHistoryAsync(_taskId, 1000);
            _all = history
                .Select(HistoryRow.FromHistory)
                .OrderByDescending(row => row.StartLocal)
                .ToList();

            ApplyFilter();
            ExportBtn.IsEnabled = _all.Count > 0;
        }
        catch (IpcException ex)
        {
            ShowBanner(IpcErrorText.Describe(ex));
            StatusLine.Text = string.Empty;
        }
        catch (Exception ex)
        {
            ShowBanner(IpcErrorText.Describe(ex));
            StatusLine.Text = string.Empty;
        }
        finally
        {
            RefreshBtn.IsEnabled = true;
        }
    }

    private void ApplyFilter()
    {
        var filter = StatusFilterBox.SelectedItem as HistoryFilter;
        var rows = filter?.Status is { } status
            ? _all.Where(row => row.Status == status).ToList()
            : _all;

        _visible.Clear();
        foreach (var row in rows) _visible.Add(row);

        EmptyText.Text = Strings.Get(_all.Count == 0
            ? "History_Empty"
            : "History_EmptyFiltered");
        EmptyText.Visibility = _visible.Count == 0 ? Visibility.Visible : Visibility.Collapsed;

        StatusLine.Text = Strings.Format(
            "History_Summary",
            _visible.Count,
            DateTime.Now.ToString("dd.MM.yyyy HH:mm:ss", Strings.Culture));
    }

    private void StatusFilterBox_SelectionChanged(object sender, SelectionChangedEventArgs e)
    {
        if (IsLoaded) ApplyFilter();
    }

    private async void RefreshBtn_Click(object sender, RoutedEventArgs e)
    {
        await LoadAsync();
    }

    private void Window_PreviewKeyDown(object sender, System.Windows.Input.KeyEventArgs e)
    {
        if (e.Key != System.Windows.Input.Key.F5) return;

        RefreshBtn_Click(sender, e);
        e.Handled = true;
    }

    private void HistoryGrid_SelectionChanged(object sender, SelectionChangedEventArgs e)
    {
        OpenLogBtn.IsEnabled = HistoryGrid.SelectedItem is HistoryRow { HasOutput: true };
    }

    private void OpenLogBtn_Click(object sender, RoutedEventArgs e)
    {
        if (HistoryGrid.SelectedItem is not HistoryRow row || string.IsNullOrWhiteSpace(row.OutputPath))
            return;

        var path = row.OutputPath;

        // Открываем только файлы логов: служба строит путь сама, но клиент не доверяет формату.
        if (!path.EndsWith(".log", StringComparison.OrdinalIgnoreCase))
        {
            ShowBanner(Strings.Get("History_LogInvalidPath"));
            return;
        }

        if (!File.Exists(path))
        {
            ShowBanner(Strings.Get("History_LogMissing"));
            return;
        }

        try
        {
            Process.Start(new ProcessStartInfo(path) { UseShellExecute = true });
            HideBanner();
        }
        catch (Exception ex)
        {
            ShowBanner(Strings.Format("History_LogOpenError", ex.Message));
        }
    }

    private void ExportBtn_Click(object sender, RoutedEventArgs e)
    {
        var dialog = new Microsoft.Win32.SaveFileDialog
        {
            Title = Strings.Get("History_ExportTitle"),
            Filter = Strings.Get("History_CsvFilter"),
            DefaultExt = ".csv",
            FileName = $"history-{SafeFileName(_taskName)}-{DateTime.Now.ToString("yyyyMMdd-HHmm", Invariant)}.csv"
        };

        if (dialog.ShowDialog() != true) return;

        var builder = new StringBuilder();
        builder.AppendLine(string.Join(';',
            Strings.Get("History_ColumnStart"),
            Strings.Get("History_ColumnEnd"),
            Strings.Get("History_ColumnStatus"),
            Strings.Get("History_ColumnExitCode"),
            Strings.Get("History_ColumnDuration"),
            Strings.Get("History_ColumnError")));

        foreach (var row in _visible)
        {
            builder.AppendLine(string.Join(';',
                Csv(row.StartText),
                Csv(row.EndText),
                Csv(row.StatusText),
                Csv(row.ExitCodeText),
                Csv(row.DurationText),
                Csv(row.ErrorMessage)));
        }

        try
        {
            // BOM нужен Excel для корректного определения UTF-8.
            File.WriteAllText(dialog.FileName, builder.ToString(), new UTF8Encoding(true));
            HideBanner();
            StatusLine.Text = Strings.Format(
                "History_ExportSaved",
                _visible.Count,
                DateTime.Now.ToString("dd.MM.yyyy HH:mm:ss", Strings.Culture));
        }
        catch (Exception ex)
        {
            ShowBanner(Strings.Format("History_ExportError", ex.Message));
        }
    }

    private static string Csv(string? value)
    {
        var text = (value ?? string.Empty).Replace('\r', ' ').Replace('\n', ' ');
        return text.Contains(';') || text.Contains('"')
            ? "\"" + text.Replace("\"", "\"\"") + "\""
            : text;
    }

    private static string SafeFileName(string name)
    {
        var invalid = Path.GetInvalidFileNameChars();
        return string.Concat(name.Select(character => invalid.Contains(character) ? '_' : character));
    }

    private void ShowBanner(string message)
    {
        BannerText.Text = message;
        Banner.Visibility = Visibility.Visible;
    }

    private void HideBanner()
    {
        BannerText.Text = string.Empty;
        Banner.Visibility = Visibility.Collapsed;
    }
}

/// <summary>Строка журнала в локальном времени и с русскими подписями.</summary>
public sealed class HistoryRow
{
    public DateTime StartLocal { get; init; }
    public DateTime? EndLocal { get; init; }
    public RunStatus Status { get; init; }
    public int? ExitCode { get; init; }
    public string ErrorMessage { get; init; } = string.Empty;
    public string? OutputPath { get; init; }

    public string StartText => StartLocal.ToString("dd.MM.yyyy HH:mm:ss", Strings.Culture);
    public string EndText => EndLocal?.ToString("dd.MM.yyyy HH:mm:ss", Strings.Culture) ?? Strings.Get("Common_Dash");
    public string ExitCodeText => ExitCode?.ToString(Strings.Culture) ?? Strings.Get("Common_Dash");

    public string StatusText => Strings.Get(Status switch
    {
        RunStatus.Running => "History_StatusRunning",
        RunStatus.Completed => "History_StatusCompleted",
        RunStatus.Failed => "History_StatusFailed",
        RunStatus.Killed => "History_StatusKilled",
        RunStatus.Timeout => "History_StatusTimeout",
        RunStatus.Aborted => "History_StatusAborted",
        _ => "Common_Unknown"
    });

    public string DurationText
    {
        get
        {
            if (EndLocal is not { } end) return Strings.Get("Common_Dash");

            var duration = end - StartLocal;
            return duration < TimeSpan.Zero
                ? Strings.Get("Common_Dash")
                : duration.ToString(@"hh\:mm\:ss", Strings.Culture);
        }
    }

    public bool HasOutput => !string.IsNullOrWhiteSpace(OutputPath);

    public static HistoryRow FromHistory(RunHistory history) => new()
    {
        StartLocal = ToLocal(history.StartTime),
        EndLocal = history.EndTime is { } end ? ToLocal(end) : null,
        Status = history.Status,
        ExitCode = history.ExitCode,
        ErrorMessage = history.ErrorMessage,
        OutputPath = history.OutputPath,
    };

    /// <summary>Контракт IPC передаёт UTC; значения без пояса трактуем как UTC.</summary>
    private static DateTime ToLocal(DateTime value) => value.Kind switch
    {
        DateTimeKind.Utc => value.ToLocalTime(),
        DateTimeKind.Unspecified => DateTime.SpecifyKind(value, DateTimeKind.Utc).ToLocalTime(),
        _ => value
    };
}

public sealed record HistoryFilter(string Label, RunStatus? Status);
