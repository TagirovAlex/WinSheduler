using System.Globalization;
using System.Windows;
using System.Windows.Data;
using System.Windows.Media;
using WinSheduler.Shared;
using WinSheduler.UI.Localization;
using WindowStyle = WinSheduler.Shared.WindowStyle;

namespace WinSheduler.UI;

/// <summary>
/// Конвертеры значений для XAML. Все подписи берутся из <see cref="Strings"/>,
/// даты и числа форматируются в культуре ru-RU (контракт UI.md, раздел 6).
/// </summary>
public class BoolToStatusConverter : IValueConverter
{
    public object Convert(object value, Type targetType, object parameter, CultureInfo culture)
        => Strings.Get(value is true ? "Common_Enabled" : "Common_Disabled");

    public object ConvertBack(object value, Type targetType, object parameter, CultureInfo culture)
        => throw new NotSupportedException();
}

/// <summary>Включённая задача — цвет успеха, отключённая — нейтральный текст.</summary>
public class BoolToColorConverter : IValueConverter
{
    public object Convert(object value, Type targetType, object parameter, CultureInfo culture)
        => ResourceBrush(value is true ? "Status_SuccessBrush" : "App_DisabledTextBrush");

    public object ConvertBack(object value, Type targetType, object parameter, CultureInfo culture)
        => throw new NotSupportedException();

    internal static Brush ResourceBrush(string key)
        => Application.Current?.TryFindResource(key) as Brush ?? Brushes.Gray;
}

public class BoolToVisibilityConverter : IValueConverter
{
    public object Convert(object value, Type targetType, object parameter, CultureInfo culture)
        => value is true ? Visibility.Visible : Visibility.Collapsed;

    public object ConvertBack(object value, Type targetType, object parameter, CultureInfo culture)
        => value is Visibility.Visible;
}

public class InverseBoolToVisibilityConverter : IValueConverter
{
    public object Convert(object value, Type targetType, object parameter, CultureInfo culture)
        => value is true ? Visibility.Collapsed : Visibility.Visible;

    public object ConvertBack(object value, Type targetType, object parameter, CultureInfo culture)
        => value is not Visibility.Visible;
}

public class InverseBooleanConverter : IValueConverter
{
    public object Convert(object value, Type targetType, object parameter, CultureInfo culture)
        => value is not true;

    public object ConvertBack(object value, Type targetType, object parameter, CultureInfo culture)
        => value is not true;
}

public class NullToVisibilityConverter : IValueConverter
{
    /// <summary>Параметр <c>Invert</c> показывает элемент, когда значение <c>null</c>.</summary>
    public object Convert(object value, Type targetType, object parameter, CultureInfo culture)
    {
        var isNull = value is null;
        var invert = string.Equals(parameter as string, "Invert", StringComparison.OrdinalIgnoreCase);
        return isNull ^ invert ? Visibility.Collapsed : Visibility.Visible;
    }

    public object ConvertBack(object value, Type targetType, object parameter, CultureInfo culture)
        => throw new NotSupportedException();
}

public class NullToBooleanConverter : IValueConverter
{
    public object Convert(object value, Type targetType, object parameter, CultureInfo culture)
        => value is not null;

    public object ConvertBack(object value, Type targetType, object parameter, CultureInfo culture)
        => throw new NotSupportedException();
}

/// <summary>Связывает перечисление с <c>RadioButton</c>: параметр — имя значения.</summary>
public class EnumToBooleanConverter : IValueConverter
{
    public object Convert(object value, Type targetType, object parameter, CultureInfo culture)
        => value is not null &&
           string.Equals(value.ToString(), parameter as string, StringComparison.Ordinal);

    public object ConvertBack(object value, Type targetType, object parameter, CultureInfo culture)
    {
        if (value is not true || parameter is not string name || targetType is null)
        {
            return Binding.DoNothing;
        }

        var enumType = Nullable.GetUnderlyingType(targetType) ?? targetType;
        return enumType.IsEnum && Enum.TryParse(enumType, name, ignoreCase: false, out var parsed)
            ? parsed
            : Binding.DoNothing;
    }
}

/// <summary>Локализованное имя значения перечисления.</summary>
public class EnumTextConverter : IValueConverter
{
    public object Convert(object value, Type targetType, object parameter, CultureInfo culture)
        => value is null ? Strings.Get("Common_Dash") : EnumText.Of(value);

    public object ConvertBack(object value, Type targetType, object parameter, CultureInfo culture)
        => throw new NotSupportedException();
}

/// <summary>Цвет статуса запуска: успех, ошибка, предупреждение, выполняется.</summary>
public class RunStatusToBrushConverter : IValueConverter
{
    public object Convert(object value, Type targetType, object parameter, CultureInfo culture)
        => value is RunStatus status
            ? BoolToColorConverter.ResourceBrush(status switch
            {
                RunStatus.Completed => "Status_SuccessBrush",
                RunStatus.Failed or RunStatus.Timeout => "Status_ErrorBrush",
                RunStatus.Killed => "Status_WarningBrush",
                RunStatus.Running => "App_AccentBrush",
                _ => "App_SecondaryTextBrush",
            })
            : BoolToColorConverter.ResourceBrush("App_SecondaryTextBrush");

    public object ConvertBack(object value, Type targetType, object parameter, CultureInfo culture)
        => throw new NotSupportedException();
}

/// <summary>
/// Локальное время в формате <c>dd.MM.yyyy HH:mm:ss</c> (контракт UI.md).
/// Параметр конвертера может задать другой формат.
/// </summary>
public class LocalDateTimeConverter : IValueConverter
{
    private const string DefaultFormat = "dd.MM.yyyy HH:mm:ss";

    public object Convert(object value, Type targetType, object parameter, CultureInfo culture)
    {
        DateTime? local = value switch
        {
            DateTimeOffset dto => dto.ToLocalTime().DateTime,
            DateTime dt => dt.Kind == DateTimeKind.Utc ? dt.ToLocalTime() : dt,
            _ => null,
        };

        if (local is null)
        {
            return Strings.Get("Common_Dash");
        }

        var format = parameter as string ?? DefaultFormat;
        return local.Value.ToString(format, Strings.Culture);
    }

    public object ConvertBack(object value, Type targetType, object parameter, CultureInfo culture)
        => throw new NotSupportedException();
}

/// <summary>Длительность в читаемом виде: «1 ч 5 мин 3 с».</summary>
public class DurationConverter : IValueConverter
{
    public object Convert(object value, Type targetType, object parameter, CultureInfo culture)
    {
        if (value is not TimeSpan duration)
        {
            return Strings.Get("Common_Dash");
        }

        if (duration < TimeSpan.Zero)
        {
            duration = TimeSpan.Zero;
        }

        var parts = new List<string>();
        if (duration.Days > 0)
        {
            parts.Add($"{duration.Days} {Strings.Get("Time_DurationDays")}");
        }

        if (duration.Hours > 0)
        {
            parts.Add($"{duration.Hours} {Strings.Get("Time_DurationHours")}");
        }

        if (duration.Minutes > 0)
        {
            parts.Add($"{duration.Minutes} {Strings.Get("Time_DurationMinutes")}");
        }

        if (parts.Count == 0 || duration.Seconds > 0)
        {
            parts.Add($"{duration.Seconds} {Strings.Get("Time_DurationSeconds")}");
        }

        return string.Join(' ', parts);
    }

    public object ConvertBack(object value, Type targetType, object parameter, CultureInfo culture)
        => throw new NotSupportedException();
}

/// <summary>Текст значения перечисления с русской подписью.</summary>
internal static class EnumText
{
    public static string Of(object value) => value switch
    {
        RunStatus status => Strings.Get(status switch
        {
            RunStatus.Running => "History_StatusRunning",
            RunStatus.Completed => "History_StatusCompleted",
            RunStatus.Failed => "History_StatusFailed",
            RunStatus.Killed => "History_StatusKilled",
            RunStatus.Timeout => "History_StatusTimeout",
            RunStatus.Aborted => "History_StatusAborted",
            _ => "Common_Unknown",
        }),
        WindowStyle style => Strings.Get(style switch
        {
            WindowStyle.Normal => "WindowStyle_Normal",
            WindowStyle.Hidden => "WindowStyle_Hidden",
            WindowStyle.Minimized => "WindowStyle_Minimized",
            WindowStyle.Maximized => "WindowStyle_Maximized",
            _ => "Common_Unknown",
        }),
        OnErrorAction action => Strings.Get(action switch
        {
            OnErrorAction.SkipNext => "OnError_SkipNext",
            OnErrorAction.Retry => "OnError_Retry",
            OnErrorAction.Fail => "OnError_Fail",
            _ => "Common_Unknown",
        }),
        OnOverlapAction overlap => Strings.Get(overlap switch
        {
            OnOverlapAction.WaitComplete => "OnOverlap_WaitComplete",
            OnOverlapAction.WaitNext => "OnOverlap_WaitNext",
            OnOverlapAction.StartAnother => "OnOverlap_StartAnother",
            _ => "Common_Unknown",
        }),
        TriggerKind kind => Strings.Get(kind switch
        {
            TriggerKind.Once => "TriggerKind_Once",
            TriggerKind.Daily => "TriggerKind_Daily",
            TriggerKind.Weekly => "TriggerKind_Weekly",
            TriggerKind.Monthly => "TriggerKind_Monthly",
            TriggerKind.Interval => "TriggerKind_Interval",
            _ => "Common_Unknown",
        }),
        SessionMode mode => Strings.Get(mode switch
        {
            SessionMode.Headless => "SessionMode_Headless",
            SessionMode.ActiveUser => "SessionMode_ActiveUser",
            _ => "Common_Unknown",
        }),
        TriggeredBy triggeredBy => Strings.Get(triggeredBy switch
        {
            TriggeredBy.Schedule => "TriggeredBy_Schedule",
            TriggeredBy.Manual => "TriggeredBy_Manual",
            _ => "Common_Unknown",
        }),
        DayOfWeek day => Strings.Get(day switch
        {
            DayOfWeek.Monday => "DayOfWeek_Monday",
            DayOfWeek.Tuesday => "DayOfWeek_Tuesday",
            DayOfWeek.Wednesday => "DayOfWeek_Wednesday",
            DayOfWeek.Thursday => "DayOfWeek_Thursday",
            DayOfWeek.Friday => "DayOfWeek_Friday",
            DayOfWeek.Saturday => "DayOfWeek_Saturday",
            DayOfWeek.Sunday => "DayOfWeek_Sunday",
            _ => "Common_Unknown",
        }),
        _ => value.ToString() ?? Strings.Get("Common_Unknown"),
    };
}
