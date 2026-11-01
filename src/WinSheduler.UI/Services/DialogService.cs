using Microsoft.Win32;
using WinSheduler.UI.Localization;

namespace WinSheduler.UI.Services;

/// <summary>Реализация <see cref="IDialogService"/> на стандартных диалогах Windows.</summary>
public sealed class DialogService : IDialogService
{
    /// <inheritdoc />
    public bool Confirm(string message, string? title = null)
        => System.Windows.MessageBox.Show(
            message,
            title ?? Strings.Get("Dialog_ConfirmTitle"),
            System.Windows.MessageBoxButton.YesNo,
            System.Windows.MessageBoxImage.Question,
            System.Windows.MessageBoxResult.No) == System.Windows.MessageBoxResult.Yes;

    /// <inheritdoc />
    public void ShowInfo(string message, string? title = null)
        => System.Windows.MessageBox.Show(
            message,
            title ?? Strings.Get("Dialog_InfoTitle"),
            System.Windows.MessageBoxButton.OK,
            System.Windows.MessageBoxImage.Information);

    /// <inheritdoc />
    public void ShowError(string message, string? title = null)
        => System.Windows.MessageBox.Show(
            message,
            title ?? Strings.Get("Dialog_ErrorTitle"),
            System.Windows.MessageBoxButton.OK,
            System.Windows.MessageBoxImage.Error);

    /// <inheritdoc />
    public string? PickFile(string filter, string? initialDirectory = null, string? title = null)
    {
        var dialog = new OpenFileDialog
        {
            Filter = filter,
            Title = title ?? Strings.Get("Dialog_BrowseProgram"),
            CheckFileExists = true,
        };

        if (!string.IsNullOrWhiteSpace(initialDirectory) && System.IO.Directory.Exists(initialDirectory))
        {
            dialog.InitialDirectory = initialDirectory;
        }

        return dialog.ShowDialog() == true ? dialog.FileName : null;
    }

    /// <inheritdoc />
    public string? PickFolder(string? initialDirectory = null, string? title = null)
    {
        var dialog = new OpenFolderDialog
        {
            Title = title ?? Strings.Get("Dialog_BrowseFolder"),
        };

        if (!string.IsNullOrWhiteSpace(initialDirectory) && System.IO.Directory.Exists(initialDirectory))
        {
            dialog.InitialDirectory = initialDirectory;
        }

        return dialog.ShowDialog() == true ? dialog.FolderName : null;
    }
}
