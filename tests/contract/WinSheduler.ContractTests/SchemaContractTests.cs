using System.Text.Json;
using System.Text.Json.Nodes;
using Xunit;
using Xunit.Abstractions;

namespace WinSheduler.ContractTests;

/// <summary>
/// Проверка фикстур по JSON-схемам из ./docs/contracts/schemas.
/// Схемы — зона Architect; пока файла нет, тест фиксирует пропуск в выводе,
/// а запрос на схему лежит в ./docs/status/g-requests.md.
/// </summary>
public class SchemaContractTests
{
    private readonly ITestOutputHelper _output;

    public SchemaContractTests(ITestOutputHelper output)
    {
        _output = output;
    }

    public static TheoryData<string, string> ManifestLinks()
    {
        var data = new TheoryData<string, string>();

        foreach (var link in ContractFixtures.LoadManifest())
        {
            data.Add(link.FixtureFile, link.SchemaFile);
        }

        return data;
    }

    [Fact]
    public void Manifest_СсылаетсяНаСуществующиеФикстуры()
    {
        var links = ContractFixtures.LoadManifest();

        Assert.NotEmpty(links);
        Assert.Equal(links.Count, links.Select(link => link.FixtureFile).Distinct().Count());

        foreach (var link in links)
        {
            Assert.True(
                File.Exists(ContractFixtures.FixturePath(link.FixtureFile)),
                $"нет фикстуры {link.FixtureFile}");
        }
    }

    [Fact]
    public void Схемы_ВалидныКакJsonИОбъявляютDraft202012()
    {
        if (!Directory.Exists(ContractFixtures.SchemasDirectory))
        {
            _output.WriteLine("Каталог схем отсутствует — проверка пропущена.");
            return;
        }

        var schemas = Directory.GetFiles(ContractFixtures.SchemasDirectory, "*.schema.json");

        foreach (var file in schemas)
        {
            var node = JsonNode.Parse(File.ReadAllText(file));
            Assert.NotNull(node);
            Assert.IsType<JsonObject>(node);

            var schemaVersion = node!["$schema"]?.GetValue<string>() ?? "";
            Assert.Equal("https://json-schema.org/draft/2020-12/schema", schemaVersion);
        }
    }

    [Theory]
    [MemberData(nameof(ManifestLinks))]
    public void Фикстура_СоответствуетСхеме(string fixtureFile, string schemaFile)
    {
        var schemaPath = ContractFixtures.SchemaPath(schemaFile);
        if (!File.Exists(schemaPath))
        {
            _output.WriteLine(
                $"Схема {schemaFile} отсутствует: проверка {fixtureFile} пропущена. " +
                "Запрос — ./docs/status/g-requests.md.");
            return;
        }

        var schema = JsonNode.Parse(File.ReadAllText(schemaPath));
        Assert.NotNull(schema);

        var fixture = JsonNode.Parse(ContractFixtures.ReadFixture(fixtureFile));
        var errors = JsonSchemaValidator.Validate(fixture, schema!);

        Assert.True(
            errors.Count == 0,
            $"{fixtureFile} не соответствует {schemaFile}:{Environment.NewLine}" +
            string.Join(Environment.NewLine, errors));
    }

    [Fact]
    public void Валидатор_ЛовитНарушенияТипаИОбязательныхПолей()
    {
        // Самотест валидатора: без него «зелёная» схема ничего не доказывает.
        var schema = JsonNode.Parse("""
            {
              "$schema": "https://json-schema.org/draft/2020-12/schema",
              "type": "object",
              "required": ["name"],
              "additionalProperties": false,
              "properties": {
                "name": { "type": "string", "minLength": 1 },
                "count": { "type": "integer", "minimum": 1, "maximum": 10 },
                "kind": { "enum": ["Daily", "Weekly"] }
              }
            }
            """);

        Assert.NotNull(schema);

        var valid = JsonNode.Parse("""{"name":"x","count":5,"kind":"Daily"}""");
        Assert.Empty(JsonSchemaValidator.Validate(valid, schema!));

        var missingName = JsonNode.Parse("""{"count":5}""");
        Assert.NotEmpty(JsonSchemaValidator.Validate(missingName, schema!));

        var wrongType = JsonNode.Parse("""{"name":1}""");
        Assert.NotEmpty(JsonSchemaValidator.Validate(wrongType, schema!));

        var extraField = JsonNode.Parse("""{"name":"x","extra":true}""");
        Assert.NotEmpty(JsonSchemaValidator.Validate(extraField, schema!));

        var outOfRange = JsonNode.Parse("""{"name":"x","count":11}""");
        Assert.NotEmpty(JsonSchemaValidator.Validate(outOfRange, schema!));
    }
}
