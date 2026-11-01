using WinSheduler.Shared;
using WinSheduler.Shared.Models;
using Xunit;

namespace WinSheduler.Tests;

/// <summary>
/// Дымовые тесты каркаса: проверяют, что общие модели собираются
/// и что новые поля v2 имеют обратно совместимые значения по умолчанию.
/// </summary>
public class SmokeTests
{
    [Fact]
    public void ОбщиеМодели_НовыеПоляИмеютЗначенияПоУмолчанию()
    {
        var task = new TaskDto();
        var definition = new TaskDefinition();
        var window = new TimeWindowDto();
        var history = new RunHistory();
        var status = new ServiceStatus();

        Assert.Equal(SessionMode.Headless, task.SessionMode);
        Assert.Equal("", task.OwnerSid);
        Assert.False(task.HasPassword);
        Assert.Null(task.Password);

        Assert.Equal(SessionMode.Headless, definition.SessionMode);
        Assert.Equal("", definition.OwnerSid);

        Assert.Equal(TriggerKind.Daily, window.Kind);
        Assert.Equal(1, window.DayInterval);
        Assert.Equal(1, window.WeekInterval);
        Assert.Equal(1, window.MonthInterval);

        Assert.Equal(TriggeredBy.Schedule, history.TriggeredBy);

        Assert.False(status.IsAdmin);
        Assert.False(status.IsOperator);
        Assert.False(status.CanManageSystemTasks);
        Assert.Equal("", status.ServiceVersion);
        Assert.False(status.DbReady);

        Assert.Equal(5, (int)RunStatus.Aborted);
    }
}
