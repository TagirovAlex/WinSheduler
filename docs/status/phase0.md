# Статус трека Phase 0

## Текущая задача

P0-6 (каркас), P0-7 (локальный gate), P0-8 (скелет контрактных тестов), P0-9 (проверка).
Статус: **готово**.

## Выполнено

### P0-6. Каркас

- `./Directory.Build.props` — версия 1.2.0 / 1.2.0.0, Company/Product, `Deterministic=true`,
  `Nullable=enable`, `ImplicitUsings=enable`, `LangVersion=latest`, `WarningLevel=5`.
  `TreatWarningsAsErrors` намеренно не включён.
- `./.editorconfig` — UTF-8, завершающий перевод строки, отступ 4 для C#/C++/XAML,
  2 для JSON/YAML/MD и файлов проектов, CRLF для C#/C++/XAML, LF для MD/JSON.
  Правила именования не добавлялись. Для `*.ps1` задан `utf-8-bom` (требование PowerShell 5.1).
- `./src/WinSheduler.UI/app.manifest` — `asInvoker`, `dpiAwareness=PerMonitorV2`,
  `longPathAware=true`, `supportedOS` Win10, `activeCodePage=UTF-8`.
  Подключён через `<ApplicationManifest>app.manifest</ApplicationManifest>`.
  Проверено: строки манифеста присутствуют в собранном `WinSheduler.UI.exe`.
- `./src/WinShedulerSvc/WinShedulerSvc.vcxproj` — явные списки заменены на
  `<ClCompile Include="*.cpp" />`, `<ClCompile Include="*.c" />`, `<ClInclude Include="*.h" />`.
  `sqlite3.c` входит только через `*.c` (в `Cl.items.tlog` ровно одна запись).
- Общие модели C# (`./src/WinSheduler.Shared`): добавлены `SessionMode`, `TriggerKind`,
  `TriggeredBy`, `RunStatus.Aborted`; поля `SessionMode`, `OwnerSid`, `HasPassword`,
  `Password` (только запись), `Kind`, `DayInterval`, `WeekInterval`, `MonthInterval`,
  `TriggeredBy`; в `ServiceStatus` — `IsAdmin`, `IsOperator`, `CanManageSystemTasks`,
  `ServiceVersion`, `DbReady`. Все новые поля имеют значения по умолчанию.
- `./src/WinShedulerSvc/config.h` — зеркальные энумы и поля, `RunStatus::Aborted=5`,
  ветка `Aborted` в `ToWide(RunStatus)`.
- `./tests/WinSheduler.Tests` — xUnit-проект (`net8.0`, `IsTestProject`), ссылка на
  `WinSheduler.Shared`, тест `ОбщиеМодели_НовыеПоляИмеютЗначенияПоУмолчанию`.
  Проект добавлен в `./WinSheduler.sln` (папка решения `tests`).
- Версия сборки проверена: `WinSheduler.UI.dll` и `WinSheduler.Shared.dll` — `1.2.0.0`.

### P0-7. Локальный gate

- `./scripts/build.ps1` — поиск MSBuild: `vswhere` в `ProgramFiles(x86)` → `ProgramFiles`
  → явный путь VS 2022 Community → `msbuild` из PATH; `dotnet restore` для C# проектов;
  сборка `./WinSheduler.sln` с `/p:Configuration=Release /p:Platform=x64`;
  lock-файл `./temp/locks/build.lock` через `[System.IO.File]::Open` с `FileShare.None`,
  ожидание до 10 минут.
- `./scripts/test.ps1` — `dotnet test ... -c Release --no-build`; для консольного
  тестового проекта предусмотрен запуск exe.
- `./scripts/format.ps1` — `dotnet format whitespace --verify-no-changes` для Shared/UI/Tests;
  `clang-format --dry-run --Werror` для `./src/WinShedulerSvc/*.cpp|*.h` при наличии утилиты.
- `./scripts/verify.ps1 -Scope <track>` — build → test → format → проверка JSON-схем →
  smoke-тест standalone-службы; ненулевой код при любой ошибке.
- `./scripts/bench.ps1` — заглушка «Бенчмарки будут добавлены в Track G».
- `./scripts/README.md` — описание команд по-русски.

### P0-8. Скелет контрактных тестов

- `./tests/contract/README.md` — назначение будущих схем и round-trip тестов IPC.
- `./docs/contracts/schemas/README.md` — каталог для JSON-схем, соглашения и статус.

## Принятые решения

1. **xUnit, а не консольный тест.** Пакеты `xunit 2.9.3`, `xunit.runner.visualstudio 2.8.2`,
   `Microsoft.NET.Test.Sdk 17.12.0` уже были в локальном кеше NuGet, restore прошёл.
2. **Скрипты сохранены в UTF-8 с BOM.** Windows PowerShell 5.1 без BOM читает файл как ANSI
   и ломает кириллицу. Поэтому в `.editorconfig` для `*.ps1` указан `utf-8-bom`.
3. **Проверка форматирования C# через `dotnet format whitespace`.** Полный `dotnet format`
   включает style/analyzer-правила, которые дают шум на существующем коде. Whitespace-режим
   проверяет отступы, хвостовые пробелы и завершающий перевод строки — это и есть цель gate.
4. **Легаси `end_of_line`/`charset` — предупреждение, не ошибка.** `.editorconfig` требует CRLF
   для C#/C++/XAML (по заданию), но репозиторий исторически хранит LF, а массовая нормализация
   затронула бы файлы вне зоны Phase 0 (в том числе запрещённые). `format.ps1` выводит такие
   замечания предупреждением, остальные нарушения считает ошибкой. Нормализация — отдельная задача.
5. **`AssemblyInfo.cs` (UI) приведён к формату.** Единственное реальное нарушение whitespace
   было в этом файле (пробел после `[assembly:`). Файл не входит в зону треков D/E, поэтому
   правка допустима и минимальна.
6. **Smoke-тест службы** проверяет, что процесс жив через 2 секунды и корректно останавливается.
   Код выхода после принудительной остановки не информативен, поэтому ошибкой считается
   только самостоятельное завершение процесса в окне ожидания.

## Расхождения с контрактами

1. **`TimeWindowDto.DayInterval/WeekInterval/MonthInterval`.** В `./docs/contracts/IPC.md`
   указан тип `int?`, в задаче P0-6 — `int` со значением по умолчанию 1. Реализовано как
   `int = 1`: это совпадает с `./docs/contracts/DB.md` (`NOT NULL DEFAULT 1`) и не меняет
   JSON для существующих клиентов. Решение Architect: оставить `int` со значением 1;
   контракт `./docs/contracts/IPC.md` обновлён.
2. Остальные новые поля контрактам не противоречат.

## В работе

Ничего.

## Осталось

- Установить `clang-format` (LLVM или компонент VS), чтобы gate проверял C++.
- Добавить JSON-схемы в `./docs/contracts/schemas/` (Track G, G-4) — сейчас проверка выдаёт предупреждение.
- Нормализовать переводы строк существующих C#/C++/XAML файлов под `.editorconfig` (отдельная задача, вне Phase 0).
- Реализовать `./scripts/bench.ps1` (Track G).
- ~~Решить Architect расхождение по `int?`/`int` в `TimeWindowDto`.~~ Решено: `int` со значением 1.

## Проверки

- `./scripts/build.ps1` — успешно (MSBuild 17.14, `Release|x64`).
- `./scripts/test.ps1` — успешно, 1 тест пройден.
- `./scripts/format.ps1` — успешно (предупреждения: clang-format отсутствует, легаси `end_of_line`/`charset`).
- `./scripts/verify.ps1 -Scope phase0` — успешно, все шаги `OK` (build, test, format, schemas, smoke).

## Блокеры

Нет. Требования к окружению выполнены: VS 2022 Community с MSBuild 17.14, .NET 8 SDK,
runtime net8.0/WindowsDesktop. WiX, `clang-format`, `signtool` отсутствуют — для Phase 0 не нужны.

## Запросы к другим трекам

- Architect: уточнить тип `DayInterval`/`WeekInterval`/`MonthInterval` в `TimeWindowDto`
  (`int` против `int?` в `./docs/contracts/IPC.md`).
- Track G: добавить JSON-схемы контрактов (G-4) и наполнить `./tests/contract/`.
