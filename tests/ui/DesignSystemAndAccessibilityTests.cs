using System.Threading;
using System.Windows;
using System.Windows.Media;
using WinSheduler.UI.Localization;
using WinSheduler.UI.Services;
using Xunit;

namespace WinSheduler.UI.Tests;

/// <summary>D-2 и D-9: дизайн-система загружается, тема переключается, карта клавиатуры на месте.</summary>
public class DesignSystemAndAccessibilityTests
{
    [Fact]
    public void Стили_ЗагружаютсяИПереключаютТему()
    {
        RunSta(() =>
        {
            var application = new Application();
            application.Resources.MergedDictionaries.Add(new ResourceDictionary
            {
                Source = new Uri(
                    "pack://application:,,,/WinSheduler.UI;component/Styles/Styles.xaml",
                    UriKind.Absolute),
            });

            Assert.NotNull(application.TryFindResource("PrimaryButton"));
            Assert.NotNull(application.TryFindResource("SecondaryButton"));
            Assert.NotNull(application.TryFindResource("DangerButton"));
            Assert.NotNull(application.TryFindResource("ToolbarButton"));
            Assert.NotNull(application.TryFindResource("FormLabel"));
            Assert.NotNull(application.TryFindResource("FormTextBox"));
            Assert.NotNull(application.TryFindResource("FormComboBox"));
            Assert.NotNull(application.TryFindResource("DataGridStyle"));
            Assert.NotNull(application.TryFindResource("EmptyState"));
            Assert.NotNull(application.TryFindResource("StatusBanner"));
            Assert.NotNull(application.TryFindResource("Control_FocusVisual"));
            Assert.NotNull(application.TryFindResource("Glyph_Add"));

            Assert.Equal(28d, (double)application.FindResource("Control_Height"));
            Assert.Equal(16d, (double)application.FindResource("Icon_Size"));
            Assert.Equal(90d, (double)application.FindResource("Button_MinWidth"));
            Assert.Equal(4d, (double)application.FindResource("Radius"));

            var lightBackground = (SolidColorBrush)application.FindResource("App_BackgroundBrush");

            var theme = new ThemeService();
            var changed = 0;
            theme.ThemeChanged += (_, _) => changed++;
            theme.Apply(ThemeMode.Dark);

            var darkBackground = (SolidColorBrush)application.FindResource("App_BackgroundBrush");

            Assert.True(theme.IsDark);
            Assert.Equal(1, changed);
            Assert.NotEqual(lightBackground.Color, darkBackground.Color);
            Assert.Equal(Color.FromRgb(0x20, 0x20, 0x20), darkBackground.Color);

            theme.Apply(ThemeMode.Light);
            var backToLight = (SolidColorBrush)application.FindResource("App_BackgroundBrush");
            Assert.Equal(lightBackground.Color, backToLight.Color);
        });
    }

    [Fact]
    public void КартаКлавиатуры_СоответствуетКонтракту()
    {
        Assert.Equal(System.Windows.Input.Key.F5, ShortcutCatalog.Refresh.Key);
        Assert.Equal(System.Windows.Input.ModifierKeys.Control, ShortcutCatalog.AddTask.Modifiers);
        Assert.Equal(System.Windows.Input.Key.N, ShortcutCatalog.AddTask.Key);
        Assert.Equal(System.Windows.Input.Key.Enter, ShortcutCatalog.EditTask.Key);
        Assert.Equal(System.Windows.Input.Key.Delete, ShortcutCatalog.DeleteTask.Key);
        Assert.Equal(System.Windows.Input.ModifierKeys.Control, ShortcutCatalog.RunNow.Modifiers);
        Assert.Equal(System.Windows.Input.Key.R, ShortcutCatalog.RunNow.Key);

        foreach (var shortcut in ShortcutCatalog.All)
        {
            Assert.False(Strings.Get(shortcut.ActionKey).StartsWith('!'));
        }
    }

    /// <summary>Выполняет действие в отдельном STA-потоке: WPF требует STA.</summary>
    private static void RunSta(Action action)
    {
        Exception? failure = null;

        var thread = new Thread(() =>
        {
            try
            {
                action();
            }
            catch (Exception ex)
            {
                failure = ex;
            }
        });

        thread.SetApartmentState(ApartmentState.STA);
        thread.Start();
        thread.Join();

        if (failure is not null)
        {
            throw new AggregateException(failure);
        }
    }
}
