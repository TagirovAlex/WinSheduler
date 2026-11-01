# Запросы трека B

Запросы на правку чужих файлов и найденные дефекты вне зоны B.
Сам трек B эти файлы не меняет.

## Architect

### 1. Схема `IpcResponse`: payload может быть массивом

`./docs/contracts/schemas/IpcResponse.schema.json` требует
`payload: object|null`. По IPC.md раздел 4 и по клиенту
(`IpcClient.CallAsync<T>`) `ListTasks`, `GetTimeWindows`, `GetHistory`,
`GetAudit` возвращают массивы. Предложение: `["object","array","null"]`
и `items` без ограничений (конкретный тип проверяется схемами DTO).

### 2. DTO аудита в IPC.md

Клиент (`AuditEntryDto`) ожидает поля `timestamp`, `actorSid`, `action`,
`objectType`, `objectId`, `result`, `details`. В IPC.md форма `GetAudit`
не описана; сервер отдаёт именно эти имена (camelCase). Просьба
зафиксировать их в контракте.

### 3. `startedAt` и UTC

`GetStatus.startedAt` приходит от планировщика в локальном времени
с суффиксом `Z`. По DB.md события — UTC ISO 8601. Нужно решение:
исправляет трек A или это допустимое отклонение.

## Track A (планировщик)

1. **Расшифровка пароля несовместима с B-6.** `decrypt_password`
   в `scheduler.cpp` использует `CRYPTPROTECT_LOCAL_MACHINE`, а служба
   теперь шифрует `CRYPTPROTECT_SYSTEM` (`auth::protect_secret`).
   Нужно вызывать `auth::unprotect_secret` (`./src/WinShedulerSvc/auth_secret.h`)
   либо передавать `CRYPTPROTECT_SYSTEM`; старые blob-ы читаются с
   `legacy_local_machine=true` только для ротации.
2. **RunNow от имени клиента (A-8).** Для не-админа нужен primary-токен
   клиента: он доступен в обработчике через `auth::current_client_token()`
   (`./src/WinShedulerSvc/auth_token.h`) на время запроса. Токен закрывает
   IPC-сервер после ответа — использовать только в пределах вызова.
3. **`GetStatus.started_at`** — см. запрос Architect № 3.

## Track C (хранение)

1. API использован: `list_tasks_by_owner`, `replace_time_windows`,
   `append_audit`/`list_audit` (через `db_repository.h`), `task_count`.
   До слияния работают совместимые fallback-ветки.
2. **`owner_sid` при вставке.** `Database::insert_task` должен сохранять
   `owner_sid` (в B-5 он выставляется SID вызывающего). Иначе после создания
   задача станет невидимой автору-неадмину.
3. **`purge_expired_audit`** пока не вызывается из IPC: нужна точка вызова
   при старте службы (зона Integrator/A) или внутри `Database::open`.

## Track D / E (UI)

1. **Пароль.** `TaskEditorWindow` шифрует пароль на клиенте
   (`CryptoHelper.Encrypt` → `RunAsPasswordEncrypted`). По SECURITY.md
   раздел 4 UI не шифрует и не получает blob: нужно передавать `password`
   (write-only) и показывать `hasPassword`. Сервер уже принимает оба
   варианта, старый — переходно.
2. **GetSettings/SetSettings.** Клиент их пока не использует. Формат:
   `GetSettings` → объект `{"ключ": "значение"}`; `SetSettings` → тот же
   объект в payload, ответ `{}`. Только администратор.
3. **GetDashboard** уже отдаёт все поля `DashboardViewModel`; при добавлении
   полей — сначала в IPC.md.

## Track F (установщик)

1. **`GroupSid` в реестре** читается сервером:
   `HKLM/SOFTWARE/WinSheduler`, REG_SZ, строка SID. Установщик должен
   записывать её вместе с `InstallDir`/`DataDir`/`Version`.
2. **Неэлевированный администратор и ACL канала.** ACE `Administrators`
   не действует на filtered token (deny-only SID), поэтому админ без
   elevation подключается только если он входит в `WinSheduler Users`.
   Предложение: установщик добавляет устанавливающего пользователя в группу
   (F-5) либо в контракт добавляется отдельный ACE для интерактивного
   пользователя.
3. **Fallback без группы** реализован: канал только `SYSTEM` +
   `Administrators`, в лог пишется предупреждение, `isOperator=false`.

## Track G (QA и gate)

1. **`./scripts/verify.ps1`.** Добавить шаг `contract-cpp`:
   `./tests/ipc/run.ps1` (собирает `./tests/ipc/ipc_tests.vcxproj` и гоняет
   148 проверок). Проект намеренно не входит в `./WinSheduler.sln`.
2. **PENDING-тесты G.** Дефекты 1–5 из `g-requests.md` закрыты в ветке B:
   `escape`/`unescape`, лимит глубины 64, кадрирование v2 (byte-mode,
   4 байта LE, 1 МиБ), `stop()` без `detach` (join + `CancelSynchronousIo`).
3. **JSON-схемы.** В ветке B схемы ещё не было; серверные ответы v2 —
   camelCase, legacy — PascalCase. После слияния стоит прогнать схемную
   проверку на реальных ответах (см. запрос Architect № 1).
