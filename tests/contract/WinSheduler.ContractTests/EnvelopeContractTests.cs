using System.Text.Json;
using System.Text.Json.Nodes;
using Xunit;

namespace WinSheduler.ContractTests;

/// <summary>
/// Конверт IPC v1 и legacy-режим (./docs/contracts/IPC.md, п. 2 и 7).
/// Фикстуры общие с C++ тестами, поэтому обе стороны проверяют один формат.
/// </summary>
public class EnvelopeContractTests
{
    private static readonly string[] AllowedErrorCodes =
    [
        "AUTH_DENIED",
        "NOT_FOUND",
        "VALIDATION",
        "CONFLICT",
        "TOO_LARGE",
        "UNSUPPORTED_VERSION",
        "DB",
        "INTERNAL",
    ];

    [Fact]
    public void Запрос_СоответствуетКонвертуV1()
    {
        using var document = ContractFixtures.ParseFixture("ipc-request.json");
        var root = document.RootElement;

        Assert.Equal(1, root.GetProperty("v").GetInt32());
        Assert.True(Guid.TryParse(root.GetProperty("id").GetString(), out _));
        Assert.False(string.IsNullOrWhiteSpace(root.GetProperty("action").GetString()));
        Assert.Equal(JsonValueKind.Object, root.GetProperty("payload").ValueKind);
    }

    [Fact]
    public void УспешныйОтвет_СодержитOkИPayload()
    {
        using var document = ContractFixtures.ParseFixture("ipc-response-ok.json");
        var root = document.RootElement;

        Assert.Equal(1, root.GetProperty("v").GetInt32());
        Assert.True(Guid.TryParse(root.GetProperty("id").GetString(), out _));
        Assert.True(root.GetProperty("ok").GetBoolean());
        Assert.Equal(JsonValueKind.Null, root.GetProperty("error").ValueKind);
        Assert.Equal(JsonValueKind.Object, root.GetProperty("payload").ValueKind);
    }

    [Fact]
    public void ОтветСОшибкой_СодержитКодИзКонтракта()
    {
        using var document = ContractFixtures.ParseFixture("ipc-response-error.json");
        var root = document.RootElement;

        Assert.False(root.GetProperty("ok").GetBoolean());
        Assert.Equal(JsonValueKind.Null, root.GetProperty("payload").ValueKind);

        var error = root.GetProperty("error");
        Assert.Equal(JsonValueKind.Object, error.ValueKind);

        var code = error.GetProperty("code").GetString() ?? "";
        Assert.Contains(code, AllowedErrorCodes);
        Assert.False(string.IsNullOrWhiteSpace(error.GetProperty("message").GetString()));
    }

    [Fact]
    public void IdОтвета_СовпадаетСIdЗапроса()
    {
        using var request = ContractFixtures.ParseFixture("ipc-request.json");
        using var response = ContractFixtures.ParseFixture("ipc-response-ok.json");

        Assert.Equal(
            request.RootElement.GetProperty("id").GetString(),
            response.RootElement.GetProperty("id").GetString());
    }

    [Fact]
    public void LegacyЗапрос_БезВерсииИСПолемAction()
    {
        using var document = ContractFixtures.ParseFixture("ipc-legacy-request.json");
        var root = document.RootElement;

        Assert.False(root.TryGetProperty("v", out _));
        Assert.False(string.IsNullOrWhiteSpace(root.GetProperty("Action").GetString()));
        Assert.True(root.TryGetProperty("Payload", out var payload));
        Assert.Equal(JsonValueKind.String, payload.ValueKind);
    }

    [Fact]
    public void LegacyОтвет_СодержитSuccessErrorPayload()
    {
        using var document = ContractFixtures.ParseFixture("ipc-legacy-response.json");
        var root = document.RootElement;

        Assert.False(root.TryGetProperty("v", out _));
        Assert.True(root.GetProperty("Success").GetBoolean());
        Assert.Equal(JsonValueKind.String, root.GetProperty("Error").ValueKind);
        Assert.Equal(JsonValueKind.String, root.GetProperty("Payload").ValueKind);
    }

    [Theory]
    [InlineData("ipc-request.json")]
    [InlineData("ipc-response-ok.json")]
    [InlineData("ipc-response-error.json")]
    [InlineData("ipc-legacy-request.json")]
    [InlineData("ipc-legacy-response.json")]
    [InlineData("task-dto.json")]
    [InlineData("time-window-dto.json")]
    [InlineData("run-history-dto.json")]
    [InlineData("service-status.json")]
    public void Фикстура_ПереживаетRoundTripБезПотериПолей(string name)
    {
        var original = JsonNode.Parse(ContractFixtures.ReadFixture(name));
        Assert.NotNull(original);

        var serialized = original.ToJsonString();
        var restored = JsonNode.Parse(serialized);

        Assert.NotNull(restored);
        Assert.True(JsonNode.DeepEquals(original, restored));
    }

    [Fact]
    public void НеизвестныеПоля_НеЛомаютРазборКонверта()
    {
        // IPC.md, п. 4: неизвестные поля игнорируются (прямая совместимость).
        // Добавляем поле через JsonNode, чтобы тест не зависел от переводов строк.
        var request = JsonNode.Parse(ContractFixtures.ReadFixture("ipc-request.json"));
        Assert.NotNull(request);
        request!["futureField"] = "x";

        using var document = JsonDocument.Parse(request.ToJsonString());
        var root = document.RootElement;

        Assert.Equal(1, root.GetProperty("v").GetInt32());
        Assert.True(root.TryGetProperty("futureField", out _));
        Assert.Equal("GetStatus", root.GetProperty("action").GetString());
    }
}
