using System.IO;
using System.Text;
using System.Text.Json;
using WinSheduler.Shared;
using WinSheduler.Shared.Models;
using WinSheduler.UI.Services;
using WinSheduler.UI.Tests.Fakes;
using Xunit;

namespace WinSheduler.UI.Tests;

/// <summary>
/// X-3/X-4: экспорт и импорт задач через fake IPC. Реальная служба не нужна.
/// Временные файлы создаются только в ./temp/x-tests.
/// </summary>
public sealed class TaskExchangeServiceTests
{
    [Fact]
    public async Task Экспорт_НеСодержитСекретовИСлужебныхПолей()
    {
        var path = NewFilePath();

        try
        {
            var task = CreateTask();
            task.Password = "secret";
            task.RunAsPasswordEncrypted = "encrypted";
            task.OwnerSid = "S-1-5-21-1";

            var fake = new FakeIpcService
            {
                GetTaskFactory = (_, _) => Task.FromResult(task),
            };

            await new TaskExchangeService(fake).ExportAsync([task.Id], path);

            var bytes = await File.ReadAllBytesAsync(path);
            Assert.False(
                bytes.Length >= 3 && bytes[0] == 0xEF && bytes[1] == 0xBB && bytes[2] == 0xBF,
                "Файл не должен начинаться с BOM.");

            var json = Encoding.UTF8.GetString(bytes);
            Assert.False(json.Contains("\"password\"", StringComparison.OrdinalIgnoreCase), json);
            Assert.False(json.Contains("\"encryptedPassword\"", StringComparison.OrdinalIgnoreCase), json);
            Assert.False(json.Contains("\"runAsPasswordEncrypted\"", StringComparison.OrdinalIgnoreCase), json);
            Assert.False(json.Contains("\"owner\"", StringComparison.OrdinalIgnoreCase), json);
            Assert.False(json.Contains("\"createdAt\"", StringComparison.OrdinalIgnoreCase), json);
            Assert.False(json.Contains("\"modifiedAt\"", StringComparison.OrdinalIgnoreCase), json);
        }
        finally
        {
            Delete(path);
        }
    }

    [Fact]
    public async Task Экспорт_ОшибкаЗадачи_ФайлНеСоздаётся()
    {
        var path = NewFilePath();

        try
        {
            var fake = new FakeIpcService
            {
                GetTaskFactory = (_, _) => throw new IpcException(
                    IpcErrorCode.NotFound,
                    "NOT_FOUND",
                    "Задача не найдена."),
            };

            await Assert.ThrowsAsync<IpcException>(
                () => new TaskExchangeService(fake).ExportAsync([Guid.NewGuid()], path));

            Assert.False(File.Exists(path));
        }
        finally
        {
            Delete(path);
        }
    }

    [Fact]
    public async Task КруговаяПоездка_ПоляИТриггерыСовпадают()
    {
        var path = NewFilePath();

        try
        {
            var source = CreateTask();
            var exportFake = new FakeIpcService
            {
                GetTaskFactory = (_, _) => Task.FromResult(source),
            };

            await new TaskExchangeService(exportFake).ExportAsync([source.Id], path);

            var importFake = new FakeIpcService();
            var report = await new TaskExchangeService(importFake)
                .ImportAsync(path, TaskImportMode.Copy);

            Assert.Equal(1, report.Imported);
            Assert.Equal(0, report.Failed);

            var created = Assert.Single(importFake.CreatedTasks);
            Assert.NotEqual(source.Id, created.Id);
            Assert.Equal(source.Name, created.Name);
            Assert.Equal(source.Description, created.Description);
            Assert.Equal(source.Enabled, created.Enabled);
            Assert.Equal(source.ProgramPath, created.ProgramPath);
            Assert.Equal(source.Arguments, created.Arguments);
            Assert.Equal(source.WorkingDirectory, created.WorkingDirectory);
            Assert.Equal(source.WindowStyle, created.WindowStyle);
            Assert.Equal(source.SessionMode, created.SessionMode);
            Assert.Equal(source.OnError, created.OnError);
            Assert.Equal(source.RetryCount, created.RetryCount);
            Assert.Equal(source.OnOverlap, created.OnOverlap);
            Assert.Equal(source.TimeoutMinutes, created.TimeoutMinutes);
            Assert.Equal(source.KillOnTimeout, created.KillOnTimeout);
            Assert.Equal(source.LogOutput, created.LogOutput);
            Assert.Equal(source.MaxHistoryRecords, created.MaxHistoryRecords);
            Assert.Equal(source.TimeWindows.Count, created.TimeWindows.Count);
            Assert.All(created.TimeWindows, static window => Assert.Equal(Guid.Empty, window.Id));
            Assert.Equal(source.TimeWindows[0].ExactTimes, created.TimeWindows[0].ExactTimes);
            Assert.Equal(source.TimeWindows[0].DaysOfWeek, created.TimeWindows[0].DaysOfWeek);
        }
        finally
        {
            Delete(path);
        }
    }

    [Fact]
    public async Task Copy_СоздаётНовыйИдентификатор()
    {
        var path = NewFilePath();

        try
        {
            var source = CreateTask();
            await WriteDocumentAsync(path, source);

            var fake = new FakeIpcService();
            var report = await new TaskExchangeService(fake)
                .ImportAsync(path, TaskImportMode.Copy);

            Assert.Equal(1, report.Imported);
            var created = Assert.Single(fake.CreatedTasks);
            Assert.NotEqual(source.Id, created.Id);
        }
        finally
        {
            Delete(path);
        }
    }

    [Fact]
    public async Task Skip_ПропускаетСуществующийИдентификатор()
    {
        var path = NewFilePath();

        try
        {
            var source = CreateTask();
            await WriteDocumentAsync(path, source);

            var fake = new FakeIpcService
            {
                ListTasksFactory = _ => Task.FromResult<IReadOnlyList<TaskDto>>(
                [
                    new TaskDto { Id = source.Id, Name = source.Name },
                ]),
            };

            var report = await new TaskExchangeService(fake)
                .ImportAsync(path, TaskImportMode.Skip);

            Assert.Equal(1, report.Skipped);
            Assert.Empty(fake.CreatedTasks);
            Assert.Equal(1, fake.ListTasksCalls);
        }
        finally
        {
            Delete(path);
        }
    }

    [Fact]
    public async Task Overwrite_ОбновляетСвоюЗадачу()
    {
        var path = NewFilePath();

        try
        {
            var source = CreateTask();
            await WriteDocumentAsync(path, source);

            var fake = new FakeIpcService
            {
                GetTaskFactory = (id, _) => Task.FromResult(new TaskDto { Id = id, Name = source.Name }),
            };

            var report = await new TaskExchangeService(fake)
                .ImportAsync(path, TaskImportMode.Overwrite);

            Assert.Equal(1, report.Updated);
            Assert.Empty(fake.CreatedTasks);

            var updated = Assert.Single(fake.UpdatedTasks);
            Assert.Equal(source.Id, updated.Id);
            Assert.All(updated.TimeWindows, static window => Assert.Equal(Guid.Empty, window.Id));
        }
        finally
        {
            Delete(path);
        }
    }

    [Fact]
    public async Task Overwrite_НеТрогаетЧужуюЗадачу()
    {
        var path = NewFilePath();

        try
        {
            var source = CreateTask();
            await WriteDocumentAsync(path, source);

            var fake = new FakeIpcService
            {
                GetTaskFactory = (_, _) => throw new IpcException(
                    IpcErrorCode.AuthDenied,
                    "AUTH_DENIED",
                    "Недостаточно прав."),
            };

            var report = await new TaskExchangeService(fake)
                .ImportAsync(path, TaskImportMode.Overwrite);

            Assert.Equal(1, report.Imported);
            Assert.Empty(fake.UpdatedTasks);

            var created = Assert.Single(fake.CreatedTasks);
            Assert.NotEqual(source.Id, created.Id);
        }
        finally
        {
            Delete(path);
        }
    }

    [Fact]
    public async Task HasPassword_ВыключаетЗадачуИДобавляетПредупреждение()
    {
        var path = NewFilePath();

        try
        {
            var source = CreateTask();
            source.HasPassword = true;
            source.RunAsUser = "operator";
            source.Enabled = true;
            await WriteDocumentAsync(path, source);

            var fake = new FakeIpcService();
            var report = await new TaskExchangeService(fake)
                .ImportAsync(path, TaskImportMode.Copy);

            Assert.Equal(1, report.Imported);
            var created = Assert.Single(fake.CreatedTasks);
            Assert.False(created.Enabled);
            Assert.True(
                report.Warnings.Any(static warning =>
                    warning.Contains("пароль", StringComparison.OrdinalIgnoreCase)),
                string.Join("; ", report.Warnings));
        }
        finally
        {
            Delete(path);
        }
    }

    [Fact]
    public async Task ФорматВерсии2_ДаётПонятнуюОшибку()
    {
        var path = NewFilePath();

        try
        {
            await File.WriteAllTextAsync(
                path,
                """{"format":"winsheduler.tasks","formatVersion":2,"tasks":[]}""");

            var fake = new FakeIpcService();
            var exception = await Assert.ThrowsAsync<TaskExchangeException>(
                () => new TaskExchangeService(fake).ImportAsync(path, TaskImportMode.Copy));

            Assert.True(
                exception.Message.Contains("новой", StringComparison.OrdinalIgnoreCase),
                exception.Message);
            Assert.Empty(fake.CreatedTasks);
        }
        finally
        {
            Delete(path);
        }
    }

    [Fact]
    public async Task БитыйJson_ДаётПонятнуюОшибку()
    {
        var path = NewFilePath();

        try
        {
            await File.WriteAllTextAsync(path, "{ это не json");

            var fake = new FakeIpcService();
            var exception = await Assert.ThrowsAsync<TaskExchangeException>(
                () => new TaskExchangeService(fake).ImportAsync(path, TaskImportMode.Copy));

            Assert.True(
                exception.Message.Contains("JSON", StringComparison.OrdinalIgnoreCase)
                || exception.Message.Contains("поврежд", StringComparison.OrdinalIgnoreCase),
                exception.Message);
            Assert.Empty(fake.CreatedTasks);
        }
        finally
        {
            Delete(path);
        }
    }

    [Fact]
    public async Task ЛимитРазмераФайла_ПрерываетИмпорт()
    {
        var path = NewFilePath();

        try
        {
            await File.WriteAllTextAsync(path, new string('x', 100));

            var fake = new FakeIpcService();
            var options = new TaskExchangeOptions { MaxFileBytes = 16 };
            var exception = await Assert.ThrowsAsync<TaskExchangeException>(
                () => new TaskExchangeService(fake).ImportAsync(path, options));

            Assert.Contains("размер", exception.Message, StringComparison.OrdinalIgnoreCase);
            Assert.Empty(fake.CreatedTasks);
        }
        finally
        {
            Delete(path);
        }
    }

    [Fact]
    public async Task ЛимитыЗадачИТриггеров_Соблюдаются()
    {
        var path = NewFilePath();

        try
        {
            await WriteDocumentAsync(path, CreateTask(), CreateTask());

            var tooManyTasks = new TaskExchangeOptions { MaxTasks = 1 };
            await Assert.ThrowsAsync<TaskExchangeException>(
                () => new TaskExchangeService(new FakeIpcService()).ImportAsync(path, tooManyTasks));

            var fake = new FakeIpcService();
            var tooManyTriggers = new TaskExchangeOptions { MaxTriggersPerTask = 0 };
            var report = await new TaskExchangeService(fake).ImportAsync(path, tooManyTriggers);

            Assert.Equal(2, report.Failed);
            Assert.All(report.Items, static item => Assert.Equal("VALIDATION", item.ErrorCode));
            Assert.Empty(fake.CreatedTasks);
        }
        finally
        {
            Delete(path);
        }
    }

    [Fact]
    public async Task ЛимитДлиныСтроки_ДаётFailedБезВызоваCreate()
    {
        var path = NewFilePath();

        try
        {
            var source = CreateTask();
            source.Name = new string('n', 50);
            await WriteDocumentAsync(path, source);

            var fake = new FakeIpcService();
            var options = new TaskExchangeOptions { MaxStringLength = 10 };
            var report = await new TaskExchangeService(fake).ImportAsync(path, options);

            Assert.Equal(0, report.Imported);
            var item = Assert.Single(report.Items);
            Assert.Equal(TaskImportOutcome.Failed, item.Outcome);
            Assert.Equal("VALIDATION", item.ErrorCode);
            Assert.Empty(fake.CreatedTasks);
        }
        finally
        {
            Delete(path);
        }
    }

    [Fact]
    public async Task Unavailable_ПрерываетИмпорт()
    {
        var path = NewFilePath();

        try
        {
            await WriteDocumentAsync(path, CreateTask(), CreateTask());

            var calls = 0;
            var fake = new FakeIpcService
            {
                CreateTaskFactory = (task, _) =>
                {
                    if (Interlocked.Increment(ref calls) == 1)
                    {
                        throw new IpcUnavailableException("Служба недоступна.");
                    }

                    return Task.FromResult(task);
                },
            };

            var report = await new TaskExchangeService(fake)
                .ImportAsync(path, TaskImportMode.Copy);

            Assert.Equal(1, report.Failed);
            Assert.Equal(0, report.Imported);
            Assert.Single(report.Items);
            Assert.Empty(fake.CreatedTasks);
            Assert.True(
                report.Warnings.Any(static warning =>
                    warning.Contains("прерван", StringComparison.OrdinalIgnoreCase)),
                string.Join("; ", report.Warnings));
        }
        finally
        {
            Delete(path);
        }
    }

    private static TaskDto CreateTask()
    {
        return new TaskDto
        {
            Id = Guid.NewGuid(),
            Name = "Ночной бэкап",
            Description = "Копирование на NAS",
            Enabled = true,
            ProgramPath = "d:/scripts/backup.cmd",
            Arguments = "--full",
            WorkingDirectory = "d:/scripts",
            WindowStyle = WindowStyle.Hidden,
            SessionMode = SessionMode.Headless,
            OnError = OnErrorAction.Retry,
            RetryCount = 2,
            OnOverlap = OnOverlapAction.WaitComplete,
            TimeoutMinutes = 60,
            KillOnTimeout = true,
            LogOutput = true,
            MaxHistoryRecords = 1000,
            TimeWindows =
            [
                new TimeWindowDto
                {
                    Id = Guid.NewGuid(),
                    Type = TimeWindowType.ExactTimes,
                    Kind = TriggerKind.Daily,
                    ExactTimes = ["02:00"],
                    DayInterval = 1,
                },
                new TimeWindowDto
                {
                    Id = Guid.NewGuid(),
                    Type = TimeWindowType.ExactTimes,
                    Kind = TriggerKind.Weekly,
                    DaysOfWeek = [DayOfWeek.Monday, DayOfWeek.Wednesday],
                    ExactTimes = ["09:30"],
                    WeekInterval = 1,
                },
            ],
        };
    }

    private static async Task WriteDocumentAsync(string path, params TaskDto[] tasks)
    {
        var exchangeTasks = tasks.Select(TaskExchangeTask.FromTaskDto).ToList();
        var document = TaskExchangeJson.CreateDocument("2.1.0", exchangeTasks);
        var json = JsonSerializer.Serialize(document, TaskExchangeJson.Options);

        await File.WriteAllTextAsync(
            path,
            json,
            new UTF8Encoding(encoderShouldEmitUTF8Identifier: false));
    }

    private static string NewFilePath()
    {
        var directory = Path.Combine(ProjectRoot(), "temp", "x-tests");
        Directory.CreateDirectory(directory);
        return Path.Combine(directory, $"{Guid.NewGuid():N}.wstasks.json");
    }

    private static string ProjectRoot()
    {
        var directory = new DirectoryInfo(AppContext.BaseDirectory);
        while (directory is not null
               && !File.Exists(Path.Combine(directory.FullName, "WinSheduler.sln")))
        {
            directory = directory.Parent;
        }

        return directory?.FullName
               ?? throw new InvalidOperationException("Не найден корень проекта (WinSheduler.sln).");
    }

    private static void Delete(string path)
    {
        if (File.Exists(path))
        {
            File.Delete(path);
        }
    }
}
