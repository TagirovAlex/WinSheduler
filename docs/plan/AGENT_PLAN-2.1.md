# План работы агентов для релиза 2.1

Основа: `./docs/plan/ROADMAP-2.1.md`, доска `./docs/plan/TASKS.md`.
Правила: `./docs/plan/AGENTS.md`. Работаем короткими диффами, комментарии и
документация — по-русски, `./published/**` не трогаем.

## 1. Принципы

- Один агент — один рабочий поток с явным списком файлов.
- Общие файлы (`MainWindow.*`, `Strings.resx`, `WinSheduler.UI.csproj`)
  одновременно правит только один агент.
- Сборки не запускаются параллельно: общий `bin/obj` и lock сборки.
  Агент собирает только свои проекты; финальный gate — у Integrator.
- Коммиты в общем дереве запрещены. Для параллельной работы — отдельный
  worktree на поток; слияние и gate выполняет Integrator.

## 2. Потоки

| Поток | Задачи | Зона | Зависит |
|-------|--------|------|---------|
| H-A | H-2, H-3 | `./global.json`, `*.csproj`, `./scripts/build.ps1`, `./scripts/test.ps1`, `./tests/e2e/ui-smoke.ps1`, `./Directory.Build.props` | ADR 0004 |
| H-B | H-4 | `./installer/ca/ca.cpp`, `./installer/ca/WinShedulerCA.vcxproj`, `./src/WinShedulerSvc/WinShedulerSvc.vcxproj`, `./process_runner.cpp`, `./scheduler.cpp` | ADR 0004 |
| X-A | X-1, X-2 | `./docs/contracts/EXCHANGE.md`, `./src/WinSheduler.Shared/Models/TaskExchange*.cs` | — |
| X-B | X-3…X-5, X-8 | `./src/WinSheduler.UI/Services/TaskExchange*`, `./src/WinSheduler.UI/ViewModels/Exchange*`, `./src/WinSheduler.UI/ExchangeWindow*`, `MainWindow.*`, `Strings.resx` | H-A, X-A |
| Y-A | Y-1…Y-3 | `DashboardViewModel.cs`, `TaskListItemViewModel.cs`, `./tests/ui/**` | H-A |
| Y-B | Y-4, Y-5, Y-7 | `MainWindow.*`, `Strings.resx`, `./docs/contracts/UI.md` | Y-A, X-B (последовательно) |
| QA | H-8, X-6, X-7, Y-6 | `./tests/**`, `./docs/status/**` | H-A…Y-B |
| Icons | — | `./ico/**`, `Resources/Icons*`, `ICON_MAP.md` | готово |

## 3. Волны

### Волна 1 — параллельно

1. **H-A**: .NET 10, TFM, `global.json`, build-скрипты.
2. **H-B**: порог ОС 14393, `_WIN32_WINNT`, макросы.
3. **X-A**: контракт обмена и C#-модели (без UI).

Файлы не пересекаются. Сборки: H-A — только managed; H-B — только native.

### Волна 2 — после H-A

1. **Y-A**: тихое обновление и цикл сопровождения в VM (без `MainWindow`).
2. **X-B**: сервис экспорта/импорта и диалоги (без `MainWindow` до готовности Y-A).

`MainWindow.xaml`/`.cs` и `Strings.resx` на этом этапе не трогаются, чтобы
не конфликтовать.

### Волна 3 — интеграция

1. Integrator сводит X-B и Y-A.
2. **Y-B**: добавляет кнопки/колонки/баннер в `MainWindow`, строки в `Strings.resx`.
3. **QA**: тесты, gate, MSI, smoke.

## 4. Промпты агентов (сокращённо)

Каждый промпт содержит:

- ссылку на `ROADMAP-2.1.md`, `TASKS.md`, `AGENTS.md`;
- точный список файлов, которые можно менять;
- запрет на `git commit/checkout/reset`, `./published/**`, сетевые скачивания;
- требование: сначала прочитать файлы, потом минимальный дифф;
- проверку: профильная сборка/тесты, без параллельных сборок;
- отчёт по шаблону из `AGENTS.md` §6.

### H-A

> Переведи UI/Shared/тесты на .NET 10. Добавь `./global.json` (10.0.403,
> latestFeature), поменяй TFM, версию 2.1.0, обнови build/test/e2e-скрипты:
> managed — `dotnet build`, native — MSBuild. Не трогай `installer/**` и
> `src/WinShedulerSvc/**`. Проверь `dotnet build` трёх managed-проектов.

### H-B

> Снизь минимум ОС до build 14393 в `installer/ca/ca.cpp` (3 сравнения и
> сообщение), добавь `_WIN32_WINNT=0x0601;WINVER=0x0601` в оба vcxproj,
> убери локальные `#define` в `process_runner.cpp`/`scheduler.cpp`.
> Проверь сборку службы и CA через MSBuild. Не трогай C# и docs.

### X-A

> Оформи `EXCHANGE.md` (JSON v1, лимиты, секреты, режимы импорта) и
> C#-модели обмена в `WinSheduler.Shared`. Без UI и без IPC-действий.
> Сериализация camelCase, `formatVersion`, отсутствие секретов.

### X-B

> Реализуй экспорт/импорт в UI по `EXCHANGE.md`: `TaskExchangeService`,
> диалоги, режимы copy/skip/overwrite, отчёт, отмена. Используй только
> существующие IPC-методы. Пароли не экспортируй, ID триггеров очищай.
> `MainWindow` пока не трогай.

### Y-A

> Добавь в `DashboardViewModel` тихое обновление и цикл сопровождения
> запущенных задач через `GetHistory(taskId, 1)` (2→5 с, grace 5 с,
> лимит 24 ч, отмена). В `TaskListItemViewModel` — `LastRun.Status` и
> приоритет Running > ошибка > Enabled/Disabled. Без `MainWindow`.

### Y-B

> Подключи автосопровождение к `RunNow` и `running=true`, добавь колонку/
> индикатор результата и баннер завершения/ошибки, строки в `Strings.resx`.
> Не меняй логику VM, только представление и вызовы.

### QA

> Прогони `./scripts/verify.ps1 -Scope integration`, контрактные тесты,
> UI-тесты, `./publish.ps1`. Проверь: секретов в экспорте нет, повторный
> импорт не ломает PK, статус обновляется без F5. Отчёт — в
> `./docs/status/gate.md` и `./docs/status/f.md`.

## 5. Матрица владения общими файлами

| Файл | Владелец | Когда |
|------|----------|-------|
| `MainWindow.xaml` | Y-B | после X-B и Y-A |
| `MainWindow.xaml.cs` | Y-B | после X-B и Y-A |
| `Strings.resx` | Y-B | после X-B и Y-A |
| `WinSheduler.UI.csproj` | H-A | волна 1; позже — Integrator |
| `Directory.Build.props` | H-A | волна 1 |
| `docs/contracts/UI.md` | Y-B | волна 3 |
| `docs/contracts/EXCHANGE.md` | X-A | волна 1 |

## 6. Gate и приёмка

- После каждой волны: профильная сборка/тесты, затем `git diff --check`.
- Финальный gate: `./scripts/build.ps1`, `./scripts/test.ps1`,
  `./scripts/verify.ps1 -Scope integration`, `./publish.ps1`.
- Ручной smoke: Windows 10 1809+, Server 2016 Desktop Experience,
  Server 2016 Core (только служба).
- Обязательные проверки 2.1:
  - экспорт 1 и N задач, импорт copy/skip/overwrite;
  - в файле нет `password`/`encryptedPassword`;
  - повторный импорт копии не даёт конфликт PK;
  - после `RunNow` статус меняется без F5, ошибка видна в списке и баннере.

## 7. Текущий статус

- ADR 0004 (.NET 10, Server 2016) и ADR 0005 (обмен задачами) приняты.
- H-A/H-B: .NET 10, TFM, build-скрипты, порог ОС 14393, макросы — готово.
- X-A/X-B: контракт `EXCHANGE.md`, модели, сервис, предпросмотр — готово.
- Y-A/Y-B: цикл сопровождения, статусы, кнопки, баннер — готово.
- Иконки `export`/`import`: SVG, PNG 16/24/32/48, ICO, ресурсы UI,
  `Icons.xaml`, `ICON_MAP.md` — готово.
- `./scripts/verify.ps1 -Scope integration` — зелёный; UI.Tests — 60/60.
- `./publish.ps1` — финальный комплект в `./published`, MSI 2.1.0 — 49,8 МБ,
  SHA256 `57CDFCB4…062A`.
- Тег `v2.1.0` и GitHub Release опубликованы:
  https://github.com/TagirovAlex/WinSheduler/releases/tag/v2.1.0
- Осталось: smoke Server 2016 (Desktop Experience и Core).
