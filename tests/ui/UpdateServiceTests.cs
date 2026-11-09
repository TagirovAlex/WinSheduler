using System.Text.Json;
using WinSheduler.UI.Services;
using Xunit;

namespace WinSheduler.UI.Tests;

/// <summary>Проверка разбора манифеста и выбора источника обновлений.</summary>
public class UpdateServiceTests
{
    private const string Sha256 = "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA";

    private static readonly string ManifestNewer =
        $$"""{"version":"2.1.3","url":"//server/share/WinSheduler-2.1.3.msi","sha256":"{{Sha256}}","size":52428800}""";

    [Fact]
    public void Evaluate_НоваяВерсия_Доступна()
    {
        var result = UpdateService.Evaluate(ManifestNewer, "2.1.2.0");

        Assert.Equal(UpdateCheckStatus.Available, result.Status);
        Assert.Equal("2.1.3", result.Version);
    }

    [Fact]
    public void Evaluate_ТаЖеВерсия_Актуальна()
        => Assert.Equal(
            UpdateCheckStatus.UpToDate,
            UpdateService.Evaluate(ManifestNewer, "2.1.3.0").Status);

    [Fact]
    public void Evaluate_БезВерсии_ОшибкаМанифеста()
        => Assert.Throws<JsonException>(
            () => UpdateService.Evaluate("""{"product":"WinSheduler"}""", "2.1.2.0"));

    [Fact]
    public void Evaluate_НеверныйSha256_ОшибкаМанифеста()
    {
        var json = """{"version":"2.1.3","url":"//server/share/a.msi","sha256":"abc","size":1}""";

        Assert.Throws<JsonException>(() => UpdateService.Evaluate(json, "2.1.2.0"));
    }

    [Fact]
    public void Evaluate_ЛокальныйПуть_ОшибкаМанифеста()
    {
        var json =
            $$"""{"version":"2.1.3","url":"c:/temp/a.msi","sha256":"{{Sha256}}","size":1}""";

        Assert.Throws<JsonException>(() => UpdateService.Evaluate(json, "2.1.2.0"));
    }

    [Fact]
    public void UpdateUrl_Пустой_ИсточникНеНастроен()
    {
        var service = new UpdateService(() => "  ");

        Assert.False(service.IsConfigured);
        Assert.Null(service.UpdateUrl);
    }

    [Fact]
    public void FormatSize_Мегабайты()
        => Assert.Equal("50,0 МБ", UpdateService.FormatSize(52428800));
}
