using System.ComponentModel;
using System.Diagnostics;
using System.Windows;
using System.Windows.Input;
using WinSheduler.UI.Localization;
using WinSheduler.UI.Services;
using WinSheduler.UI.ViewModels;

namespace WinSheduler.UI;

public partial class MainWindow : Window
{
    private readonly AppServices? _services;
    private readonly DashboardViewModel? _viewModel;

    public MainWindow()
    {
        InitializeComponent();
        ApplyWorkAreaLimits();

        _services = App.Services;
        if (_services is null)
        {
            // Конструктор без композиционного корня (дизайнер, тесты) — пустое окно.
            return;
        }

        _viewModel = _services.CreateDashboardViewModel();
        DataContext = _viewModel;
        _viewModel.PropertyChanged += ViewModel_PropertyChanged;
        _viewModel.RefreshCompleted += ViewModel_RefreshCompleted;

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

        _viewModel.PropertyChanged -= ViewModel_PropertyChanged;
        _viewModel.RefreshCompleted -= ViewModel_RefreshCompleted;
    }

    private void ViewModel_PropertyChanged(object? sender, PropertyChangedEventArgs e) => UpdateUi();

    private void ViewModel_RefreshCompleted(object? sender, EventArgs e)
    {
        HideActionError();
        UpdateUi();
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

    private async void RefreshMenu_Click(object sender, RoutedEventArgs e)
    {
        if (_viewModel is not null) await _viewModel.RefreshAsync();
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
