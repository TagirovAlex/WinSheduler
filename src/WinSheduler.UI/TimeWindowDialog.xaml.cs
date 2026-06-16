using System.Windows;

namespace WinSheduler.UI;

public partial class TimeWindowDialog : Window
{
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

    public TimeWindowDialog()
    {
        InitializeComponent();
        StartDatePicker.SelectedDate = DateTime.Today;
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

        if (existing.Type == TimeWindowEditType.Once)
        {
            if (existing.OnceDate.HasValue) OnceDatePicker.SelectedDate = existing.OnceDate;
            OnceTimeBox.Text = existing.OnceTimeStr ?? "18:00";
        }

        if (existing.Type is TimeWindowEditType.Exact or TimeWindowEditType.ExactWeekly or TimeWindowEditType.ExactMonthly)
        {
            if (existing.DayInterval > 1) DailyIntervalBox.Text = existing.DayInterval.ToString();
            DailyTimesBox.Text = existing.ExactTimesStr;

            if (existing.WeekInterval > 1) WeeklyIntervalBox.Text = existing.WeekInterval.ToString();
            WeeklyTimesBox.Text = existing.ExactTimesStr;

            if (existing.MonthDay > 1) MonthlyDayBox.Text = existing.MonthDay.ToString();
            if (existing.MonthInterval > 1) MonthlyIntervalBox.Text = existing.MonthInterval.ToString();
            MonthlyTimesBox.Text = existing.ExactTimesStr;

            foreach (var d in existing.SelectedDays)
            {
                switch (d)
                {
                    case DayOfWeek.Monday: MonBox.IsChecked = true; break;
                    case DayOfWeek.Tuesday: TueBox.IsChecked = true; break;
                    case DayOfWeek.Wednesday: WedBox.IsChecked = true; break;
                    case DayOfWeek.Thursday: ThuBox.IsChecked = true; break;
                    case DayOfWeek.Friday: FriBox.IsChecked = true; break;
                    case DayOfWeek.Saturday: SatBox.IsChecked = true; break;
                    case DayOfWeek.Sunday: SunBox.IsChecked = true; break;
                }
            }
        }

        if (existing.Type == TimeWindowEditType.Interval)
        {
            IntervalStartBox.Text = existing.StartTimeStr;
            IntervalEndBox.Text = existing.EndTimeStr;
            IntervalRepeatBox.Text = existing.RepeatIntervalStr;

            foreach (var d in existing.SelectedDays)
            {
                switch (d)
                {
                    case DayOfWeek.Monday: IntMonBox.IsChecked = true; break;
                    case DayOfWeek.Tuesday: IntTueBox.IsChecked = true; break;
                    case DayOfWeek.Wednesday: IntWedBox.IsChecked = true; break;
                    case DayOfWeek.Thursday: IntThuBox.IsChecked = true; break;
                    case DayOfWeek.Friday: IntFriBox.IsChecked = true; break;
                    case DayOfWeek.Saturday: IntSatBox.IsChecked = true; break;
                    case DayOfWeek.Sunday: IntSunBox.IsChecked = true; break;
                }
            }
        }

        if (existing.StartDate != null && DateTime.TryParse(existing.StartDate, out var sd))
            StartDatePicker.SelectedDate = sd;
        if (existing.EndDate != null && DateTime.TryParse(existing.EndDate, out var ed))
            EndDatePicker.SelectedDate = ed;
        if (existing.RepeatUntil != null && DateTime.TryParse(existing.RepeatUntil, out var ru))
            RepeatUntilPicker.SelectedDate = ru;

        if (!existing.TriggerEnabled) EnabledCheck.IsChecked = false;

        Radio_Checked(null!, null!);
    }

    private void Radio_Checked(object sender, RoutedEventArgs e)
    {
        if (PanelOnce == null) return;

        PanelOnce.Visibility = RadioOnce.IsChecked == true ? Visibility.Visible : Visibility.Collapsed;
        PanelDaily.Visibility = RadioDaily.IsChecked == true ? Visibility.Visible : Visibility.Collapsed;
        PanelWeekly.Visibility = RadioWeekly.IsChecked == true ? Visibility.Visible : Visibility.Collapsed;
        PanelMonthly.Visibility = RadioMonthly.IsChecked == true ? Visibility.Visible : Visibility.Collapsed;
        PanelInterval.Visibility = RadioInterval.IsChecked == true ? Visibility.Visible : Visibility.Collapsed;
    }

    private void RepeatEveryCheck_Changed(object sender, RoutedEventArgs e)
    {
        if (RepeatEveryValueBox == null) return;
        var on = RepeatEveryCheck.IsChecked == true;
        RepeatEveryValueBox.IsEnabled = on;
        RepeatEveryUnitBox.IsEnabled = on;
        RepeatDurationBox.IsEnabled = on;
        RepeatDurationUnitBox.IsEnabled = on;
        RepeatStopAllCheck.IsEnabled = on;
    }

    private void StopAfterCheck_Changed(object sender, RoutedEventArgs e)
    {
        if (StopAfterValueBox == null) return;
        var on = StopAfterCheck.IsChecked == true;
        StopAfterValueBox.IsEnabled = on;
        StopAfterUnitBox.IsEnabled = on;
    }

    private void ExpirationCheck_Changed(object sender, RoutedEventArgs e)
    {
        if (ExpirationDatePicker == null) return;
        var on = ExpirationCheck.IsChecked == true;
        ExpirationDatePicker.IsEnabled = on;
        ExpirationTimeBox.IsEnabled = on;
    }

    private void OkBtn_Click(object sender, RoutedEventArgs e)
    {
        if (RadioDaily.IsChecked == true)
        {
            SelectedType = TimeWindowEditType.Exact;
            if (string.IsNullOrWhiteSpace(DailyTimesBox.Text))
            { MessageBox.Show("Введите время запуска", "Ошибка", MessageBoxButton.OK, MessageBoxImage.Warning); return; }
            ExactTimes = DailyTimesBox.Text.Trim();
            if (int.TryParse(DailyIntervalBox.Text, out var di) && di > 0) DayInterval = di;
        }
        else if (RadioWeekly.IsChecked == true)
        {
            SelectedType = TimeWindowEditType.ExactWeekly;
            if (string.IsNullOrWhiteSpace(WeeklyTimesBox.Text))
            { MessageBox.Show("Введите время запуска", "Ошибка", MessageBoxButton.OK, MessageBoxImage.Warning); return; }
            ExactTimes = WeeklyTimesBox.Text.Trim();
            if (int.TryParse(WeeklyIntervalBox.Text, out var wi) && wi > 0) WeekInterval = wi;

            SelectedDays = [];
            if (MonBox.IsChecked == true) SelectedDays.Add(DayOfWeek.Monday);
            if (TueBox.IsChecked == true) SelectedDays.Add(DayOfWeek.Tuesday);
            if (WedBox.IsChecked == true) SelectedDays.Add(DayOfWeek.Wednesday);
            if (ThuBox.IsChecked == true) SelectedDays.Add(DayOfWeek.Thursday);
            if (FriBox.IsChecked == true) SelectedDays.Add(DayOfWeek.Friday);
            if (SatBox.IsChecked == true) SelectedDays.Add(DayOfWeek.Saturday);
            if (SunBox.IsChecked == true) SelectedDays.Add(DayOfWeek.Sunday);
        }
        else if (RadioMonthly.IsChecked == true)
        {
            SelectedType = TimeWindowEditType.ExactMonthly;
            if (string.IsNullOrWhiteSpace(MonthlyTimesBox.Text))
            { MessageBox.Show("Введите время запуска", "Ошибка", MessageBoxButton.OK, MessageBoxImage.Warning); return; }
            ExactTimes = MonthlyTimesBox.Text.Trim();
            if (int.TryParse(MonthlyDayBox.Text, out var md) && md is >= 1 and <= 31) MonthDay = md;
            if (int.TryParse(MonthlyIntervalBox.Text, out var mi) && mi > 0) MonthInterval = mi;
        }
        else if (RadioInterval.IsChecked == true)
        {
            SelectedType = TimeWindowEditType.Interval;
            if (string.IsNullOrWhiteSpace(IntervalStartBox.Text) || string.IsNullOrWhiteSpace(IntervalEndBox.Text))
            { MessageBox.Show("Введите начало и конец интервала", "Ошибка", MessageBoxButton.OK, MessageBoxImage.Warning); return; }
            StartTime = IntervalStartBox.Text.Trim();
            EndTime = IntervalEndBox.Text.Trim();
            RepeatInterval = IntervalRepeatBox.Text.Trim();

            SelectedDays = [];
            if (IntMonBox.IsChecked == true) SelectedDays.Add(DayOfWeek.Monday);
            if (IntTueBox.IsChecked == true) SelectedDays.Add(DayOfWeek.Tuesday);
            if (IntWedBox.IsChecked == true) SelectedDays.Add(DayOfWeek.Wednesday);
            if (IntThuBox.IsChecked == true) SelectedDays.Add(DayOfWeek.Thursday);
            if (IntFriBox.IsChecked == true) SelectedDays.Add(DayOfWeek.Friday);
            if (IntSatBox.IsChecked == true) SelectedDays.Add(DayOfWeek.Saturday);
            if (IntSunBox.IsChecked == true) SelectedDays.Add(DayOfWeek.Sunday);
        }
        else // RadioOnce
        {
            SelectedType = TimeWindowEditType.Once;
            OnceDate = OnceDatePicker.SelectedDate?.ToString("yyyy-MM-dd");
            OnceTime = OnceTimeBox.Text.Trim();
            if (string.IsNullOrWhiteSpace(OnceTime))
            { MessageBox.Show("Введите время запуска", "Ошибка", MessageBoxButton.OK, MessageBoxImage.Warning); return; }
        }

        StartDate = StartDatePicker.SelectedDate?.ToString("yyyy-MM-dd");
        EndDate = EndDatePicker.SelectedDate?.ToString("yyyy-MM-dd");
        RepeatUntil = RepeatUntilPicker.SelectedDate?.ToString("yyyy-MM-dd");
        TriggerEnabled = EnabledCheck.IsChecked == true;

        DialogResult = true;
    }

    private void CancelBtn_Click(object sender, RoutedEventArgs e)
    {
        DialogResult = false;
    }
}
