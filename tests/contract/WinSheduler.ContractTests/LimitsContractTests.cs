using System.Text.Json;
using Xunit;

namespace WinSheduler.ContractTests;

/// <summary>
/// Лимиты из ./docs/contracts/IPC.md, п. 1 и ./docs/contracts/SECURITY.md, п. 3.
/// </summary>
public class LimitsContractTests
{
    [Fact]
    public void КонстантыЛимитов_СовпадаютСКонтрактом()
    {
        Assert.Equal(1024 * 1024, IpcFrame.MaxFrameBytes);
        Assert.Equal(64, IpcFrame.MaxJsonDepth);
        Assert.Equal(64 * 1024, IpcFrame.MaxStringBytes);
    }

    [Fact]
    public void ГлубинаJson64_Разбирается()
    {
        var text = new string('[', 64) + new string(']', 64);

        using var document = JsonDocument.Parse(text, new JsonDocumentOptions
        {
            MaxDepth = IpcFrame.MaxJsonDepth,
        });

        Assert.Equal(JsonValueKind.Array, document.RootElement.ValueKind);
    }

    [Fact]
    public void ГлубинаJson65_Отклоняется()
    {
        var text = new string('[', 65) + new string(']', 65);

        Assert.ThrowsAny<JsonException>(() => JsonDocument.Parse(text, new JsonDocumentOptions
        {
            MaxDepth = IpcFrame.MaxJsonDepth,
        }));
    }

    [Fact]
    public void Строка64КиБ_УкладываетсяВЛимит()
    {
        var text = "{\"s\":\"" + new string('a', IpcFrame.MaxStringBytes) + "\"}";

        using var document = JsonDocument.Parse(text);
        var value = document.RootElement.GetProperty("s").GetString();

        Assert.NotNull(value);
        Assert.Equal(IpcFrame.MaxStringBytes, value!.Length);
    }

    [Fact]
    public void Фикстуры_НеПревышаютЛимитКадра()
    {
        foreach (var file in Directory.EnumerateFiles(ContractFixtures.FixturesDirectory, "*.json"))
        {
            var length = new FileInfo(file).Length;
            Assert.True(
                length <= IpcFrame.MaxFrameBytes,
                $"{Path.GetFileName(file)}: {length} байт больше лимита кадра");
        }
    }
}
