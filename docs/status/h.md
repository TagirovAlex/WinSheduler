# Статус Track H — платформа 2.1

Дата: 2026-11-06.

## Сделано

- ADR 0004: .NET 10 LTS и минимальная ОС Server 2016 (build 14393).
- `./global.json`: SDK `10.0.403`, `rollForward: latestFeature`.
- TFM: UI и UI.Tests — `net10.0-windows`; Shared, WinSheduler.Tests,
  ContractTests — `net10.0`.
- `Directory.Build.props`: версия 2.1.0.
- `scripts/build.ps1`: managed — `dotnet build`, native — MSBuild из VS 2022;
  lock сборки сохранён.
- `scripts/test.ps1` и `tests/e2e/ui-smoke.ps1`: убран хардкод `net8.0`.
- `installer/ca/ca.cpp`: порог ОС `14393`, сообщение «Windows 10 1607 /
  Server 2016».
- `_WIN32_WINNT=0x0601`, `WINVER=0x0601` в оба vcxproj;
  `SQLITE_WIN32_GETVERSIONEX=0` для сборки SQLite без deprecated `GetVersionEx`.
- Локальные `#define WINVER` в `process_runner.cpp`/`scheduler.cpp` удалены.
- UI: устранён конфликт `ThemeMode` с новым `System.Windows.ThemeMode`
  в .NET 10 (`App.xaml.cs`, UI-тесты).
- Иконки `export`/`import`: SVG, PNG 16/24/32/48, ICO, ресурсы UI,
  `Icons.xaml`, `ICON_MAP.md`.

## Проверка

- `dotnet build` Shared, UI, WinSheduler.Tests, ContractTests — успешно,
  0 предупреждений.
- `./scripts/build.ps1 -Configuration Release` — успешно (managed + native).
- `./scripts/test.ps1` — 1/1.
- ContractTests — 46/46.
- UI.Tests — 60/60 (37 прежних + 9 `RunFollowTests` + 14 `TaskExchangeServiceTests`).
- MSBuild службы и CA — успешно.
- `dotnet publish` UI self-contained `win-x64` в `./temp/publish/ui` — успешно,
  версия 2.1.0; 402 файла, 140,1 МБ до сжатия MSI (бюджет MSI — 80 МБ).
- `./scripts/installer/build.ps1 -Version 2.1.0 -OutputDir artifacts -RequireWix
  -EnforceBudgets` — MSI 49,8 МБ, SHA256-манифест создан, бюджет не превышен.
- `./publish.ps1` — финальный комплект в `./published`: MSI 2.1.0 (49,8 МБ,
  SHA256 `57CDFCB4…062A`), служба, self-contained UI, лицензии, README,
  `SHA256SUMS.txt`. Старая БД при наличии сохраняется в `./temp/backup/`.

- `./scripts/verify.ps1 -Scope integration` — зелёный (build, test, format,
  contract-cpp, contract-csharp, schemas, smoke).
- UI smoke — окно и кнопка «Добавить» найдены.

## Осталось

- H-8: smoke на Server 2016 Desktop Experience и Server Core (MSI уже в
  `./published`, 49,8 МБ).
- H-7 закрыт: README/SPECIFICATION/INSTALL/OPEN_QUESTIONS/RISKS сверены.

## Блокеры

- Нет. Для H-8 нужна реальная ВМ Server 2016 (Desktop Experience и Core).
