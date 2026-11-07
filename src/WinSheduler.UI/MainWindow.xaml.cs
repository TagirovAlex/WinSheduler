using System.ComponentModel;
using System.Diagnostics;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using Microsoft.Win32;
using WinSheduler.Shared;
using WinSheduler.UI.Localization;
using WinSheduler.UI.Services;
using WinSheduler.UI.ViewModels;

namespace WinSheduler.UI;

public partial class MainWindow : Window
{
    private const string HelpUrl = "https://github.com/TagirovAlex/WinSheduler#readme";

    private readonly AppServices? _services;
    private readonly DashboardViewModel? _viewModel;
    private readonly TaskExchangeService? _exchange;

    public MainWindow()
    {
        InitializeComponent();

        // В XAML-дизайнере не выполняем рантайм-инициализацию и не трогаем службу.
        if (DesignerProperties.GetIsInDesignMode(this))
        {
            return;
        }

        ApplyWorkAreaLimits();

        _services = App.Services;
        if (_services is null)
        {
            // Конструктор без композиционного корня (дизайнер, тесты) — пустое окно.
            return;
        }

        _viewModel = _services.CreateDashboardViewModel();
        _exchange = new TaskExchangeService(_services.Ipc, _services.Logger);
        DataContext = _viewModel;
        _viewModel.PropertyChanged += ViewModel_PropertyChanged;
        _viewModel.RefreshCompleted += ViewModel_RefreshCompleted;
        _viewModel.RunFinished += ViewModel_RunFinished;

        Loaded += MainWindow_Loaded;
        Closed += MainWindow_Closed;
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

    private async void MainWindow_Loaded(object sender, RoutedEventArgs e)
    {
        if (_viewModel is null) return;

        await _viewModel.RefreshStatusAsync();
        await _viewModel.RefreshAsync();
    }

    private void MainWindow_Closed(object? sender, EventArgs e)
    {
        if (_viewModel is null) return;

        _viewModel.StopFollowing();
        _viewModel.PropertyChanged -= ViewModel_PropertyChanged;
        _viewModel.RefreshCompleted -= ViewModel_RefreshCompleted;
        _viewModel.RunFinished -= ViewModel_RunFinished;
    }

    private void ViewModel_PropertyChanged(object? sender, PropertyChangedEventArgs e) => UpdateUi();

    private void ViewModel_RefreshCompleted(object? sender, EventArgs e)
    {
        HideActionError();
        UpdateUi();
    }

    /// <summary>Сопровождаемый запуск завершился: обновляем статус-бар или баннер ошибки.</summary>
    private void ViewModel_RunFinished(object? sender, RunFinishedEventArgs e)
    {
        if (e.RunStatus == RunStatus.Completed)
        {
            HideActionError();
            StatusSummaryText.Text = Strings.Format("Main_RunCompleted", e.Name);
            return;
        }

        var code = e.ExitCode?.ToString(Strings.Culture) ?? Strings.Get("Common_Dash");
        ShowActionError(Strings.Format("Main_RunFailed", e.Name, code));
    }

    private void UpdateUi()
    {
        if (_viewModel is null) return;

        var status = _viewModel.Status;
        var serviceState = _viewModel.IsServiceUnavailable
            ? Strings.Get("Main_StatusServiceUnavailable")
            : status?.Running == true
                ? Strings.Get("Main_StatusServiceRunning")
                : Strings.Get("Main_StatusServiceStopped");

        var parts = new List<string>
        {
            serviceState,
            _viewModel.RoleText,
            Strings.Format("Main_StatusTasks", _viewModel.TaskCount),
        };

        if (_viewModel.LastRefreshedAt is { } refreshed)
        {
            parts.Add(Strings.Format(
                "Main_StatusLastRefresh",
                refreshed.ToLocalTime().ToString("dd.MM.yyyy HH:mm:ss", Strings.Culture)));
        }

        StatusSummaryText.Text = string.Join(" · ", parts.Where(part => !string.IsNullOrWhiteSpace(part)));

        var hasSelection = _viewModel.SelectedTask is not null;
        var canAct = hasSelection && !_viewModel.IsLoading && !_viewModel.IsServiceUnavailable;

        AddBtn.IsEnabled = !_viewModel.IsLoading;
        EditBtn.IsEnabled = canAct;
        DeleteBtn.IsEnabled = canAct;
        HistoryBtn.IsEnabled = canAct;
        RunNowBtn.IsEnabled = canAct;
        ToggleBtn.IsEnabled = canAct;
        ExportBtn.IsEnabled = !_viewModel.IsLoading
            && !_viewModel.IsServiceUnavailable
            && TaskGrid.SelectedItems.Count > 0;
        ImportBtn.IsEnabled = !_viewModel.IsLoading && !_viewModel.IsServiceUnavailable;

        UpdateEmptyState();
    }

    private void UpdateEmptyState()
    {
        if (_viewModel is null) return;

        var empty = !_viewModel.IsLoading && !_viewModel.IsServiceUnavailable && _viewModel.VisibleTaskCount == 0;
        EmptyState.Visibility = empty ? Visibility.Visible : Visibility.Collapsed;
        if (!empty) return;

        var nothingFound = _viewModel.TaskCount > 0;
        EmptyTitle.Text = Strings.Get(nothingFound ? "Main_EmptyNoMatchesTitle" : "Main_EmptyTitle");
        EmptyHint.Text = Strings.Get(nothingFound ? "Main_EmptyNoMatchesHint" : "Main_EmptyHint");
        EmptyAddBtn.Visibility = nothingFound ? Visibility.Collapsed : Visibility.Visible;
    }

    private async void AddBtn_Click(object sender, RoutedEventArgs e) => await OpenEditorAsync(null);

    private async void EditBtn_Click(object sender, RoutedEventArgs e)
    {
        if (_viewModel?.SelectedTask is not { } task) return;
        await OpenEditorAsync(task);
    }

    private async void TaskGrid_MouseDoubleClick(object sender, MouseButtonEventArgs e)
    {
        if (_viewModel?.SelectedTask is not { } task) return;
        await OpenEditorAsync(task);
    }

    private void TaskGrid_SelectionChanged(object sender, SelectionChangedEventArgs e)
        => UpdateUi();

    private async Task OpenEditorAsync(TaskListItemViewModel? task)
    {
        if (_services is null) return;

        var editor = new TaskEditorWindow(_services.Ipc, task?.Id, task?.Name) { Owner = this };
        if (editor.ShowDialog() == true)
        {
            await _viewModel!.RefreshAsync();
            await _viewModel.RefreshStatusAsync();
        }
    }

    private async void DeleteBtn_Click(object sender, RoutedEventArgs e)
    {
        if (_services is null || _viewModel?.SelectedTask is not { } task) return;

        if (!_services.Dialogs.Confirm(Strings.Format("Dialog_DeleteTask", task.Name))) return;

        try
        {
            await _services.Ipc.DeleteTaskAsync(task.Id);
            await _viewModel.RefreshAsync();
        }
        catch (IpcException ex)
        {
            ShowActionError(IpcErrorText.Describe(ex));
        }
        catch (Exception ex)
        {
            ShowActionError(IpcErrorText.Describe(ex));
        }
    }

    private async void RunNowBtn_Click(object sender, RoutedEventArgs e)
    {
        if (_services is null || _viewModel?.SelectedTask is not { } task) return;

        try
        {
            await _services.Ipc.RunNowAsync(task.Id);
            StatusSummaryText.Text = Strings.Format("Main_RunStarted", task.Name);
            _viewModel.TrackRun(task.Id);
            await _viewModel.RefreshSilentlyAsync();
        }
        catch (IpcException ex)
        {
            ShowActionError(IpcErrorText.Describe(ex));
        }
        catch (Exception ex)
        {
            ShowActionError(IpcErrorText.Describe(ex));
        }
    }

    private async void ToggleBtn_Click(object sender, RoutedEventArgs e)
    {
        if (_services is null || _viewModel?.SelectedTask is not { } task) return;

        try
        {
            await _services.Ipc.SetTaskEnabledAsync(task.Id, !task.Enabled);
            await _viewModel.RefreshAsync();
        }
        catch (IpcException ex)
        {
            ShowActionError(IpcErrorText.Describe(ex));
        }
        catch (Exception ex)
        {
            ShowActionError(IpcErrorText.Describe(ex));
        }
    }

    private void HistoryBtn_Click(object sender, RoutedEventArgs e)
    {
        if (_services is null || _viewModel?.SelectedTask is not { } task) return;

        var history = new HistoryWindow(_services.Ipc, task.Id, task.Name) { Owner = this };
        history.ShowDialog();
    }

    private async void ExportBtn_Click(object sender, RoutedEventArgs e)
    {
        if (_services is null || _exchange is null) return;

        var taskIds = TaskGrid.SelectedItems
            .OfType<TaskListItemViewModel>()
            .Select(static task => task.Id)
            .ToList();
        if (taskIds.Count == 0)
        {
            ShowActionError(Strings.Get("Main_ExportNoSelection"));
            return;
        }

        var dialog = new SaveFileDialog
        {
            Title = Strings.Get("Main_ExportTitle"),
            Filter = Strings.Get("Main_ExchangeFilter"),
            DefaultExt = ".wstasks.json",
            AddExtension = true,
            FileName = $"winsheduler-tasks-{DateTime.Now:yyyyMMdd-HHmm}.wstasks.json",
        };
        if (dialog.ShowDialog(this) != true) return;

        try
        {
            await _exchange.ExportAsync(taskIds, dialog.FileName);
            HideActionError();
            StatusSummaryText.Text = Strings.Format("Main_ExportSuccess", taskIds.Count);
        }
        catch (Exception ex)
        {
            ShowActionError(IpcErrorText.Describe(ex));
        }
    }

    private async void ImportBtn_Click(object sender, RoutedEventArgs e)
    {
        if (_services is null || _exchange is null) return;

        var dialog = new OpenFileDialog
        {
            Title = Strings.Get("Main_ImportTitle"),
            Filter = Strings.Get("Main_ExchangeFilter"),
            CheckFileExists = true,
        };
        if (dialog.ShowDialog(this) != true) return;

        try
        {
            var preview = await _exchange.PreviewAsync(dialog.FileName);
            if (preview.Tasks.Count == 0)
            {
                _services.Dialogs.ShowInfo(Strings.Get("Main_ImportEmpty"));
                return;
            }

            var window = new ImportPreviewWindow(_exchange, preview) { Owner = this };
            if (window.ShowDialog() != true || window.Report is not { } report)
            {
                return;
            }

            HideActionError();
            StatusSummaryText.Text = Strings.Format(
                "Main_ImportSuccess",
                report.Imported,
                report.Skipped,
                report.Updated,
                report.Failed);
        }
        catch (Exception ex)
        {
            ShowActionError(IpcErrorText.Describe(ex));
            return;
        }

        await _viewModel!.RefreshAsync();
        await _viewModel.RefreshStatusAsync();
    }

    private async void RefreshMenu_Click(object sender, RoutedEventArgs e)
    {
        if (_viewModel is not null) await _viewModel.RefreshAsync();
    }

    private void ResetFilter_Click(object sender, RoutedEventArgs e)
    {
        if (_viewModel is null) return;

        _viewModel.SearchText = string.Empty;
        _viewModel.Filter = TaskFilter.All;
    }

    private void ExitMenu_Click(object sender, RoutedEventArgs e) => Close();

    private void HelpMenu_Click(object sender, RoutedEventArgs e)
    {
        try
        {
            Process.Start(new ProcessStartInfo(HelpUrl) { UseShellExecute = true });
        }
        catch (Exception ex)
        {
            ShowActionError(Strings.Format("Menu_HelpError", ex.Message));
        }
    }

    private void AboutMenu_Click(object sender, RoutedEventArgs e)
    {
        if (_services is null) return;

        var version = typeof(MainWindow).Assembly.GetName().Version?.ToString(3) ?? "2.1.0";
        _services.Dialogs.ShowInfo(
            Strings.Format("About_Message", version),
            Strings.Get("About_Title"));
    }

    private void OpenServicesBtn_Click(object sender, RoutedEventArgs e)
    {
        try
        {
            Process.Start(new ProcessStartInfo("services.msc") { UseShellExecute = true });
        }
        catch (Exception ex)
        {
            ShowActionError(Strings.Format("Main_OpenServicesError", ex.Message));
        }
    }

    private void DiagnosticsBtn_Click(object sender, RoutedEventArgs e)
    {
        if (_services is null) return;

        var window = new DiagnosticsWindow(_services.Ipc, _services.Elevation) { Owner = this };
        window.ShowDialog();
    }

    private void RelaunchBtn_Click(object sender, RoutedEventArgs e)
    {
        if (_services is null) return;

        if (!_services.Elevation.RelaunchAsAdmin())
        {
            ShowActionError(Strings.Get("Admin_RelaunchFailed"));
        }
    }

    private void ShowActionError(string message)
    {
        ActionBannerText.Text = message;
        ActionBanner.Visibility = Visibility.Visible;
    }

    private void HideActionError()
    {
        ActionBannerText.Text = string.Empty;
        ActionBanner.Visibility = Visibility.Collapsed;
    }

    /// <summary>Горячие клавиши из <see cref="ShortcutCatalog"/> (контракт UI.md, раздел 3).</summary>
    private void Window_PreviewKeyDown(object sender, KeyEventArgs e)
    {
        // В поле поиска Enter и Delete обрабатывает сам TextBox.
        if (e.OriginalSource is System.Windows.Controls.TextBox &&
            e.Key is Key.Enter or Key.Delete)
        {
            return;
        }

        if (Matches(e, ShortcutCatalog.Refresh))
        {
            if (_viewModel is not null) _ = _viewModel.RefreshAsync();
            e.Handled = true;
        }
        else if (Matches(e, ShortcutCatalog.Help))
        {
            HelpMenu_Click(sender, e);
            e.Handled = true;
        }
        else if (Matches(e, ShortcutCatalog.AddTask))
        {
            AddBtn_Click(sender, e);
            e.Handled = true;
        }
        else if (Matches(e, ShortcutCatalog.EditTask))
        {
            EditBtn_Click(sender, e);
            e.Handled = true;
        }
        else if (Matches(e, ShortcutCatalog.DeleteTask))
        {
            DeleteBtn_Click(sender, e);
            e.Handled = true;
        }
        else if (Matches(e, ShortcutCatalog.RunNow))
        {
            RunNowBtn_Click(sender, e);
            e.Handled = true;
        }
    }

    private static bool Matches(KeyEventArgs e, KeyGesture gesture)
        => e.Key == gesture.Key && Keyboard.Modifiers == gesture.Modifiers;
}
