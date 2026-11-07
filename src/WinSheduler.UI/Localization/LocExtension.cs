using System.Windows.Markup;

namespace WinSheduler.UI.Localization;

/// <summary>
/// Markup extension для строк из <c>Strings.resx</c>.
/// Использование в XAML: <c>Text="{loc:Loc Main_Refresh}"</c>
/// или <c>Text="{loc:Loc Key=Main_Refresh}"</c>.
/// Литеральные строки в XAML запрещены контрактом UI.md.
/// </summary>
[MarkupExtensionReturnType(typeof(string))]
public sealed class LocExtension : MarkupExtension
{
    public LocExtension()
    {
    }

    public LocExtension(string key) => Key = key;

    /// <summary>Ключ строки в формате <c>Area_Element</c>.</summary>
    public string Key { get; set; } = string.Empty;

    /// <inheritdoc />
    public override object ProvideValue(IServiceProvider serviceProvider)
    {
        try
        {
            return Strings.Get(Key);
        }
        catch
        {
            // Дизайнер VS может загрузить XAML до инициализации ресурсов.
            return string.IsNullOrEmpty(Key) ? string.Empty : $"!{Key}!";
        }
    }
}
