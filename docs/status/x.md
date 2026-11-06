# Статус Track X — экспорт и импорт задач

Дата: 2026-11-06.

## Сделано

- X-1: контракт `./docs/contracts/EXCHANGE.md` — формат `winsheduler.tasks` v1,
  шапка, поля задачи и триггеров, исключённые секреты и служебные поля,
  лимиты, режимы `copy` / `skip` / `overwrite`, правило `hasPassword`,
  очистка id триггеров, права и владелец, отчёт и аудит.
- X-2: модели обмена в `./src/WinSheduler.Shared/Models/`:
  `TaskExchangeDocument`, `TaskExchangeTask`, `TaskExchangeOptions`,
  `TaskImportMode`, `TaskImportOutcome`, `TaskImportItemResult`,
  `TaskImportReport`, `TaskExchangeJson`.
- `TaskExchangeTask` не наследует `TaskDto`, поэтому пароли не попадают
  в сериализацию; `FromTaskDto` / `ToTaskDto` выполняют явный маппинг,
  а `ToTaskDto` очищает id триггеров.
- `TaskExchangeJson` задаёт camelCase, строковые перечисления, отступы,
  игнорирование `null` и константы формата/лимитов.
- Дата только в формате `yyyy-MM-dd` (через конвертер), `exportedAt` — UTC с `Z`.
- X-3: `./src/WinSheduler.UI/Services/TaskExchangeService.cs` — экспорт
  выбранных задач через `GetTask`; документ собирается в памяти, файл пишется
  UTF-8 без BOM только после успешного чтения всех задач.
- X-4: `TaskExchangeService.ImportAsync` — проверка шапки, лимитов и строк,
  режимы `Copy` / `Skip` / `Overwrite`, частичный успех, отчёт со счётчиками
  и предупреждениями, очистка id триггеров (через `ToTaskDto`), принудительное
  выключение задачи с `hasPassword` и предупреждение, остановка по `UNAVAILABLE`.
- X-4: `./src/WinSheduler.UI/ImportPreviewWindow.xaml` и `.xaml.cs` —
  предпросмотр задач и предупреждений, выбор режима, прогресс и отмена;
  секреты не отображаются, тексты — в `Strings.resx` (`Exchange_*`).

## Проверка

- `dotnet build ./src/WinSheduler.Shared/WinSheduler.Shared.csproj -c Release --nologo` —
  успешно, 0 предупреждений, 0 ошибок.
- Ручная проверка сериализации в `./temp/x`: camelCase, строковые перечисления,
  отсутствие `password` / `encryptedPassword` / `runAsPasswordEncrypted` /
  `owner` / `createdAt` / `modifiedAt`, отсутствие `null`-полей,
  `specificDates` как `yyyy-MM-dd`, round-trip и очистка id триггеров — успешно.
- `dotnet build ./src/WinSheduler.UI/WinSheduler.UI.csproj -c Release --nologo` —
  успешно, 0 предупреждений, 0 ошибок.
- `dotnet test ./tests/ui/WinSheduler.UI.Tests.csproj -c Release --nologo` —
  успешно, 60 тестов (включая `TaskExchangeServiceTests`).
- `TaskExchangeServiceTests` проверяют: отсутствие секретов в файле,
  round-trip полей и триггеров, режимы `Copy` / `Skip` / `Overwrite`,
  чужую задачу, очистку id триггеров, `hasPassword`, версию формата 2,
  битый JSON, лимиты размера/задач/триггеров/строк и остановку по `UNAVAILABLE`.

- X-5: в `MainWindow` включён `SelectionMode=Extended`, добавлены кнопки
  «Экспорт»/«Импорт», пункты контекстного меню, `SaveFileDialog`/`OpenFileDialog`
  и обновление доступности команд.
- X-6: секреты не экспортируются; `hasPassword` без пароля выключает задачу;
  перезапись чужих задач не-админом невозможна; ID триггеров очищаются.
- X-7: 14 новых тестов `TaskExchangeServiceTests`, всего UI-тестов — 60.
- X-8: обновлены `UI.md`, `SECURITY.md`, README и `EXCHANGE.md`.
- ADR 0005 (`./docs/adr/0005-task-exchange.md`) зафиксировал решения.

## Проверка (дополнительно)

- `./scripts/verify.ps1 -Scope integration` — зелёный (build, test, format,
  contract-cpp, contract-csharp, schemas, smoke).
- UI.Tests — 60/60.

## Осталось

- Финальный MSI/`publish.ps1` и smoke на Server 2016 — в рамках H-8/I-8.

## Блокеры

- Нет. Для полного E2E нужна ВМ Server 2016.
