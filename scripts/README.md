# Скрипты сборки и проверки

Все команды выполняются из корня проекта, без `cd`. Скрипты совместимы с PowerShell 5.1.

| Скрипт | Назначение |
|--------|------------|
| `./scripts/build.ps1` | Сборка `./WinSheduler.sln` (C++ служба, C# UI, тесты) в `Release|x64`. |
| `./scripts/test.ps1` | Запуск тестов `./tests/WinSheduler.Tests` без повторной сборки. |
| `./scripts/format.ps1` | Проверка форматирования C# (`dotnet format`) и C++ (`clang-format`). |
| `./scripts/verify.ps1 -Scope <track>` | Полный локальный gate: сборка, тесты, форматирование, контрактные тесты, схемы, smoke-тест службы. |
| `./scripts/bench.ps1` | Заглушка бенчмарков (будут в Track G). |
| `./publish.ps1` | Финальный комплект в `./published`: служба, self-contained UI, MSI, SHA256, лицензии. Старые версии заменяются, БД копируется в `./temp/backup/`. |
| `./scripts/installer/build.ps1` | Сборка службы, self-contained UI и MSI; `-OutputDir` — каталог готовых файлов, `-SkipMsi` — без MSI, `-RequireWix` — ошибка при отсутствии WiX. |
| `./tests/e2e/ui-smoke.ps1` | UI smoke через UI Automation: окно и кнопка «Добавить»; нужен интерактивный рабочий стол. |

## build.ps1

1. Ищет MSBuild: `vswhere` в `ProgramFiles(x86)`, затем в `ProgramFiles`, затем явный путь
   Visual Studio 2022 Community, затем `msbuild` из `PATH`.
2. Восстанавливает NuGet-пакеты C# проектов (`dotnet restore`).
3. Собирает решение через MSBuild с `/p:Configuration=Release /p:Platform=x64`.

Сборки сериализуются lock-файлом `./temp/locks/build.lock` (`[System.IO.File]::Open`
с `FileShare.None`). Ожидание блокировки — до 10 минут, затем ошибка.

Параметры: `-Configuration` (по умолчанию `Release`), `-Platform` (по умолчанию `x64`).

## test.ps1

Запускает `dotnet test ./tests/WinSheduler.Tests/WinSheduler.Tests.csproj -c Release --no-build`.
Если тестовый проект консольный (без xUnit), запускается его исполняемый файл.
При падении тестов возвращает ненулевой код.

## format.ps1

- C#: `dotnet format whitespace <project> --verify-no-changes` для `Shared`, `UI`, `Tests`.
- C++: `clang-format --dry-run --Werror` для `./src/WinShedulerSvc/*.cpp` и `*.h`
  (кроме сторонних `sqlite3.*`).

Если `clang-format` не установлен — выводится предупреждение, проверка C++ пропускается.

## verify.ps1

Шаги выполняются последовательно, при любой ошибке итоговый код — `1`:

1. `build.ps1`
2. `test.ps1`
3. `format.ps1`
4. Контрактные тесты C++ (`./tests/contract/cpp`).
5. Контрактные тесты C# (`./tests/contract/WinSheduler.ContractTests`).
6. Проверка JSON-схем в `./docs/contracts/schemas/`.
7. Smoke-тест `./src/WinShedulerSvc/bin/Release/WinShedulerSvc.exe --standalone`:
   запуск, ожидание 2 секунды, остановка. Если процесс завершился сам — ошибка.

Дополнительно: `-Analyze` — MSVC `/analyze` и `clang-tidy` (если есть);
`-CleanCheckout` — сборка снимка `git archive HEAD` в `./temp/verify/clean`.

Параметр `-Scope` зарезервирован и не влияет на набор шагов.

## Требования

- Visual Studio 2022 (MSVC v143, Windows 10 SDK) или Build Tools с MSBuild.
- .NET 8 SDK.
- WiX 5 для MSI: `dotnet tool install --global wix --version 5.0.2`,
  `wix extension add -g WixToolset.UI.wixext/5.0.2`.
- Опционально: `clang-format` для проверки C++.
