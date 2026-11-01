using System.Collections.ObjectModel;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Text;
using System.Text.Json;
using System.Text.Json.Serialization;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Data;
using System.Windows.Media;
using WinSheduler.Shared;
using WinSheduler.Shared.Models;

namespace WinSheduler.UI;

public partial class HistoryWindow : Window
{
    private static readonly CultureInfo Invariant = CultureInfo.InvariantCulture;
    private static readonly JsonSerializerOptions JsonOpts = new()
    {
        Converters = { new JsonStringEnumConverter() }
    };

    private readonly IpcClient _ipc;
    private readonly Guid _taskId;
    private readonly string _taskName;
    private readonly ObservableCollection<HistoryRow> _visible = [];
    private List<HistoryRow> _all = [];

    public HistoryWindow(IpcClient ipc, Guid taskId, string taskName)
    {
        InitializeComponent();
        _ipc = ipc;
        _taskId = taskId;
        _taskName = taskName;

        var area = SystemParameters.WorkArea;
        MaxWidth = area.Width;
        MaxHeight = area.Height;
        Width = Math.Min(Width, Math.Max(MinWidth, area.Width - 32));
        Height = Math.Min(Height, Math.Max(MinHeight, area.Height - 32));

        Title = $"Журнал запусков: {taskName}";
        HistoryList.ItemsSource = _visible;

        StatusFilterBox.ItemsSource = new List<HistoryFilter>
        {
            new("Все статусы", null),
            new("Выполнено", RunStatus.Completed),
            new("Ошибка", RunStatus.Failed),
            new("Таймаут", RunStatus.Timeout),
            new("Прервано", RunStatus.Killed),
            new("Прервано при старте", RunStatus.Aborted),
            new("Выполняется", RunStatus.Running),
        };
        StatusFilterBox.SelectedIndex = 0;

        Loaded += async (_, _) => await LoadAsync();
    }

    private async Task LoadAsync()
    {
        RefreshBtn.IsEnabled = false;
        HideBanner();
        StatusLine.Text = "Загрузка...";

        try
        {
            var resp = await _ipc.SendAsync(new IpcRequest { Action = "GetHistory", Payload = _taskId.ToString() });
            if (!resp.Success)
            {
                ShowBanner($"Не удалось загрузить журнал: {resp.Error}");
                StatusLine.Text = "";
                return;
            }

            var history = JsonSerializer.Deserialize<List<RunHistory>>(resp.Payload, JsonOpts) ?? [];
            _all = history
                .Select(HistoryRow.FromHistory)
                .OrderByDescending(r => r.StartLocal)
                .ToList();

            ApplyFilter();
            ExportBtn.IsEnabled = _all.Count > 0;
        }
        catch (Exception ex)
        {
            ShowBanner($"Ошибка чтения журнала: {ex.Message}");
            StatusLine.Text = "";
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
            ? _all.Where(r => r.Status == status).ToList()
            : _all;

        _visible.Clear();
        foreach (var row in rows) _visible.Add(row);

        EmptyText.Text = _all.Count == 0
            ? "Запусков ещё не было."
            : "Нет записей с выбранным статусом.";
        EmptyText.Visibility = _visible.Count == 0 ? Visibility.Visible : Visibility.Collapsed;

        StatusLine.Text = $"Записей: {_visible.Count} · обновлено {DateTime.Now.ToString("dd.MM.yyyy HH:mm:ss", Invariant)}";
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
        if (e.Key == System.Windows.Input.Key.F5)
        {
            RefreshBtn_Click(sender, e);
            e.Handled = true;
        }
    }

    private void HistoryList_SelectionChanged(object sender, SelectionChangedEventArgs e)
    {
        OpenLogBtn.IsEnabled = HistoryList.SelectedItem is HistoryRow { HasOutput: true };
    }

    private void OpenLogBtn_Click(object sender, RoutedEventArgs e)
    {
        if (HistoryList.SelectedItem is not HistoryRow row || string.IsNullOrWhiteSpace(row.OutputPath))
            return;

        var path = row.OutputPath;

        // Открываем только файлы логов: служба строит путь сама, но клиент не доверяет формату.
        if (!path.EndsWith(".log", StringComparison.OrdinalIgnoreCase))
        {
            ShowBanner("Служба вернула недопустимый путь к файлу вывода.");
            return;
        }

        if (!File.Exists(path))
        {
            ShowBanner("Файл лога недоступен: он удалён или у вас нет прав на чтение.");
            return;
        }

        try
        {
            Process.Start(new ProcessStartInfo(path) { UseShellExecute = true });
            HideBanner();
        }
        catch (Exception ex)
        {
            ShowBanner($"Не удалось открыть лог: {ex.Message}");
        }
    }

    private void ExportBtn_Click(object sender, RoutedEventArgs e)
    {
        var dlg = new Microsoft.Win32.SaveFileDialog
        {
            Title = "Экспорт журнала",
            Filter = "CSV (*.csv)|*.csv",
            DefaultExt = ".csv",
            FileName = $"history-{SafeFileName(_taskName)}-{DateTime.Now.ToString("yyyyMMdd-HHmm", Invariant)}.csv"
        };

        if (dlg.ShowDialog() != true) return;

        var sb = new StringBuilder();
        sb.AppendLine("Начало;Конец;Статус;Код;Длительность;Ошибка");
        foreach (var row in _visible)
        {
            sb.AppendLine(string.Join(';',
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
            File.WriteAllText(dlg.FileName, sb.ToString(), new UTF8Encoding(true));
            HideBanner();
            StatusLine.Text = $"Экспортировано записей: {_visible.Count} · {DateTime.Now.ToString("dd.MM.yyyy HH:mm:ss", Invariant)}";
        }
        catch (Exception ex)
        {
            ShowBanner($"Не удалось сохранить CSV: {ex.Message}");
        }
    }

    private static string Csv(string? value)
    {
        var text = (value ?? "").Replace('\r', ' ').Replace('\n', ' ');
        return text.Contains(';') || text.Contains('"')
            ? "\"" + text.Replace("\"", "\"\"") + "\""
            : text;
    }

    private static string SafeFileName(string name)
    {
        var invalid = Path.GetInvalidFileNameChars();
        return string.Concat(name.Select(c => invalid.Contains(c) ? '_' : c));
    }

    private void ShowBanner(string message)
    {
        BannerText.Text = message;
        BannerText.Visibility = Visibility.Visible;
    }

    private void HideBanner()
    {
        BannerText.Text = "";
        BannerText.Visibility = Visibility.Collapsed;
    }
}

/// <summary>Строка журнала в локальном времени и с русскими подписями.</summary>
public sealed class HistoryRow
{
    private static readonly CultureInfo Invariant = CultureInfo.InvariantCulture;

    public DateTime StartLocal { get; init; }
    public DateTime? EndLocal { get; init; }
    public RunStatus Status { get; init; }
    public int? ExitCode { get; init; }
    public string ErrorMessage { get; init; } = "";
    public string? OutputPath { get; init; }

    public string StartText => StartLocal.ToString("dd.MM.yyyy HH:mm:ss", Invariant);
    public string EndText => EndLocal?.ToString("dd.MM.yyyy HH:mm:ss", Invariant) ?? "—";
    public string ExitCodeText => ExitCode?.ToString(Invariant) ?? "—";

    public string StatusText => Status switch
    {
        RunStatus.Running => "Выполняется",
        RunStatus.Completed => "Выполнено",
        RunStatus.Failed => "Ошибка",
        RunStatus.Killed => "Прервано",
        RunStatus.Timeout => "Таймаут",
        RunStatus.Aborted => "Прервано при старте",
        _ => Status.ToString()
    };

    public string DurationText
    {
        get
        {
            if (EndLocal is not { } end) return "—";
            var duration = end - StartLocal;
            return duration < TimeSpan.Zero ? "—" : duration.ToString(@"hh\:mm\:ss", Invariant);
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

/// <summary>Цвет статуса: успех/ошибка из токенов контракта UI, нейтральный — системный.</summary>
public sealed class HistoryStatusBrushConverter : IValueConverter
{
    private static readonly Brush SuccessBrush = new SolidColorBrush(Color.FromRgb(0x10, 0x7C, 0x10));
    private static readonly Brush ErrorBrush = new SolidColorBrush(Color.FromRgb(0xC4, 0x2B, 0x1C));

    public object Convert(object value, Type targetType, object parameter, CultureInfo culture)
    {
        if (value is not RunStatus status) return SystemColors.GrayTextBrush;

        return status switch
        {
            RunStatus.Completed => SuccessBrush,
            RunStatus.Failed or RunStatus.Timeout or RunStatus.Killed or RunStatus.Aborted => ErrorBrush,
            _ => SystemColors.GrayTextBrush
        };
    }

    public object ConvertBack(object value, Type targetType, object parameter, CultureInfo culture)
        => throw new NotImplementedException();
}
