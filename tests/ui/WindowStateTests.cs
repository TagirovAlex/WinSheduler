using System.IO;
using WinSheduler.UI.Services;
using Xunit;

namespace WinSheduler.UI.Tests;

/// <summary>D-8: сохранение состояния окна и последнего выбора.</summary>
public class WindowStateTests
{
    [Fact]
    public void НовыйФайлСостояния_ДаётЗначенияПоУмолчанию()
    {
        var path = TempPath();
        try
        {
            var service = new WindowStateService(path);

            Assert.Null(service.State.Width);
            Assert.Null(service.State.Height);
            Assert.False(service.State.Maximized);
            Assert.Null(service.State.LastSelectedTaskId);
        }
        finally
        {
            File.Delete(path);
        }
    }

    [Fact]
    public void ПоследнийВыбор_СохраняетсяИЧитается()
    {
        var path = TempPath();
        try
        {
            var taskId = Guid.NewGuid();
            new WindowStateService(path).RememberSelection(taskId);

            var restored = new WindowStateService(path);

            Assert.Equal(taskId, restored.State.LastSelectedTaskId);
        }
        finally
        {
            File.Delete(path);
        }
    }

    [Fact]
    public void ПовреждённыйФайл_НеЛомаетЗапуск()
    {
        var path = TempPath();
        try
        {
            File.WriteAllText(path, "{ это не json");

            var service = new WindowStateService(path);

            Assert.Null(service.State.Width);
        }
        finally
        {
            File.Delete(path);
        }
    }

    private static string TempPath()
        => Path.Combine(AppContext.BaseDirectory, $"ui-state-{Guid.NewGuid():N}.json");
}
