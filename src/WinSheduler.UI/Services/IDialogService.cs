namespace WinSheduler.UI.Services;

/// <summary>
/// Диалоги представления. ViewModel не работает с <see cref="System.Windows.MessageBox"/>
/// напрямую — это упрощает тесты и сохраняет MVVM.
///
/// Контракт UI.md: модальные сообщения — только для подтверждения разрушительных
/// действий; ошибки показываются баннером. <see cref="ShowError"/> оставлен для
/// случаев, когда баннер недоступен (например, сбой до создания окна).
/// </summary>
public interface IDialogService
{
    /// <summary>Подтверждение действия. Возвращает <c>true</c>, если пользователь согласился.</summary>
    bool Confirm(string message, string? title = null);

    /// <summary>Информационное сообщение.</summary>
    void ShowInfo(string message, string? title = null);

    /// <summary>Сообщение об ошибке.</summary>
    void ShowError(string message, string? title = null);

    /// <summary>Выбор файла. Возвращает путь или <c>null</c> при отмене.</summary>
    string? PickFile(string filter, string? initialDirectory = null, string? title = null);

    /// <summary>Выбор папки. Возвращает путь или <c>null</c> при отмене.</summary>
    string? PickFolder(string? initialDirectory = null, string? title = null);
}
