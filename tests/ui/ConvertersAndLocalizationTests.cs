using System.Globalization;
using WinSheduler.Shared;
using WinSheduler.UI.Localization;
using Xunit;

namespace WinSheduler.UI.Tests;

/// <summary>D-3 и D-6: строки и конвертеры значений.</summary>
public class ConvertersAndLocalizationTests
{
    [Fact]
    public void Строки_ЗагружаютсяИзResx()
    {
        Assert.Equal("Обновить", Strings.Get("Main_ActionRefresh"));
        Assert.Equal("Администратор", Strings.Get("Status_Administrator"));
        Assert.Equal("ru-RU", Strings.Culture.Name);
    }

    [Fact]
    public void Строки_НеизвестныйКлючВиденВПроверке()
        => Assert.Equal("!НетТакого_Ключа!", Strings.Get("НетТакого_Ключа"));

    [Fact]
    public void LocExtension_ВозвращаетСтрокуПоКлючу()
    {
        var extension = new LocExtension("Main_ActionDelete");
        Assert.Equal("Удалить", extension.ProvideValue(null!));
    }

    [Fact]
    public void BoolToStatusConverter_ЛокализуетЗначение()
    {
        var converter = new BoolToStatusConverter();
        Assert.Equal("Включено", converter.Convert(true, typeof(string), null!, CultureInfo.InvariantCulture));
        Assert.Equal("Отключено", converter.Convert(false, typeof(string), null!, CultureInfo.InvariantCulture));
    }

    [Fact]
    public void LocalDateTimeConverter_ФорматируетПоКонтракту()
    {
        var converter = new LocalDateTimeConverter();
        var value = new DateTime(2026, 11, 1, 9, 5, 3, DateTimeKind.Local);

        Assert.Equal(
            "01.11.2026 09:05:03",
            converter.Convert(value, typeof(string), null!, CultureInfo.InvariantCulture));
        Assert.Equal(
            "01.11.2026",
            converter.Convert(value, typeof(string), "dd.MM.yyyy", CultureInfo.InvariantCulture));
        Assert.Equal("—", converter.Convert(null!, typeof(string), null!, CultureInfo.InvariantCulture));
    }

    [Fact]
    public void DurationConverter_СобираетЧитаемуюДлительность()
    {
        var converter = new DurationConverter();
        var value = new TimeSpan(0, 1, 5, 3);

        Assert.Equal("1 ч 5 мин 3 с", converter.Convert(value, typeof(string), null!, CultureInfo.InvariantCulture));
        Assert.Equal("—", converter.Convert(null!, typeof(string), null!, CultureInfo.InvariantCulture));
    }

    [Fact]
    public void EnumTextConverter_ЛокализуетПеречисления()
    {
        var converter = new EnumTextConverter();

        Assert.Equal(
            "Завершено",
            converter.Convert(RunStatus.Completed, typeof(string), null!, CultureInfo.InvariantCulture));
        Assert.Equal(
            "Еженедельно",
            converter.Convert(TriggerKind.Weekly, typeof(string), null!, CultureInfo.InvariantCulture));
        Assert.Equal(
            "Понедельник",
            converter.Convert(DayOfWeek.Monday, typeof(string), null!, CultureInfo.InvariantCulture));
    }

    [Fact]
    public void NullToVisibilityConverter_ИнвертируетсяПараметром()
    {
        var converter = new NullToVisibilityConverter();

        Assert.Equal(
            System.Windows.Visibility.Collapsed,
            converter.Convert(null!, typeof(System.Windows.Visibility), null!, CultureInfo.InvariantCulture));
        Assert.Equal(
            System.Windows.Visibility.Visible,
            converter.Convert(null!, typeof(System.Windows.Visibility), "Invert", CultureInfo.InvariantCulture));
        Assert.Equal(
            System.Windows.Visibility.Visible,
            converter.Convert("x", typeof(System.Windows.Visibility), null!, CultureInfo.InvariantCulture));
    }
}
