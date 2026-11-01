using System.IO;
using WinSheduler.Shared;
using WinSheduler.UI.Services;
using WinSheduler.UI.Tests.Fakes;
using WinSheduler.UI.ViewModels;
using Xunit;

namespace WinSheduler.UI.Tests;

/// <summary>
/// D-7: batch-обновление, фильтрация, отмена и защита от повторного входа.
/// ViewModel рассчитан на UI-поток; тесты завершают ожидания на своём потоке.
/// </summary>
public class DashboardViewModelTests
{
    [Fact]
    public async Task Refresh_ЗаполняетСписокОднимBatchЗапросом()
    {
        var fake = new FakeIpcService
        {
            DashboardFactory = _ => Task.FromResult(new DashboardDto
            {
                Tasks =
                [
                    new DashboardTaskDto { Id = Guid.NewGuid(), Name = "Бэкап", IsOwn = true, Enabled = true },
                    new DashboardTaskDto { Id = Guid.NewGuid(), Name = "Отчёт", IsOwn = false, Enabled = false },
                ],
            }),
        };

        var viewModel = new DashboardViewModel(fake);
        await viewModel.RefreshAsync();

        Assert.Equal(1, fake.DashboardCalls);
        Assert.Equal(2, viewModel.Tasks.Count);
        Assert.NotNull(viewModel.LastRefreshedAt);
        Assert.False(viewModel.HasError);
    }

    [Fact]
    public async Task Refresh_ПовторныйВходИгнорируется()
    {
        var gate = new TaskCompletionSource<DashboardDto>(TaskCreationOptions.RunContinuationsAsynchronously);
        var fake = new FakeIpcService { DashboardFactory = _ => gate.Task };
        var viewModel = new DashboardViewModel(fake);

        var first = viewModel.RefreshAsync();
        var second = viewModel.RefreshAsync();

        Assert.Equal(1, fake.DashboardCalls);
        Assert.True(viewModel.IsLoading);

        gate.SetResult(new DashboardDto());
        await Task.WhenAll(first, second);

        Assert.False(viewModel.IsLoading);
    }

    [Fact]
    public async Task Filter_ИПоиск_МеняютВидимыйСписок()
    {
        var fake = new FakeIpcService
        {
            DashboardFactory = _ => Task.FromResult(new DashboardDto
            {
                Tasks =
                [
                    new DashboardTaskDto { Id = Guid.NewGuid(), Name = "Ночной бэкап", IsOwn = true, Enabled = true, ProgramPath = "d:/scripts/backup.cmd" },
                    new DashboardTaskDto { Id = Guid.NewGuid(), Name = "Отчёт", IsOwn = false, Enabled = false, ProgramPath = "d:/scripts/report.cmd" },
                ],
            }),
        };

        var viewModel = new DashboardViewModel(fake);
        await viewModel.RefreshAsync();

        Assert.Equal(2, viewModel.VisibleTaskCount);

        viewModel.Filter = TaskFilter.Mine;
        Assert.Equal(1, viewModel.VisibleTaskCount);

        viewModel.Filter = TaskFilter.All;
        viewModel.SearchText = "report";
        Assert.Equal(1, viewModel.VisibleTaskCount);

        viewModel.SearchText = "нет такого";
        Assert.Equal(0, viewModel.VisibleTaskCount);
    }

    [Fact]
    public async Task Refresh_ОбновляетСтрокиНаМесте()
    {
        var taskId = Guid.NewGuid();
        var name = "Первое имя";
        var fake = new FakeIpcService
        {
            DashboardFactory = _ => Task.FromResult(new DashboardDto
            {
                Tasks = [new DashboardTaskDto { Id = taskId, Name = name, IsOwn = true, Enabled = true }],
            }),
        };

        var viewModel = new DashboardViewModel(fake);
        await viewModel.RefreshAsync();
        var row = viewModel.Tasks[0];

        name = "Новое имя";
        await viewModel.RefreshAsync();

        Assert.Same(row, viewModel.Tasks[0]);
        Assert.Equal("Новое имя", viewModel.Tasks[0].Name);
    }

    [Fact]
    public async Task ОшибкаСлужбы_ПоказываетсяВБаннереСТипизированнымКодом()
    {
        var fake = new FakeIpcService
        {
            DashboardFactory = _ => Task.FromException<DashboardDto>(
                new IpcException(IpcErrorCode.AuthDenied, "AUTH_DENIED", "Недостаточно прав")),
        };

        var viewModel = new DashboardViewModel(fake);
        await viewModel.RefreshAsync();

        Assert.True(viewModel.HasError);
        Assert.Equal(nameof(IpcErrorCode.AuthDenied), viewModel.ErrorCode);
        Assert.Equal("Недостаточно прав для выполнения действия.", viewModel.ErrorText);
    }

    [Fact]
    public async Task НедоступностьСлужбы_ВключаетИнструкциюЗапуска()
    {
        var fake = new FakeIpcService
        {
            DashboardFactory = _ => Task.FromException<DashboardDto>(
                new IpcUnavailableException("Служба недоступна")),
        };

        var viewModel = new DashboardViewModel(fake);
        await viewModel.RefreshAsync();

        Assert.True(viewModel.IsServiceUnavailable);
    }

    [Fact]
    public async Task Отмена_НеПоказываетОшибку()
    {
        var started = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var fake = new FakeIpcService
        {
            DashboardFactory = async token =>
            {
                started.SetResult();
                await Task.Delay(Timeout.Infinite, token);
                return new DashboardDto();
            },
        };

        var viewModel = new DashboardViewModel(fake);
        var refresh = viewModel.RefreshAsync();
        await started.Task;

        viewModel.CancelRefreshCommand.Execute(null);
        await refresh;

        Assert.False(viewModel.HasError);
        Assert.False(viewModel.IsLoading);
    }

    [Fact]
    public async Task Выбор_ВосстанавливаетсяИзСостояния()
    {
        var path = Path.Combine(AppContext.BaseDirectory, $"ui-state-{Guid.NewGuid():N}.json");
        try
        {
            var taskId = Guid.NewGuid();
            var fake = new FakeIpcService
            {
                DashboardFactory = _ => Task.FromResult(new DashboardDto
                {
                    Tasks = [new DashboardTaskDto { Id = taskId, Name = "Задача", IsOwn = true, Enabled = true }],
                }),
            };

            var state = new WindowStateService(path);
            var first = new DashboardViewModel(fake, windowState: state);
            await first.RefreshAsync();
            first.SelectedTask = first.Tasks[0];

            var restoredState = new WindowStateService(path);
            Assert.Equal(taskId, restoredState.State.LastSelectedTaskId);

            var second = new DashboardViewModel(fake, windowState: restoredState);
            await second.RefreshAsync();

            Assert.NotNull(second.SelectedTask);
            Assert.Equal(taskId, second.SelectedTask!.Id);
        }
        finally
        {
            File.Delete(path);
        }
    }

    [Fact]
    public async Task Статус_ОбновляетРольПользователя()
    {
        var elevation = new ElevationService();
        var fake = new FakeIpcService
        {
            StatusFactory = _ => Task.FromResult(new ServiceStatus
            {
                Running = true,
                IsAdmin = true,
                IsOperator = true,
                CanManageSystemTasks = true,
            }),
        };

        var viewModel = new DashboardViewModel(fake, elevation);
        await viewModel.RefreshStatusAsync();

        Assert.True(viewModel.IsAdmin);
        Assert.True(viewModel.CanManageSystemTasks);
        Assert.Equal("Администратор", viewModel.RoleText);
    }
}
