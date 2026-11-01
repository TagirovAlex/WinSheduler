# Контрактные тесты

Общие тесты формата IPC и моделей. Стратегия и матрица — в `./tests/README.md`.

## Состав

| Путь | Что проверяет |
|------|---------------|
| `./fixtures/*.json` | общие фикстуры конверта IPC, DTO и legacy-режима |
| `./cpp` | C++ харнесс: разбор/сериализация JSON, инварианты конверта, fuzz |
| `./WinSheduler.ContractTests` | C# xUnit: конверт, DTO, кадрирование, JSON-схемы |

Фикстуры — единственный источник данных для обеих сторон: если C++ и C#
разойдутся в трактовке контракта, тест упадёт с одной стороны.

## Запуск

```powershell
./scripts/verify.ps1 -Scope g
```

Отдельно:

```powershell
dotnet test ./tests/contract/WinSheduler.ContractTests/WinSheduler.ContractTests.csproj -c Release
./tests/contract/cpp/bin/Release/WinSheduler.ContractTests.exe --fixtures=./tests/contract/fixtures
```

C++ проект собирается отдельно (не входит в `./WinSheduler.sln`, чтобы не менять
файл решения):

```powershell
msbuild ./tests/contract/cpp/WinSheduler.ContractTests.vcxproj /p:Configuration=Release /p:Platform=x64
```

## JSON-схемы

- Схемы лежат в `./docs/contracts/schemas/` и принадлежат Architect.
- Сопоставление «фикстура → схема» — в `./fixtures/manifest.json`.
- Пока схема отсутствует, соответствующая проверка пропускается с сообщением.
  Запросы на схемы — в `./docs/status/g-requests.md`.
- Валидатор — минимальное подмножество draft 2020-12 без внешних зависимостей
  (`./WinSheduler.ContractTests/JsonSchemaValidator.cs`).

## Правила

- Источник истины — `./docs/contracts/IPC.md` и `./docs/contracts/DB.md`.
- Схемы и тесты меняются только вместе с контрактом и ADR.
- Фикстуры не содержат паролей, SID и реальных путей пользователей.
- Тесты детерминированы: fuzz использует фиксированное зерно, сеть не нужна.
- Незакрытый дефект реализации фиксируется через `PENDING(...)` с ID задачи,
  а не «зелёным» тестом: см. `./docs/status/g-requests.md`.
