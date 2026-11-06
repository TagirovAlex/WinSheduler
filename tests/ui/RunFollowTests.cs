using WinSheduler.Shared;
using WinSheduler.Shared.Models;
using WinSheduler.UI.Services;
using WinSheduler.UI.Tests.Fakes;
using WinSheduler.UI.ViewModels;
using Xunit;

namespace WinSheduler.UI.Tests;

/// <summary>
/// Y-1…Y-3: тихое обновление и автосопровождение запущенных задач.
/// Тесты используют только fake IPC и короткие интервалы; реальная служба не нужна.
/// </summary>
public class RunFollowTests
{
    /// <summary>Короткие интервалы, чтобы сценарии выполнялись за доли секунды.</summary>
    private static RunFollowOptions FastOptions => new()
    {
        ShortInterval = TimeSpan.FromMilliseconds(10),
        LongInterval = TimeSpan.FromMilliseconds(10),
        ShortPhase = TimeSpan.FromSeconds(5),
        StartGrace = TimeSpan.FromMilliseconds(60),
        MaxDuration = TimeSpan.FromSeconds(10),
    };

    [Fact]
    public async Task Запуск_Завершается_СтрокаОбновляетсяБезF5()
    {
        var taskId = Guid.NewGuid();
        var runFinished = new TaskCompletionSource<RunFinishedEventArgs>(
            TaskCreationOptions.RunContinuationsAsynchronously);
        var calls = 0;

        var fake = new FakeIpcService
        {
            DashboardFactory = _ => Task.FromResult(Dashboard(taskId, running: true)),
            HistoryFactory = (_, _, _) => Task.FromResult<IReadOnlyList<RunHistory>>(
            [
                Run(taskId, Interlocked.Increment(ref calls) <= 2 ? RunStatus.Running : RunStatus.Completed),
            ]),
        };

        var viewModel = new DashboardViewModel(fake, runFollowOptions: FastOptions);
        viewModel.RunFinished += (_, args) => runFinished.TrySetResult(args);

        try
        {
            await viewModel.RefreshAsync();
            var row = viewModel.Tasks[0];
            Assert.True(row.Running);

            var finished = await WithTimeout(runFinished.Task);

            Assert.Equal(taskId, finished.TaskId);
            Assert.Equal(RunStatus.Completed, finished.RunStatus);
            Assert.Equal(0, finished.ExitCode);
            Assert.False(row.Running);
            Assert.Equal(RunStatus.Completed, row.LastRunStatus);
            Assert.False(row.HasRunError);
            Assert.False(viewModel.IsLoading);
        }
        finally
        {
            viewModel.StopFollowing();
        }
    }

    [Fact]
    public async Task Запуск_СОшибкой_ПоказываетFailed()
    {
        var taskId = Guid.NewGuid();
        var runFinished = new TaskCompletionSource<RunFinishedEventArgs>(
            TaskCreationOptions.RunContinuationsAsynchronously);
        var calls = 0;

        var fake = new FakeIpcService
        {
            DashboardFactory = _ => Task.FromResult(Dashboard(taskId, running: true)),
            HistoryFactory = (_, _, _) => Task.FromResult<IReadOnlyList<RunHistory>>(
            [
                Run(taskId, Interlocked.Increment(ref calls) <= 2 ? RunStatus.Running : RunStatus.Failed, exitCode: 3),
            ]),
        };

        var viewModel = new DashboardViewModel(fake, runFollowOptions: FastOptions);
        viewModel.RunFinished += (_, args) => runFinished.TrySetResult(args);

        try
        {
            await viewModel.RefreshAsync();
            var row = viewModel.Tasks[0];

            var finished = await WithTimeout(runFinished.Task);

            Assert.Equal(RunStatus.Failed, finished.RunStatus);
            Assert.Equal(3, finished.ExitCode);
            Assert.False(row.Running);
            Assert.Equal(RunStatus.Failed, row.LastRunStatus);
            Assert.True(row.HasRunError);
        }
        finally
        {
            viewModel.StopFollowing();
        }
    }

    [Fact]
    public async Task ГонкаСтарта_ПервыйRunningFalseНеОстанавливаетЦикл()
    {
        var taskId = Guid.NewGuid();
        var runFinished = new TaskCompletionSource<RunFinishedEventArgs>(
            TaskCreationOptions.RunContinuationsAsynchronously);
        var calls = 0;

        var fake = new FakeIpcService
        {
            DashboardFactory = _ => Task.FromResult(Dashboard(taskId, running: true)),
            HistoryFactory = (_, _, _) =>
            {
                var call = Interlocked.Increment(ref calls);

                // Первый ответ — ещё не запущенная задача, затем запуск и завершение.
                var status = call switch
                {
                    1 => RunStatus.Completed,
                    2 => RunStatus.Running,
                    _ => RunStatus.Completed,
                };

                return Task.FromResult<IReadOnlyList<RunHistory>>([Run(taskId, status)]);
            },
        };

        var viewModel = new DashboardViewModel(fake, runFollowOptions: FastOptions);
        viewModel.RunFinished += (_, args) => runFinished.TrySetResult(args);

        try
        {
            await viewModel.RefreshAsync();

            var finished = await WithTimeout(runFinished.Task);

            Assert.Equal(RunStatus.Completed, finished.RunStatus);
            Assert.True(calls >= 3, $"Опрос остановился слишком рано: вызовов {calls}.");
        }
        finally
        {
            viewModel.StopFollowing();
        }
    }

    [Fact]
    public async Task TrackRun_ЗапускаетСопровождениеБезОбновленияДашборда()
    {
        var taskId = Guid.NewGuid();
        var runFinished = new TaskCompletionSource<RunFinishedEventArgs>(
            TaskCreationOptions.RunContinuationsAsynchronously);
        var calls = 0;

        var fake = new FakeIpcService
        {
            DashboardFactory = _ => Task.FromResult(Dashboard(taskId, running: false)),
            HistoryFactory = (_, _, _) => Task.FromResult<IReadOnlyList<RunHistory>>(
            [
                Run(taskId, Interlocked.Increment(ref calls) <= 2 ? RunStatus.Running : RunStatus.Completed),
            ]),
        };

        var viewModel = new DashboardViewModel(fake, runFollowOptions: FastOptions);
        viewModel.RunFinished += (_, args) => runFinished.TrySetResult(args);

        try
        {
            await viewModel.RefreshAsync();
            Assert.False(viewModel.Tasks[0].Running);
            Assert.Equal(0, fake.HistoryCalls);

            viewModel.TrackRun(taskId);

            var finished = await WithTimeout(runFinished.Task);

            Assert.Equal(RunStatus.Completed, finished.RunStatus);
            Assert.Equal(RunStatus.Completed, viewModel.Tasks[0].LastRunStatus);
            Assert.True(fake.HistoryCalls >= 3);
        }
        finally
        {
            viewModel.StopFollowing();
        }
    }

    [Fact]
    public async Task TrackRun_СтараяИсторияНеДаётЛожногоЗавершения()
    {
        var taskId = Guid.NewGuid();
        var finished = false;

        var fake = new FakeIpcService
        {
            DashboardFactory = _ => Task.FromResult(Dashboard(taskId, running: false)),
            HistoryFactory = (_, _, _) => Task.FromResult<IReadOnlyList<RunHistory>>(
            [
                new RunHistory
                {
                    TaskId = taskId,
                    StartTime = DateTime.Now.AddHours(-1),
                    Status = RunStatus.Failed,
                    ExitCode = 1,
                },
            ]),
        };

        var viewModel = new DashboardViewModel(fake, runFollowOptions: FastOptions);
        viewModel.RunFinished += (_, _) => finished = true;

        try
        {
            await viewModel.RefreshAsync();
            viewModel.TrackRun(taskId);

            await Task.Delay(200);

            Assert.False(finished);
            Assert.False(viewModel.HasError);
            Assert.True(fake.HistoryCalls >= 2, "Цикл должен продолжать ждать новый запуск.");
        }
        finally
        {
            viewModel.StopFollowing();
        }
    }

    [Fact]
    public async Task ДвеЗадачи_ЗавершениеОднойНеОстанавливаетДругую()
    {
        var firstId = Guid.NewGuid();
        var secondId = Guid.NewGuid();
        var firstFinished = new TaskCompletionSource<RunFinishedEventArgs>(
            TaskCreationOptions.RunContinuationsAsynchronously);
        var secondFinished = new TaskCompletionSource<RunFinishedEventArgs>(
            TaskCreationOptions.RunContinuationsAsynchronously);
        var firstCalls = 0;
        var secondCalls = 0;

        var fake = new FakeIpcService
        {
            DashboardFactory = _ => Task.FromResult(new DashboardDto
            {
                Tasks =
                [
                    new DashboardTaskDto { Id = firstId, Name = "Первая", IsOwn = true, Enabled = true, Running = true },
                    new DashboardTaskDto { Id = secondId, Name = "Вторая", IsOwn = true, Enabled = true, Running = true },
                ],
            }),
            HistoryFactory = (taskId, _, _) => Task.FromResult<IReadOnlyList<RunHistory>>(
            [
                taskId == firstId
                    ? Run(firstId, Interlocked.Increment(ref firstCalls) <= 2
                        ? RunStatus.Running
                        : RunStatus.Completed)
                    : Run(secondId, Interlocked.Increment(ref secondCalls) <= 20
                        ? RunStatus.Running
                        : RunStatus.Completed),
            ]),
        };

        var viewModel = new DashboardViewModel(fake, runFollowOptions: FastOptions);
        viewModel.RunFinished += (_, args) =>
        {
            if (args.TaskId == firstId)
            {
                firstFinished.TrySetResult(args);
            }
            else if (args.TaskId == secondId)
            {
                secondFinished.TrySetResult(args);
            }
        };

        try
        {
            await viewModel.RefreshAsync();

            var first = await WithTimeout(firstFinished.Task);
            Assert.Equal(RunStatus.Completed, first.RunStatus);

            // Вторая задача всё ещё выполняется и продолжает опрашиваться.
            var second = viewModel.Tasks.Single(task => task.Id == secondId);
            Assert.True(second.Running);

            var secondResult = await WithTimeout(secondFinished.Task);
            Assert.Equal(RunStatus.Completed, secondResult.RunStatus);
            Assert.False(second.Running);
        }
        finally
        {
            viewModel.StopFollowing();
        }
    }

    [Fact]
    public async Task НедоступностьСлужбы_ОстанавливаетЦиклИПоказываетОшибкуОдинРаз()
    {
        var taskId = Guid.NewGuid();
        var finished = false;

        var fake = new FakeIpcService
        {
            DashboardFactory = _ => Task.FromResult(Dashboard(taskId, running: true)),
            HistoryFactory = (_, _, _) => Task.FromException<IReadOnlyList<RunHistory>>(
                new IpcUnavailableException("Служба недоступна")),
        };

        var viewModel = new DashboardViewModel(fake, runFollowOptions: FastOptions);
        viewModel.RunFinished += (_, _) => finished = true;

        try
        {
            await viewModel.RefreshAsync();

            await WaitUntilAsync(() => viewModel.IsServiceUnavailable);
            var calls = fake.HistoryCalls;

            // Цикл остановлен: новых опросов нет, ошибка не спамит.
            await Task.Delay(150);
            Assert.Equal(calls, fake.HistoryCalls);
            Assert.False(finished);
            Assert.True(viewModel.HasError);
        }
        finally
        {
            viewModel.StopFollowing();
        }
    }

    [Fact]
    public async Task Отмена_ОстанавливаетЦиклБезОшибки()
    {
        var taskId = Guid.NewGuid();
        var started = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var cancelled = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var finished = false;

        var fake = new FakeIpcService
        {
            DashboardFactory = _ => Task.FromResult(Dashboard(taskId, running: true)),
            HistoryFactory = async (_, _, token) =>
            {
                started.TrySetResult();
                try
                {
                    await Task.Delay(Timeout.Infinite, token);
                }
                catch (OperationCanceledException)
                {
                    cancelled.TrySetResult();
                    throw;
                }

                return [];
            },
        };

        var viewModel = new DashboardViewModel(fake, runFollowOptions: FastOptions);
        viewModel.RunFinished += (_, _) => finished = true;

        try
        {
            await viewModel.RefreshAsync();
            await WithTimeout(started.Task, 2000);

            viewModel.StopFollowing();
            await WithTimeout(cancelled.Task, 2000);

            Assert.False(finished);
            Assert.False(viewModel.HasError);
        }
        finally
        {
            viewModel.StopFollowing();
        }
    }

    [Fact]
    public async Task ТихоеОбновление_НеВключаетIsLoadingИНеЗатираетОшибку()
    {
        var taskId = Guid.NewGuid();
        var gate = new TaskCompletionSource<DashboardDto>(TaskCreationOptions.RunContinuationsAsynchronously);

        var fake = new FakeIpcService
        {
            DashboardFactory = _ => Task.FromException<DashboardDto>(
                new IpcException(IpcErrorCode.AuthDenied, "AUTH_DENIED", "Недостаточно прав")),
        };

        var viewModel = new DashboardViewModel(fake, runFollowOptions: FastOptions);
        await viewModel.RefreshAsync();
        Assert.True(viewModel.HasError);

        fake.DashboardFactory = _ => gate.Task;
        var silent = viewModel.RefreshSilentlyAsync();

        Assert.False(viewModel.IsLoading);
        Assert.True(viewModel.HasError);

        gate.SetResult(new DashboardDto
        {
            Tasks = [new DashboardTaskDto { Id = taskId, Name = "Обновлено", IsOwn = true, Enabled = true }],
        });
        await silent;

        Assert.False(viewModel.IsLoading);
        Assert.True(viewModel.HasError);
        Assert.Equal("Обновлено", viewModel.Tasks[0].Name);
    }

    private static DashboardDto Dashboard(Guid taskId, bool running) => new()
    {
        Tasks =
        [
            new DashboardTaskDto
            {
                Id = taskId,
                Name = "Задача",
                IsOwn = true,
                Enabled = true,
                Running = running,
            },
        ],
    };

    private static RunHistory Run(Guid taskId, RunStatus status, int? exitCode = 0) => new()
    {
        TaskId = taskId,
        StartTime = DateTime.Now,
        Status = status,
        ExitCode = status == RunStatus.Running ? null : exitCode,
    };

    private static async Task WithTimeout(Task task, int milliseconds = 5000)
    {
        var completed = await Task.WhenAny(task, Task.Delay(milliseconds));
        if (completed != task)
        {
            throw new TimeoutException("Сценарий не завершился за отведённое время.");
        }

        await task;
    }

    private static async Task<T> WithTimeout<T>(Task<T> task, int milliseconds = 5000)
    {
        var completed = await Task.WhenAny(task, Task.Delay(milliseconds));
        if (completed != task)
        {
            throw new TimeoutException("Сценарий не завершился за отведённое время.");
        }

        return await task;
    }

    private static async Task WaitUntilAsync(Func<bool> condition, int milliseconds = 5000)
    {
        var deadline = DateTime.UtcNow.AddMilliseconds(milliseconds);
        while (!condition())
        {
            if (DateTime.UtcNow > deadline)
            {
                throw new TimeoutException("Условие не выполнилось за отведённое время.");
            }

            await Task.Delay(10);
        }
    }
}
