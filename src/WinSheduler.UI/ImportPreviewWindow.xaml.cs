using System.Windows;
using System.Windows.Media;
using WinSheduler.Shared.Models;
using WinSheduler.UI.Localization;
using WinSheduler.UI.Services;

namespace WinSheduler.UI;

/// <summary>
/// Предпросмотр импорта: список задач, предупреждения и выбор режима.
/// Секреты в окне не отображаются — только факт наличия пароля.
/// </summary>
public partial class ImportPreviewWindow : Window
{
    private readonly TaskExchangeService _service;
    private readonly TaskImportPreview _preview;

    private CancellationTokenSource? _cancellation;
    private bool _busy;

    public ImportPreviewWindow(TaskExchangeService service, TaskImportPreview preview)
    {
        InitializeComponent();

        _service = service ?? throw new ArgumentNullException(nameof(service));
        _preview = preview ?? throw new ArgumentNullException(nameof(preview));

        var area = SystemParameters.WorkArea;
        MaxWidth = area.Width;
        MaxHeight = area.Height;
        Width = Math.Min(Width, Math.Max(MinWidth, area.Width - 32));
        Height = Math.Min(Height, Math.Max(MinHeight, area.Height - 32));

        Title = Strings.Get("Exchange_ImportTitle");
        SummaryText.Text = Strings.Format("Exchange_Summary", _preview.Tasks.Count);
        WarningsText.Text = _preview.Warnings.Count == 0
            ? Strings.Get("Exchange_NoWarnings")
            : string.Join(Environment.NewLine, _preview.Warnings.Select(static warning => $"• {warning}"));

        TasksGrid.ItemsSource = _preview.Tasks
            .Where(static task => task is not null)
            .Select(static task => new ImportPreviewRow
            {
                Name = task.Name,
                TriggerCount = task.TimeWindows.Count,
                Enabled = task.Enabled && !task.HasPassword,
                HasPassword = task.HasPassword,
            })
            .ToList();

        ModeBox.ItemsSource = new[]
        {
            new ModeOption(Strings.Get("Exchange_ModeCopy"), TaskImportMode.Copy),
            new ModeOption(Strings.Get("Exchange_ModeSkip"), TaskImportMode.Skip),
            new ModeOption(Strings.Get("Exchange_ModeOverwrite"), TaskImportMode.Overwrite),
        };
        ModeBox.SelectedIndex = 0;

        Closed += (_, _) => _cancellation?.Cancel();
    }

    /// <summary>Итоговый отчёт; заполняется после успешного импорта.</summary>
    public TaskImportReport? Report { get; private set; }

    private async void ImportBtn_Click(object sender, RoutedEventArgs e)
    {
        if (_busy || ModeBox.SelectedItem is not ModeOption mode)
        {
            return;
        }

        _busy = true;
        SetBusyState(true);
        ImportProgress.Visibility = Visibility.Visible;
        ImportProgress.Minimum = 0;
        ImportProgress.Maximum = Math.Max(1, _preview.Tasks.Count);
        ImportProgress.Value = 0;
        ShowStatus(Strings.Format("Exchange_Importing", 0, _preview.Tasks.Count), isError: false);

        _cancellation = new CancellationTokenSource();
        var processed = 0;
        var progress = new Progress<TaskImportItemResult>(_ =>
        {
            processed++;
            ImportProgress.Value = Math.Min(ImportProgress.Maximum, processed);
            ShowStatus(
                Strings.Format("Exchange_Importing", processed, _preview.Tasks.Count),
                isError: false);
        });

        try
        {
            Report = await _service.ImportAsync(
                _preview.FilePath,
                mode.Mode,
                progress,
                _cancellation.Token);

            DialogResult = true;
        }
        catch (OperationCanceledException)
        {
            ShowStatus(Strings.Get("Exchange_ImportCancelled"), isError: false);
            SetBusyState(false);
        }
        catch (Exception ex)
        {
            ShowStatus(
                Strings.Format("Exchange_ImportError", IpcErrorText.Describe(ex)),
                isError: true);
            SetBusyState(false);
        }
        finally
        {
            _cancellation?.Dispose();
            _cancellation = null;
            _busy = false;
        }
    }

    private void CancelBtn_Click(object sender, RoutedEventArgs e)
    {
        if (_busy)
        {
            _cancellation?.Cancel();
            CancelBtn.IsEnabled = false;
            return;
        }

        DialogResult = false;
    }

    private void SetBusyState(bool busy)
    {
        ImportBtn.IsEnabled = !busy;
        ModeBox.IsEnabled = !busy;
        CancelBtn.IsEnabled = true;
    }

    private void ShowStatus(string text, bool isError)
    {
        StatusText.Text = text;
        StatusText.Foreground = (Brush)FindResource(
            isError ? "Status_ErrorBrush" : "App_SecondaryTextBrush");
    }

    private sealed record ModeOption(string Label, TaskImportMode Mode);
}

/// <summary>Строка предпросмотра без секретов.</summary>
public sealed class ImportPreviewRow
{
    public string Name { get; init; } = "";
    public int TriggerCount { get; init; }
    public bool Enabled { get; init; }
    public bool HasPassword { get; init; }

    public string TriggersText => TriggerCount.ToString(Strings.Culture);

    public string EnabledText => Strings.Get(Enabled ? "Common_Yes" : "Common_No");

    public string PasswordText => Strings.Get(HasPassword ? "Common_Yes" : "Common_No");
}
