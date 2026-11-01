using System.Text.Json;

namespace WinSheduler.ContractTests;

/// <summary>
/// Доступ к общим фикстурам и схемам контрактов.
/// Каталоги ищутся от каталога сборки вверх до корня репозитория, поэтому тесты
/// не зависят от конфигурации (Debug/Release) и от текущего каталога запуска.
/// </summary>
public static class ContractFixtures
{
    /// <summary>Корень репозитория (каталог с ./WinSheduler.sln).</summary>
    public static string RepositoryRoot { get; } = FindRepositoryRoot();

    /// <summary>Каталог общих фикстур ./tests/contract/fixtures.</summary>
    public static string FixturesDirectory { get; } =
        Path.Combine(RepositoryRoot, "tests", "contract", "fixtures");

    /// <summary>Каталог JSON-схем ./docs/contracts/schemas (зона Architect).</summary>
    public static string SchemasDirectory { get; } =
        Path.Combine(RepositoryRoot, "docs", "contracts", "schemas");

    public static string FixturePath(string name) => Path.Combine(FixturesDirectory, name);

    public static string SchemaPath(string name) => Path.Combine(SchemasDirectory, name);

    public static string ReadFixture(string name) => File.ReadAllText(FixturePath(name));

    public static JsonDocument ParseFixture(string name) =>
        JsonDocument.Parse(ReadFixture(name));

    /// <summary>Список сопоставлений «фикстура → схема» из manifest.json.</summary>
    public static IReadOnlyList<FixtureSchemaLink> LoadManifest()
    {
        var manifest = JsonDocument.Parse(File.ReadAllText(FixturePath("manifest.json")));
        var result = new List<FixtureSchemaLink>();

        foreach (var item in manifest.RootElement.GetProperty("fixtures").EnumerateArray())
        {
            result.Add(new FixtureSchemaLink(
                item.GetProperty("file").GetString() ?? "",
                item.GetProperty("schema").GetString() ?? ""));
        }

        return result;
    }

    private static string FindRepositoryRoot()
    {
        var directory = new DirectoryInfo(AppContext.BaseDirectory);

        while (directory is not null)
        {
            if (File.Exists(Path.Combine(directory.FullName, "WinSheduler.sln")))
            {
                return directory.FullName;
            }

            directory = directory.Parent;
        }

        throw new InvalidOperationException(
            "Не найден корень репозитория (./WinSheduler.sln) от " + AppContext.BaseDirectory);
    }
}

/// <summary>Связка фикстуры и схемы из manifest.json.</summary>
public sealed record FixtureSchemaLink(string FixtureFile, string SchemaFile);
