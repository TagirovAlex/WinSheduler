using System.Text.Json;
using System.Text.Json.Nodes;
using System.Text.RegularExpressions;

namespace WinSheduler.ContractTests;

/// <summary>
/// Минимальный валидатор JSON Schema (draft 2020-12) для контрактных тестов.
/// Поддерживается подмножество ключевых слов, которого достаточно схемам
/// ./docs/contracts/schemas: type, properties, required, additionalProperties,
/// items, enum, const, pattern, minimum, maximum, minLength, maxLength,
/// minItems, maxItems. Внешние зависимости не нужны.
/// </summary>
public static class JsonSchemaValidator
{
    public static IReadOnlyList<string> Validate(JsonNode? instance, JsonNode schema)
    {
        var errors = new List<string>();
        ValidateNode(instance, schema, "$", errors);
        return errors;
    }

    private static void ValidateNode(JsonNode? instance, JsonNode schema, string path, List<string> errors)
    {
        if (schema is not JsonObject rules)
        {
            errors.Add($"{path}: схема должна быть объектом");
            return;
        }

        if (rules.TryGetPropertyValue("type", out var typeNode))
        {
            var types = ReadTypes(typeNode);
            if (!types.Any(type => MatchesType(instance, type)))
            {
                errors.Add($"{path}: тип не соответствует ожидаемому ({string.Join("|", types)})");
                return;
            }
        }

        if (rules.TryGetPropertyValue("const", out var constNode) &&
            !JsonNode.DeepEquals(instance, constNode))
        {
            errors.Add($"{path}: значение не равно const");
        }

        if (rules.TryGetPropertyValue("enum", out var enumNode) && enumNode is JsonArray allowed)
        {
            if (!allowed.Any(item => JsonNode.DeepEquals(instance, item)))
            {
                errors.Add($"{path}: значение отсутствует в enum");
            }
        }

        if (instance is JsonObject obj)
        {
            ValidateObject(obj, rules, path, errors);
        }
        else if (instance is JsonArray array)
        {
            ValidateArray(array, rules, path, errors);
        }
        else if (instance is JsonValue value && value.TryGetValue<string>(out var text))
        {
            ValidateString(text, rules, path, errors);
        }
        else if (instance is JsonValue number && number.TryGetValue<double>(out var numeric))
        {
            ValidateNumber(numeric, rules, path, errors);
        }
    }

    private static void ValidateObject(JsonObject instance, JsonObject rules, string path, List<string> errors)
    {
        if (rules.TryGetPropertyValue("required", out var requiredNode) && requiredNode is JsonArray required)
        {
            foreach (var item in required)
            {
                var name = item?.GetValue<string>();
                if (name is not null && !instance.ContainsKey(name))
                {
                    errors.Add($"{path}: отсутствует обязательное поле {name}");
                }
            }
        }

        var properties = rules.TryGetPropertyValue("properties", out var propertiesNode) && propertiesNode is JsonObject map
            ? map
            : new JsonObject();

        foreach (var (name, value) in instance)
        {
            if (properties.TryGetPropertyValue(name, out var propertySchema) && propertySchema is not null)
            {
                ValidateNode(value, propertySchema, $"{path}.{name}", errors);
                continue;
            }

            if (rules.TryGetPropertyValue("additionalProperties", out var additional))
            {
                if (additional is JsonValue flag && flag.TryGetValue<bool>(out var allowed) && !allowed)
                {
                    errors.Add($"{path}: недопустимое дополнительное поле {name}");
                }
                else if (additional is JsonObject additionalSchema)
                {
                    ValidateNode(value, additionalSchema, $"{path}.{name}", errors);
                }
            }
        }
    }

    private static void ValidateArray(JsonArray instance, JsonObject rules, string path, List<string> errors)
    {
        if (rules.TryGetPropertyValue("minItems", out var minItems) && minItems is not null)
        {
            if (instance.Count < minItems.GetValue<int>())
            {
                errors.Add($"{path}: элементов меньше minItems");
            }
        }

        if (rules.TryGetPropertyValue("maxItems", out var maxItems) && maxItems is not null)
        {
            if (instance.Count > maxItems.GetValue<int>())
            {
                errors.Add($"{path}: элементов больше maxItems");
            }
        }

        if (rules.TryGetPropertyValue("items", out var itemsSchema) && itemsSchema is not null)
        {
            for (var index = 0; index < instance.Count; ++index)
            {
                ValidateNode(instance[index], itemsSchema, $"{path}[{index}]", errors);
            }
        }
    }

    private static void ValidateString(string text, JsonObject rules, string path, List<string> errors)
    {
        if (rules.TryGetPropertyValue("minLength", out var minLength) && minLength is not null &&
            text.Length < minLength.GetValue<int>())
        {
            errors.Add($"{path}: строка короче minLength");
        }

        if (rules.TryGetPropertyValue("maxLength", out var maxLength) && maxLength is not null &&
            text.Length > maxLength.GetValue<int>())
        {
            errors.Add($"{path}: строка длиннее maxLength");
        }

        if (rules.TryGetPropertyValue("pattern", out var patternNode) && patternNode is not null)
        {
            var pattern = patternNode.GetValue<string>();
            var regex = new Regex(pattern, RegexOptions.CultureInvariant, TimeSpan.FromSeconds(1));
            if (!regex.IsMatch(text))
            {
                errors.Add($"{path}: строка не соответствует pattern {pattern}");
            }
        }
    }

    private static void ValidateNumber(double value, JsonObject rules, string path, List<string> errors)
    {
        if (rules.TryGetPropertyValue("minimum", out var minimum) && minimum is not null &&
            value < minimum.GetValue<double>())
        {
            errors.Add($"{path}: значение меньше minimum");
        }

        if (rules.TryGetPropertyValue("maximum", out var maximum) && maximum is not null &&
            value > maximum.GetValue<double>())
        {
            errors.Add($"{path}: значение больше maximum");
        }
    }

    private static IReadOnlyList<string> ReadTypes(JsonNode? typeNode)
    {
        if (typeNode is JsonArray array)
        {
            return array
                .Select(item => item?.GetValue<string>() ?? "")
                .Where(item => item.Length > 0)
                .ToArray();
        }

        var single = typeNode?.GetValue<string>();
        return single is null ? Array.Empty<string>() : new[] { single };
    }

    private static bool MatchesType(JsonNode? instance, string type) => type switch
    {
        "object" => instance is JsonObject,
        "array" => instance is JsonArray,
        "string" => instance is JsonValue text && text.TryGetValue<string>(out _),
        "number" => instance is JsonValue number && number.TryGetValue<double>(out _),
        "integer" => instance is JsonValue integer &&
                     integer.TryGetValue<double>(out var value) &&
                     Math.Abs(value - Math.Truncate(value)) < double.Epsilon,
        "boolean" => instance is JsonValue boolean && boolean.TryGetValue<bool>(out _),
        "null" => instance is null,
        _ => false,
    };
}
