using System.Globalization;
using System.Resources;

namespace WinSheduler.UI.Localization;

/// <summary>
/// Доступ к строкам интерфейса. Язык — русский (контракт UI.md, раздел 6),
/// форматирование — <see cref="CultureInfo"/> ru-RU.
///
/// Ключи имеют формат <c>Area_Element</c>. Отсутствующий ключ возвращается
/// как <c>!Ключ!</c>, чтобы пропуск был виден при проверке, а не молча пустел.
/// </summary>
public static class Strings
{
    private const string ResourceBaseName = "WinSheduler.UI.Localization.Strings";

    private static readonly ResourceManager Manager = new(ResourceBaseName, typeof(Strings).Assembly);

    /// <summary>Культура интерфейса и форматирования дат/чисел.</summary>
    public static CultureInfo Culture { get; } = CultureInfo.GetCultureInfo("ru-RU");

    /// <summary>Возвращает строку по ключу или маркер пропуска <c>!Ключ!</c>.</summary>
    public static string Get(string key)
        => Manager.GetString(key, Culture) ?? $"!{key}!";

    /// <summary>Возвращает строку по ключу или <c>null</c>, если ключа нет.</summary>
    public static string? Find(string key) => Manager.GetString(key, Culture);

    /// <summary>Возвращает отформатированную строку по ключу.</summary>
    public static string Format(string key, params object?[] args)
        => string.Format(Culture, Get(key), args);
}
