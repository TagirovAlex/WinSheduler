# Запросы трека D

## Integrator

1. **Добавить UI-тесты в решение и gate.**
   Новый проект: `./tests/ui/WinSheduler.UI.Tests.csproj` (xUnit, `net8.0-windows`, `UseWPF`).
   Сейчас он не входит в `./WinSheduler.sln`, поэтому `./scripts/build.ps1` и
   `./scripts/test.ps1` его не видят. Нужно:
   - добавить проект в решение (папка `tests`);
   - добавить `dotnet restore` проекта в `./scripts/build.ps1`;
   - запускать `dotnet test` для него в `./scripts/test.ps1` (второй проект).
   Обоснование: без этого 37 тестов фундамента UI не проверяются локальным gate.

## Track B (IPC)

1. **Подтвердить форму payload `SetSettings`.** Клиент отправляет словарь
   `{ "ключ": "значение", ... }` прямо в `payload`. Если контракт предполагает
   обёртку `{ "settings": {...} }`, сообщите — поправлю `IpcService`.
2. **Подтвердить форму ответа `GetSettings`.** Клиент ожидает объект-словарь
   `{ "ключ": "значение" }`.
3. **Совместимость конверта.** `IpcClient` v2 отправляет
   `{"v":1,"id","action","payload"}` (camelCase), а читает и `ok/payload`, и
   legacy `Success/Payload`. После реализации B-3 прошу подтвердить, что сервер
   отвечает `ok`/`error.code`; legacy-ветка чтения будет удалена.
4. **Формат дат в `TimeWindowDto`.** Клиент пишет `startDate`/`endDate` и
   `specificDates` как `yyyy-MM-dd`, полное время — как ISO 8601 (`...Z` для UTC).
   `repeatUntil` передаётся строкой `yyyy-MM-ddTHH:mm` без изменений.
5. **Срок жизни legacy-слоя.** `IpcClient.Send/SendAsync(IpcRequest)` оставлен
   только для текущих экранов Track E (старый конверт, режим сообщений).
   После перевода экранов на `IIpcService` слой удаляется. Предлагаю зафиксировать
   это в B-3/I-3.

## Track E (экраны)

1. **IPC:** используйте `WinSheduler.UI.Services.IIpcService` из `App.Services.Ipc`,
   а не `IpcClient.SendAsync`. Старые методы — временный слой совместимости.
2. **Главный список:** `App.Services.CreateDashboardViewModel()` возвращает
   `DashboardViewModel` с `TasksView`, `RefreshCommand`, `CancelRefreshCommand`,
   `RefreshStatusCommand`, `SearchText`, `Filter`, `SelectedTask`, `ErrorText`,
   `IsServiceUnavailable`. Обновление — один вызов `GetDashboard`, без 2N+1.
3. **Пароль (важно, безопасность):** текущий `TaskEditorWindow` вызывает
   `CryptoHelper.Encrypt` и передаёт `RunAsPasswordEncrypted`. Это нарушает
   `./docs/contracts/SECURITY.md`, раздел 4: UI не шифрует пароль и не получает
   blob. Нужно передавать `TaskDto.Password` (только запись) и показывать
   `HasPassword`. `RunAsPasswordEncrypted` в запросах не заполнять.
4. **Конструктор `MainWindow`:** `App.OnStartup` создаёт `new MainWindow()`.
   Если понадобится конструктор с параметрами — сообщите, добавлю фабрику в хост.
5. **Строки:** ключи экранов добавляйте в
   `./src/WinSheduler.UI/Localization/Strings.resx` (файл фундамента).
   Существующие ключи не переименовывайте; в XAML — `{loc:Loc Ключ}`.
6. **Тема:** переключение — `App.Services.SetTheme(ThemeMode.Light/Dark/System)`,
   выбор сохраняется в состоянии окна.
7. **Привилегированные действия:** видимость привязывайте к
   `DashboardViewModel.IsAdmin` / `CanManageSystemTasks` (или
   `App.Services.Elevation`), кнопка повышения — `ElevationService.RelaunchAsAdmin()`.

## Architect

1. **Размещение DTO дашборда.** `DashboardDto`, `DashboardTaskDto`,
   `DashboardLastRunDto`, `AuditEntryDto`, `PingResult` объявлены в
   `./src/WinSheduler.UI/Services/IpcModels.cs` (зона Track D), потому что
   `./src/WinSheduler.Shared/**` — зона Architect. Если контрактные тесты (G-4)
   должны использовать эти модели, предлагаю перенести их в Shared.
