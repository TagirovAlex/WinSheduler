using System.Windows;
using Microsoft.Win32;

namespace WinSheduler.UI.Services;

/// <summary>Режим темы оформления.</summary>
public enum ThemeMode
{
    /// <summary>Как в системе (параметр «Приложения» в персонализации Windows).</summary>
    System,

    /// <summary>Светлая тема.</summary>
    Light,

    /// <summary>Тёмная тема.</summary>
    Dark,
}

/// <summary>
/// Переключение светлой и тёмной темы. Словарь темы заменяется в
/// <see cref="Application.Resources"/>, контролы используют DynamicResource,
/// поэтому переключение не требует перезапуска.
/// </summary>
public sealed class ThemeService
{
    private const string PersonalizeKey = @"Software\Microsoft\Windows\CurrentVersion\Themes\Personalize";
    private const string AppsUseLightThemeValue = "AppsUseLightTheme";

    private static readonly Uri LightThemeUri =
        new("pack://application:,,,/WinSheduler.UI;component/Styles/Theme.Light.xaml", UriKind.Absolute);

    private static readonly Uri DarkThemeUri =
        new("pack://application:,,,/WinSheduler.UI;component/Styles/Theme.Dark.xaml", UriKind.Absolute);

    /// <summary>Текущий выбранный режим.</summary>
    public ThemeMode Mode { get; private set; } = ThemeMode.System;

    /// <summary>Применена тёмная тема.</summary>
    public bool IsDark { get; private set; }

    /// <summary>Тема изменена.</summary>
    public event EventHandler? ThemeChanged;

    /// <summary>Применяет режим темы. <see cref="ThemeMode.System"/> читает настройку Windows.</summary>
    public void Apply(ThemeMode mode)
    {
        Mode = mode;
        ApplyDark(ResolveIsDark(mode));
    }

    /// <summary>Определяет, включена ли тёмная тема в системе.</summary>
    public static bool IsSystemDark()
    {
        // Режим высокой контрастности важнее темы: используем светлый набор,
        // чтобы не конфликтовать с системными цветами контраста.
        if (SystemParameters.HighContrast)
        {
            return false;
        }

        try
        {
            using var key = Registry.CurrentUser.OpenSubKey(PersonalizeKey);
            if (key?.GetValue(AppsUseLightThemeValue) is int appsUseLightTheme)
            {
                return appsUseLightTheme == 0;
            }
        }
        catch (Exception ex) when (ex is UnauthorizedAccessException or System.Security.SecurityException)
        {
            // Нет доступа к настройке персонализации — остаёмся на светлой теме.
        }

        return false;
    }

    private static bool ResolveIsDark(ThemeMode mode) => mode switch
    {
        ThemeMode.Dark => true,
        ThemeMode.Light => false,
        _ => IsSystemDark(),
    };

    private void ApplyDark(bool isDark)
    {
        var application = Application.Current;
        if (application is null)
        {
            IsDark = isDark;
            return;
        }

        var targetUri = isDark ? DarkThemeUri : LightThemeUri;
        var dictionaries = application.Resources.MergedDictionaries;

        var changed = false;
        if (!TrySetThemeDictionary(dictionaries, targetUri, ref changed))
        {
            dictionaries.Insert(0, new ResourceDictionary { Source = targetUri });
            changed = true;
        }

        IsDark = isDark;

        if (changed)
        {
            ThemeChanged?.Invoke(this, EventArgs.Empty);
        }
    }

    /// <summary>
    /// Заменяет словарь темы в дереве объединённых словарей.
    /// Возвращает <c>false</c>, если словарь темы не найден.
    /// </summary>
    private static bool TrySetThemeDictionary(
        IList<ResourceDictionary> dictionaries,
        Uri targetUri,
        ref bool changed)
    {
        for (var index = 0; index < dictionaries.Count; index++)
        {
            var dictionary = dictionaries[index];

            if (IsThemeSource(dictionary.Source))
            {
                if (dictionary.Source == targetUri)
                {
                    return true;
                }

                dictionaries[index] = new ResourceDictionary { Source = targetUri };
                changed = true;
                return true;
            }

            if (TrySetThemeDictionary(dictionary.MergedDictionaries, targetUri, ref changed))
            {
                return true;
            }
        }

        return false;
    }

    private static bool IsThemeSource(Uri? source)
    {
        if (source is null)
        {
            return false;
        }

        var value = source.OriginalString;
        return value.EndsWith("Theme.Light.xaml", StringComparison.OrdinalIgnoreCase)
            || value.EndsWith("Theme.Dark.xaml", StringComparison.OrdinalIgnoreCase);
    }
}
