using System.Text.Json;
using Xunit;

namespace WinSheduler.ContractTests;

/// <summary>
/// Формы DTO из ./docs/contracts/IPC.md, п. 5.
/// </summary>
public class DtoContractTests
{
    private static readonly string[] TriggerKinds =
        ["Once", "Daily", "Weekly", "Monthly", "Interval"];

    private static readonly string[] RunStatuses =
        ["Running", "Completed", "Failed", "Killed", "Timeout", "Aborted"];

    private static readonly string[] WindowStyles =
        ["Normal", "Hidden", "Minimized", "Maximized"];

    [Fact]
    public void TaskDto_ОбязательныеПоляПрисутствуют()
    {
        using var document = ContractFixtures.ParseFixture("task-dto.json");
        var root = document.RootElement;

        Assert.True(Guid.TryParse(root.GetProperty("id").GetString(), out _));
        Assert.False(string.IsNullOrWhiteSpace(root.GetProperty("name").GetString()));

        foreach (var field in new[]
                 {
                     "description", "programPath", "arguments", "workingDirectory",
                     "windowStyle", "sessionMode", "onError", "onOverlap", "timeoutMinutes",
                     "killOnTimeout", "logOutput", "maxHistoryRecords", "timeWindows",
                     "createdAt", "modifiedAt", "owner", "isOwn", "enabled", "hasPassword",
                 })
        {
            Assert.True(root.TryGetProperty(field, out _), $"TaskDto: нет поля {field}");
        }

        Assert.Contains(root.GetProperty("windowStyle").GetString(), WindowStyles);
        Assert.Equal("Headless", root.GetProperty("sessionMode").GetString());
        Assert.Equal(JsonValueKind.Object, root.GetProperty("owner").ValueKind);
        Assert.True(root.GetProperty("isOwn").GetBoolean());
    }

    [Fact]
    public void TaskDto_НеСодержитСекретов()
    {
        // SECURITY.md, п. 4: в ответах только hasPassword, пароль не возвращается.
        using var document = ContractFixtures.ParseFixture("task-dto.json");
        var root = document.RootElement;

        Assert.False(root.TryGetProperty("password", out _));
        Assert.False(root.TryGetProperty("encryptedPassword", out _));
        Assert.False(root.TryGetProperty("ownerSid", out _));

        // SID не должен попадать в фикстуры: в owner только isOwn и label.
        var owner = root.GetProperty("owner");
        Assert.False(owner.TryGetProperty("sid", out _));
    }

    [Fact]
    public void TimeWindowDto_ТипИИнтервалыСогласованы()
    {
        using var document = ContractFixtures.ParseFixture("time-window-dto.json");
        var root = document.RootElement;

        Assert.Contains(root.GetProperty("kind").GetString(), TriggerKinds);
        Assert.Equal(1, root.GetProperty("dayInterval").GetInt32());
        Assert.Equal(1, root.GetProperty("weekInterval").GetInt32());
        Assert.Equal(1, root.GetProperty("monthInterval").GetInt32());

        var start = root.GetProperty("startTime").GetString() ?? "";
        var end = root.GetProperty("endTime").GetString() ?? "";
        Assert.True(string.CompareOrdinal(start, end) < 0, "Для Interval нужен startTime < endTime");

        var interval = root.GetProperty("repeatIntervalMinutes").GetInt32();
        Assert.InRange(interval, 1, 1440);
    }

    [Fact]
    public void RunHistoryDto_СтатусИВремяСогласованы()
    {
        using var document = ContractFixtures.ParseFixture("run-history-dto.json");
        var root = document.RootElement;

        Assert.True(Guid.TryParse(root.GetProperty("id").GetString(), out _));
        Assert.True(Guid.TryParse(root.GetProperty("taskId").GetString(), out _));
        Assert.Contains(root.GetProperty("status").GetString(), RunStatuses);
        Assert.Contains(root.GetProperty("triggeredBy").GetString(), new[] { "Schedule", "Manual" });

        var start = root.GetProperty("startTime").GetString() ?? "";
        Assert.EndsWith("Z", start, StringComparison.Ordinal);
        Assert.True(DateTime.TryParse(start, out _));
    }

    [Fact]
    public void ServiceStatus_СодержитРолиИСчётчики()
    {
        using var document = ContractFixtures.ParseFixture("service-status.json");
        var root = document.RootElement;

        foreach (var field in new[]
                 {
                     "running", "isAdmin", "isOperator", "canManageSystemTasks",
                     "serviceVersion", "startedAt", "taskCount", "runningTaskCount", "dbReady",
                 })
        {
            Assert.True(root.TryGetProperty(field, out _), $"ServiceStatus: нет поля {field}");
        }

        Assert.EndsWith("Z", root.GetProperty("startedAt").GetString() ?? "", StringComparison.Ordinal);
    }

    [Fact]
    public void Фикстуры_НеСодержатПаролейИSid()
    {
        // Гигиена тестовых данных: никаких реальных секретов и SID.
        foreach (var file in Directory.EnumerateFiles(ContractFixtures.FixturesDirectory, "*.json"))
        {
            var text = File.ReadAllText(file);
            var name = Path.GetFileName(file);

            Assert.DoesNotContain("\"password\"", text, StringComparison.OrdinalIgnoreCase);
            Assert.DoesNotContain("S-1-5-", text, StringComparison.OrdinalIgnoreCase);
            Assert.DoesNotContain("encryptedPassword", text, StringComparison.OrdinalIgnoreCase);
        }
    }
}
