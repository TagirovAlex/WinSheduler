using System.Globalization;
using System.Windows;
using System.Windows.Controls;

namespace WinSheduler.UI;

public partial class TimeWindowDialog : Window
{
    private static readonly CultureInfo Invariant = CultureInfo.InvariantCulture;

    private static readonly string[] ShortDayNames = ["Вс", "Пн", "Вт", "Ср", "Чт", "Пт", "Сб"];

    public TimeWindowEditType SelectedType { get; private set; } = TimeWindowEditType.Exact;
    public string ExactTimes { get; private set; } = "";
    public string StartTime { get; private set; } = "";
    public string EndTime { get; private set; } = "";
    public string RepeatInterval { get; private set; } = "";
    public List<DayOfWeek> SelectedDays { get; private set; } = [];
    public string? StartDate { get; private set; }
    public string? EndDate { get; private set; }
    public string? RepeatUntil { get; private set; }
    public int DayInterval { get; private set; } = 1;
    public int WeekInterval { get; private set; } = 1;
    public int MonthDay { get; private set; } = 1;
    public int MonthInterval { get; private set; } = 1;
    public string? OnceDate { get; private set; }
    public string? OnceTime { get; private set; }
    public bool TriggerEnabled { get; private set; } = true;

    /// <summary>Текстовое описание настроенного триггера для списка в редакторе задачи.</summary>
    public string Summary => BuildSummary();

    public TimeWindowDialog()
    {
        InitializeComponent();
        MaxHeight = Math.Max(360, SystemParameters.WorkArea.Height - 24);
        StartDatePicker.SelectedDate = DateTime.Today;
        UpdatePanels();
        UpdateSummary();
    }

    public TimeWindowDialog(TimeWindowEditModel existing) : this()
    {
        switch (existing.Type)
        {
            case TimeWindowEditType.Once: RadioOnce.IsChecked = true; break;
            case TimeWindowEditType.Exact: RadioDaily.IsChecked = true; break;
            case TimeWindowEditType.ExactWeekly: RadioWeekly.IsChecked = true; break;
            case TimeWindowEditType.ExactMonthly: RadioMonthly.IsChecked = true; break;
            case TimeWindowEditType.Interval: RadioInterval.IsChecked = true; break;
        }

        OnceDatePicker.SelectedDate = existing.OnceDate ?? DateTime.Today;
        if (!string.IsNullOrWhiteSpace(existing.OnceTimeStr)) OnceTimeBox.Text = existing.OnceTimeStr;

        DailyIntervalBox.Text = existing.DayInterval.ToString(Invariant);
        DailyTimesBox.Text = existing.ExactTimesStr;

        WeeklyIntervalBox.Text = existing.WeekInterval.ToString(Invariant);
        WeeklyTimesBox.Text = existing.ExactTimesStr;

        MonthlyDayBox.Text = existing.MonthDay.ToString(Invariant);
        MonthlyIntervalBox.Text = existing.MonthInterval.ToString(Invariant);
        MonthlyTimesBox.Text = existing.ExactTimesStr;

        IntervalStartBox.Text = string.IsNullOrWhiteSpace(existing.StartTimeStr) ? "09:00" : existing.StartTimeStr;
        IntervalEndBox.Text = string.IsNullOrWhiteSpace(existing.EndTimeStr) ? "18:00" : existing.EndTimeStr;
        if (!string.IsNullOrWhiteSpace(existing.RepeatIntervalStr)) IntervalRepeatBox.Text = existing.RepeatIntervalStr;

        foreach (var day in existing.SelectedDays)
        {
            var box = DayBox(day);
            if (box != null) box.IsChecked = true;
        }

        if (ParseLocalDateTime(existing.StartDate) is { } start) StartDatePicker.SelectedDate = start;
        if (ParseLocalDateTime(existing.EndDate) is { } end) EndDatePicker.SelectedDate = end;
        if (ParseLocalDateTime(existing.RepeatUntil) is { } until)
        {
            RepeatUntilPicker.SelectedDate = until.Date;
            RepeatUntilTimeBox.Text = until.ToString("HH:mm", Invariant);
        }

        if (!existing.TriggerEnabled) EnabledCheck.IsChecked = false;

        UpdatePanels();
        UpdateSummary();
    }

    private CheckBox? DayBox(DayOfWeek day) => day switch
    {
        DayOfWeek.Monday => MonBox,
        DayOfWeek.Tuesday => TueBox,
        DayOfWeek.Wednesday => WedBox,
        DayOfWeek.Thursday => ThuBox,
        DayOfWeek.Friday => FriBox,
        DayOfWeek.Saturday => SatBox,
        DayOfWeek.Sunday => SunBox,
        _ => null
    };

    private TimeWindowEditType CurrentType =>
        RadioOnce.IsChecked == true ? TimeWindowEditType.Once :
        RadioWeekly.IsChecked == true ? TimeWindowEditType.ExactWeekly :
        RadioMonthly.IsChecked == true ? TimeWindowEditType.ExactMonthly :
        RadioInterval.IsChecked == true ? TimeWindowEditType.Interval :
        TimeWindowEditType.Exact;

    private void Radio_Checked(object sender, RoutedEventArgs e)
    {
        if (PanelOnce == null) return;
        UpdatePanels();
        UpdateSummary();
    }

    private void Field_Changed(object sender, RoutedEventArgs e) => UpdateSummary();

    private void Field_TextChanged(object sender, System.Windows.Controls.TextChangedEventArgs e) => UpdateSummary();

    private void UpdatePanels()
    {
        if (PanelOnce == null) return;

        var type = CurrentType;
        PanelOnce.Visibility = type == TimeWindowEditType.Once ? Visibility.Visible : Visibility.Collapsed;
        PanelDaily.Visibility = type == TimeWindowEditType.Exact ? Visibility.Visible : Visibility.Collapsed;
        PanelWeekly.Visibility = type == TimeWindowEditType.ExactWeekly ? Visibility.Visible : Visibility.Collapsed;
        PanelMonthly.Visibility = type == TimeWindowEditType.ExactMonthly ? Visibility.Visible : Visibility.Collapsed;
        PanelInterval.Visibility = type == TimeWindowEditType.Interval ? Visibility.Visible : Visibility.Collapsed;
    }

    private void UpdateSummary()
    {
        if (SummaryText == null) return;
        SummaryText.Text = BuildSummary();
        if (ErrorText != null && ErrorText.Visibility == Visibility.Visible)
        {
            ErrorText.Visibility = Visibility.Collapsed;
            ErrorText.Text = "";
        }
    }

    private string BuildSummary()
    {
        var parts = new List<string>();

        switch (CurrentType)
        {
            case TimeWindowEditType.Once:
                parts.Add(OnceDatePicker.SelectedDate is { } onceDate
                    ? $"Однократно: {onceDate.ToString("dd.MM.yyyy", Invariant)} в {Text(OnceTimeBox)}"
                    : "Однократно: дата не указана");
                break;

            case TimeWindowEditType.Exact:
                {
                    var interval = ReadInt(DailyIntervalBox);
                    parts.Add(interval > 1 ? $"Ежедневно, каждые {interval} дн." : "Ежедневно");
                    parts.Add($"в {Text(DailyTimesBox)}");
                    break;
                }

            case TimeWindowEditType.ExactWeekly:
                {
                    var interval = ReadInt(WeeklyIntervalBox);
                    parts.Add(interval > 1 ? $"Еженедельно, каждые {interval} нед." : "Еженедельно");
                    var days = SelectedDaysOrdered();
                    if (days.Count > 0) parts.Add(string.Join(", ", days));
                    parts.Add($"в {Text(WeeklyTimesBox)}");
                    break;
                }

            case TimeWindowEditType.ExactMonthly:
                {
                    var interval = ReadInt(MonthlyIntervalBox);
                    var day = ReadInt(MonthlyDayBox);
                    parts.Add(interval > 1
                        ? $"Ежемесячно, {day}-го числа, каждые {interval} мес."
                        : $"Ежемесячно, {day}-го числа");
                    parts.Add($"в {Text(MonthlyTimesBox)}");
                    break;
                }

            case TimeWindowEditType.Interval:
                parts.Add($"Интервал: с {Text(IntervalStartBox)} до {Text(IntervalEndBox)}, " +
                          $"каждые {Text(IntervalRepeatBox)} мин");
                break;
        }

        if (StartDatePicker.SelectedDate is { } startDate)
            parts.Add($"с {startDate.ToString("dd.MM.yyyy", Invariant)}");
        if (EndDatePicker.SelectedDate is { } endDate)
            parts.Add($"по {endDate.ToString("dd.MM.yyyy", Invariant)}");
        if (RepeatUntilPicker.SelectedDate is { } repeatDate)
            parts.Add($"повторять до {repeatDate.ToString("dd.MM.yyyy", Invariant)} {Text(RepeatUntilTimeBox)}");
        if (EnabledCheck.IsChecked != true)
            parts.Add("выключено");

        return string.Join("; ", parts.Where(p => !string.IsNullOrWhiteSpace(p)));
    }

    private List<string> SelectedDaysOrdered()
    {
        DayOfWeek[] order =
        [
            DayOfWeek.Monday, DayOfWeek.Tuesday, DayOfWeek.Wednesday, DayOfWeek.Thursday,
            DayOfWeek.Friday, DayOfWeek.Saturday, DayOfWeek.Sunday
        ];
        return order.Where(d => DayBox(d)?.IsChecked == true)
                    .Select(d => ShortDayNames[(int)d])
                    .ToList();
    }

    private static string Text(System.Windows.Controls.TextBox box) => box.Text.Trim();

    private static int ReadInt(System.Windows.Controls.TextBox box) =>
        int.TryParse(box.Text.Trim(), NumberStyles.Integer, Invariant, out var value) ? value : 0;

    private void OkBtn_Click(object sender, RoutedEventArgs e)
    {
        if (!ValidateAndApply()) return;
        DialogResult = true;
    }

    private void CancelBtn_Click(object sender, RoutedEventArgs e)
    {
        DialogResult = false;
    }

    private bool ValidateAndApply()
    {
        var errors = new List<string>();

        var startDate = StartDatePicker.SelectedDate?.Date;
        var endDate = EndDatePicker.SelectedDate?.Date;
        if (startDate.HasValue && endDate.HasValue && endDate.Value < startDate.Value)
            errors.Add("Дата конца не может быть раньше даты начала.");

        DateTime? repeatUntil = null;
        if (RepeatUntilPicker.SelectedDate is { } repeatDate)
        {
            if (!TryParseTime(RepeatUntilTimeBox.Text, out var repeatTime))
                errors.Add("Время «Повторять до» должно быть в формате ЧЧ:ММ.");
            else
                repeatUntil = repeatDate.Date + repeatTime;

            if (repeatUntil.HasValue && startDate.HasValue && repeatUntil.Value < startDate.Value)
                errors.Add("Дата «Повторять до» не может быть раньше даты начала.");
        }

        switch (CurrentType)
        {
            case TimeWindowEditType.Once:
                {
                    SelectedType = TimeWindowEditType.Once;
                    if (OnceDatePicker.SelectedDate is not { } onceDate)
                        errors.Add("Укажите дату однократного запуска.");
                    else
                        OnceDate = onceDate.ToString("yyyy-MM-dd", Invariant);

                    if (!TryParseTime(OnceTimeBox.Text, out var onceTime))
                        errors.Add("Время однократного запуска должно быть в формате ЧЧ:ММ.");
                    else
                    {
                        OnceTime = FormatTime(onceTime);
                        ExactTimes = OnceTime;
                    }

                    SelectedDays = [];
                    break;
                }

            case TimeWindowEditType.Exact:
                {
                    SelectedType = TimeWindowEditType.Exact;
                    DayInterval = ParseIntInRange(DailyIntervalBox, 1, 365, "Интервал в днях", errors);
                    if (TryParseTimes(DailyTimesBox.Text, out var times, out var badTime))
                        ExactTimes = string.Join(", ", times);
                    else
                        errors.Add(badTime);
                    SelectedDays = [];
                    break;
                }

            case TimeWindowEditType.ExactWeekly:
                {
                    SelectedType = TimeWindowEditType.ExactWeekly;
                    WeekInterval = ParseIntInRange(WeeklyIntervalBox, 1, 52, "Интервал в неделях", errors);
                    if (TryParseTimes(WeeklyTimesBox.Text, out var times, out var badTime))
                        ExactTimes = string.Join(", ", times);
                    else
                        errors.Add(badTime);

                    SelectedDays = [];
                    foreach (var day in Enum.GetValues<DayOfWeek>())
                    {
                        if (DayBox(day)?.IsChecked == true) SelectedDays.Add(day);
                    }
                    if (SelectedDays.Count == 0)
                        errors.Add("Выберите хотя бы один день недели.");
                    break;
                }

            case TimeWindowEditType.ExactMonthly:
                {
                    SelectedType = TimeWindowEditType.ExactMonthly;
                    MonthDay = ParseIntInRange(MonthlyDayBox, 1, 31, "День месяца", errors);
                    MonthInterval = ParseIntInRange(MonthlyIntervalBox, 1, 12, "Интервал в месяцах", errors);
                    if (TryParseTimes(MonthlyTimesBox.Text, out var times, out var badTime))
                        ExactTimes = string.Join(", ", times);
                    else
                        errors.Add(badTime);
                    SelectedDays = [];
                    break;
                }

            case TimeWindowEditType.Interval:
                {
                    SelectedType = TimeWindowEditType.Interval;
                    var hasStart = TryParseTime(IntervalStartBox.Text, out var intervalStart);
                    var hasEnd = TryParseTime(IntervalEndBox.Text, out var intervalEnd);
                    if (!hasStart) errors.Add("Начало интервала должно быть в формате ЧЧ:ММ.");
                    if (!hasEnd) errors.Add("Конец интервала должен быть в формате ЧЧ:ММ.");
                    if (hasStart && hasEnd && intervalStart >= intervalEnd)
                        errors.Add("Начало интервала должно быть раньше конца.");

                    StartTime = hasStart ? FormatTime(intervalStart) : Text(IntervalStartBox);
                    EndTime = hasEnd ? FormatTime(intervalEnd) : Text(IntervalEndBox);

                    var repeat = ParseIntInRange(IntervalRepeatBox, 1, 1440, "Интервал в минутах", errors);
                    RepeatInterval = repeat.ToString(Invariant);
                    SelectedDays = [];
                    break;
                }
        }

        if (errors.Count > 0)
        {
            ShowError(errors[0]);
            return false;
        }

        StartDate = startDate?.ToString("yyyy-MM-dd", Invariant);
        EndDate = endDate?.ToString("yyyy-MM-dd", Invariant);
        RepeatUntil = repeatUntil?.ToString("yyyy-MM-ddTHH:mm", Invariant);
        TriggerEnabled = EnabledCheck.IsChecked == true;

        ErrorText.Visibility = Visibility.Collapsed;
        ErrorText.Text = "";
        return true;
    }

    private int ParseIntInRange(System.Windows.Controls.TextBox box, int min, int max, string fieldName, List<string> errors)
    {
        if (int.TryParse(box.Text.Trim(), NumberStyles.Integer, Invariant, out var value) && value >= min && value <= max)
            return value;

        errors.Add($"Поле «{fieldName}» должно быть целым числом от {min} до {max}.");
        return min;
    }

    private static bool TryParseTimes(string? text, out List<string> times, out string error)
    {
        times = [];
        error = "";

        var parts = (text ?? "")
            .Split([',', ';', ' ', '\t'], StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries);

        if (parts.Length == 0)
        {
            error = "Укажите хотя бы одно время запуска в формате ЧЧ:ММ.";
            return false;
        }

        var parsed = new List<TimeSpan>();
        foreach (var part in parts)
        {
            if (!TryParseTime(part, out var time))
            {
                error = $"Время «{part}» должно быть в формате ЧЧ:ММ.";
                return false;
            }
            if (!parsed.Contains(time)) parsed.Add(time);
        }

        parsed.Sort();
        times = parsed.Select(FormatTime).ToList();
        return true;
    }

    private static bool TryParseTime(string? text, out TimeSpan time)
    {
        time = default;
        if (string.IsNullOrWhiteSpace(text)) return false;

        var formats = new[] { "H:mm", "HH:mm" };
        if (!DateTime.TryParseExact(text.Trim(), formats, Invariant, DateTimeStyles.None, out var parsed))
            return false;

        time = parsed.TimeOfDay;
        return true;
    }

    private static string FormatTime(TimeSpan time) => time.ToString(@"hh\:mm", Invariant);

    /// <summary>Разбирает локальные дату/время из модели. Поддерживает ISO-подобные варианты и старый формат.</summary>
    private static DateTime? ParseLocalDateTime(string? value)
    {
        if (string.IsNullOrWhiteSpace(value)) return null;

        var formats = new[]
        {
            "yyyy-MM-ddTHH:mm", "yyyy-MM-ddTHH:mm:ss", "yyyy-MM-dd HH:mm:ss", "yyyy-MM-dd"
        };
        return DateTime.TryParseExact(value.Trim(), formats, Invariant, DateTimeStyles.None, out var parsed)
            ? parsed
            : null;
    }

    private void ShowError(string message)
    {
        ErrorText.Text = message;
        ErrorText.Visibility = Visibility.Visible;
    }
}
