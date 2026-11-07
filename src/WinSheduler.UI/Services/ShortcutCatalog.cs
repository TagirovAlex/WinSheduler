using System.Windows.Input;

namespace WinSheduler.UI.Services;

/// <summary>Описание горячей клавиши для карты клавиатуры и справки.</summary>
public sealed record ShortcutInfo(string ActionKey, KeyGesture Gesture);

/// <summary>
/// Карта клавиатуры (контракт UI.md, раздел 3). Экраны привязывают эти жесты
/// через <c>InputBindings</c>, чтобы подсказки и справка не расходились.
/// </summary>
public static class ShortcutCatalog
{
    /// <summary>F5 — обновить список.</summary>
    public static KeyGesture Refresh { get; } = new(Key.F5);

    /// <summary>Ctrl+N — добавить задачу.</summary>
    public static KeyGesture AddTask { get; } = new(Key.N, ModifierKeys.Control);

    /// <summary>Enter — изменить выбранную задачу.</summary>
    public static KeyGesture EditTask { get; } = new(Key.Enter);

    /// <summary>Delete — удалить выбранную задачу.</summary>
    public static KeyGesture DeleteTask { get; } = new(Key.Delete);

    /// <summary>Ctrl+R — запустить выбранную задачу.</summary>
    public static KeyGesture RunNow { get; } = new(Key.R, ModifierKeys.Control);

    /// <summary>F1 — открыть справку.</summary>
    public static KeyGesture Help { get; } = new(Key.F1);

    /// <summary>Esc — отмена текущего действия или закрытие диалога.</summary>
    public static KeyGesture Cancel { get; } = new(Key.Escape);

    /// <summary>Полная карта клавиатуры в порядке отображения.</summary>
    public static IReadOnlyList<ShortcutInfo> All { get; } =
    [
        new("Main_ActionRefresh", Refresh),
        new("Main_ActionAdd", AddTask),
        new("Main_ActionEdit", EditTask),
        new("Main_ActionDelete", DeleteTask),
        new("Main_ActionRun", RunNow),
        new("Menu_HelpHelp", Help),
        new("Common_Cancel", Cancel),
    ];
}
