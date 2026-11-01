# Статус трека B (IPC и безопасность)

## Текущая задача

B-1…B-12. Статус: **готово** (с оговорками в разделе «Расхождения»).
Ветка: `./temp/worktrees/b`, коммит с кодом трека.

## Выполнено

### B-1. Named pipe

- `PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS`.
- Явный SDDL: `SYSTEM` и `Administrators` — `GA`, группа операторов — `GRGW`
  (без `FILE_CREATE_PIPE_INSTANCE`).
- SID группы берётся из `HKLM/SOFTWARE/WinSheduler`, значение `GroupSid`
  (REG_SZ, пишет установщик F-5); при отсутствии — `LookupAccountName`
  для `WinSheduler Users`; если группы нет — fallback `SYSTEM` +
  `Administrators`, запись в лог, служба не падает.
- SID кэшируется при старте (`auth::warm_up_group_cache`), реестр на каждое
  соединение не читается.
- Лимит инстансов — 64, таймаут чтения кадра — 30 с (медленный клиент
  отключается), конвейеризация не поддерживается.
- Реализация: `./src/WinShedulerSvc/auth_token.*`, `ipc.cpp`.

### B-2. Аутентификация и авторизация

- `ImpersonateNamedPipeClient` выполняется на первом кадре соединения:
  Windows требует, чтобы клиент уже записал данные в канал.
- SID клиента (`TokenUser`), членство в `Administrators`
  (`CheckTokenMembership`, filtered token неэлевированного админа даёт false)
  и в группе операторов.
- После сбора идентичности — `RevertToSelf`; primary-токен клиента
  дублируется и доступен планировщику через `auth::current_client_token()`
  (для A-8, `CreateProcessAsUser`).
- Политика — `auth::Policy` (`./src/WinShedulerSvc/auth_policy.*`):
  матрица прав IPC.md раздел 3 и SECURITY.md раздел 2.
- Гость (нет членства) получает только `Ping`; на остальное — `AUTH_DENIED`
  и запись в аудит.

### B-3. Фрейминг v2

- 4 байта длины little-endian + UTF-8 JSON, лимит кадра 1 МиБ,
  глубина JSON 64, строка 64 КиБ (`ipc_frame.*`, `json.*`).
- Соединение допускает последовательность «запрос → ответ» на одном канале
  (UI использует одно соединение).
- При превышении лимита — ответ `TOO_LARGE` и закрытие соединения.
- Переходный режим `{Action, Payload}` сохранён: ответ `{Success, Error,
  Payload}`, PascalCase, одно сообщение на соединение.
- v2: `{v,id,action,payload}` / `{v,id,ok,error,payload}`, camelCase.
  `id` проверяется как GUID, `payload` — объект, имя действия ≤ 64 символов.

### B-4. Харденинг

- Верхний `catch(...)` в клиентском потоке; исключения не пересекают границу
  потока. Внутри обработки — `catch (std::exception)` и `catch (...)`.
- Парсер JSON больше не рекурсирует без ограничения: при глубине > 64
  возвращается `TooDeep` (ответ `TOO_LARGE`), стек не переполняется.
- Разбор устойчив к мусору: нет исключений, ошибка — статус разбора.
- Fuzz: детерминированный прогон (6000 итераций) в контрактных тестах
  и цель libFuzzer `./tests/ipc/fuzz_target.cpp`.
- Исправлены дефекты из `./docs/status/g-requests.md`:
  `escape` не съедает байт после битого lead-байта; `unescape` и парсер
  собирают суррогатную пару в один codepoint UTF-8 (4 байта).

### B-5. Обработчики

Действия: `Ping`, `GetStatus`, `GetDashboard`, `ListTasks`, `GetTask`,
`GetTimeWindows`, `GetHistory`, `CreateTask`, `UpdateTask`, `DeleteTask`,
`SetTaskEnabled` (+ legacy `EnableTask`/`DisableTask`), `RunNow`,
`GetAudit`, `GetSettings`, `SetSettings`.

- Фильтрация по `owner_sid`: не-админ видит и меняет только свои задачи;
  legacy-задачи с пустым `owner_sid` и задачи `S-1-5-18` — только админ.
- Владелец при создании = SID вызывающего, при обновлении не меняется.
- Валидация (`auth_validate.*`): GUID, локальный абсолютный путь (запрет UNC,
  device paths, `..`, пустых сегментов, альтернативных потоков, `%TEMP%`
  для не-админа), рабочий каталог, enum, `HH:mm`, `yyyy-MM-dd`,
  диапазоны `retryCount`/`timeoutMinutes`/`maxHistoryRecords`, правила
  триггеров из IPC.md раздел 5.
- `sqlite3_exec("BEGIN"/"COMMIT")` из IPC удалён. Замена триггеров идёт через
  `Database::replace_time_windows` (трек C) с автоматическим fallback на
  `delete_time_windows` + `insert_time_window` до слияния C.
- Список задач для не-админа — `Database::list_tasks_by_owner` (трек C),
  fallback — `list_tasks` + фильтрация политикой.
- Не-админ не может задать `runAsUser`/`runAsDomain`/`password` и
  `sessionMode=ActiveUser` — `AUTH_DENIED`.

### B-6. Секреты

- Пароль приходит только при создании/обновлении (`password`, write-only),
  сразу шифруется `CryptProtectData` с `CRYPTPROTECT_SYSTEM` (`auth_secret.*`),
  blob в base64 хранится в `encrypted_password`.
- В ответах только `hasPassword`; blob не возвращается ни в v2, ни в legacy.
- Буферы затираются `SecureZeroMemory`.
- Переходно принимается старое поле `RunAsPasswordEncrypted` (старый UI
  шифрует на клиенте) — только от администратора; помечено как устаревшее.

### B-7. Аудит

- `audit_log.*`: SID, действие, тип и id объекта, результат
  (`ok`/`denied`/`error`), детали без секретов.
- Пишутся мутации (`CreateTask`, `UpdateTask`, `DeleteTask`,
  `SetTaskEnabled`, `RunNow`, `SetSettings`) и неудачная
  аутентификация/авторизация.
- Переводы строк и табуляция в деталях заменяются пробелом (log injection).
- Запись: `Database::append_audit` (трек C) при наличии `db_repository.h`,
  иначе параметризованный `INSERT`; при отсутствии таблицы — файл
  `./logs/audit.log`, чтобы событие не терялось.

### B-8. GetDashboard

Возвращает `tasks[]` с полями `id`, `name`, `enabled`, `isOwn`, `ownerLabel`,
`programPath`, `scheduleSummary`, `lastRun {startTime,status,exitCode}`,
`running` — ровно то, что ожидает `DashboardViewModel`.

### B-9. Graceful shutdown

- `stop()`: снять accept (`CancelIoEx`), дождаться потока-слушателя,
  разбудить клиентские потоки (`CancelSynchronousIo`) и **join** всех.
  `detach` не используется.
- Клиентские потоки проверяют флаг остановки в цикле чтения (шаг 5 мс).

### B-10. Тесты

- `./tests/ipc/` — автономный C++ проект: 148 проверок (кадрирование,
  конверты, JSON-лимиты, UTF-8, политика, валидация, fuzz). Запуск:
  `./tests/ipc/run.ps1`.
- `./tests/ipc/fuzz_target.cpp` — цель libFuzzer.
- Ручной прогон по каналу: v2 `Ping` (ok), `GetStatus`/`ListTasks`
  (AUTH_DENIED для гостя), `v=9` (UNSUPPORTED_VERSION), legacy `Ping`
  и `ListTasks` (PascalCase).

### B-11. IAuthPolicy

`auth::Policy`: `allows`, `can_access_task`, `can_use_credentials`,
`can_run_as_system`, `can_use_active_user`, `is_privileged`, `is_mutation`.

### B-12. GetStatus

Возвращает `running`, `isAdmin`, `isOperator`, `canManageSystemTasks`,
`serviceVersion`, `startedAt`, `taskCount`, `runningTaskCount`, `dbReady`.
`isOperator` = членство в группе или админ; при отсутствии группы — false.

### GetSettings / SetSettings (формат payload)

Клиент их пока не использует; контракт для будущего UI:

- `GetSettings`, payload `{}` → объект `{ "ключ": "значение", ... }`
  (таблица `settings`, DB.md раздел 3).
- `SetSettings`, payload — объект `{ "ключ": "значение", ... }` → `{}`.
  Неизвестные ключи принимаются как есть; нестроковые значения игнорируются.
- Оба действия — только администратор; `SetSettings` пишется в аудит.
- Запись идёт через `Database::set_setting`/`list_settings` (трек C) либо,
  до слияния, параметризованным `INSERT OR REPLACE` без `BEGIN`.

## В работе

Ничего.

## Осталось

- Прогнать контрактные тесты G (C#) после слияния и добавить
  `./tests/ipc/run.ps1` в `./scripts/verify.ps1` (зона G).
- После слияния C проверить ветку `WINSHEDULER_AUDIT_REPOSITORY`
  (`append_audit`/`list_audit`) и `replace_time_windows` на живой БД.
- Живой прогон CRUD по каналу: локальная учётная запись не входит в группу
  `WinSheduler Users`, поэтому сервер корректно отвечает `AUTH_DENIED`.
  Нужен стенд с установленной группой (F-5) или элевированный токен.

## Проверки

- `./scripts/verify.ps1 -Scope b` — успешно (build, test, format, schemas, smoke).
- `./tests/ipc/run.ps1` — 148 проверок, 0 ошибок.
- Ручной e2e по каналу — см. B-10.

## Блокеры

Нет.

## Расхождения с контрактами

1. **`IpcResponse.payload` в схеме — `object|null`.** `ListTasks`,
   `GetTimeWindows`, `GetHistory`, `GetAudit` возвращают массивы (IPC.md
   раздел 4, так же их читает `IpcClient.CallAsync<T>`). Схему нужно
   расширить до `["object","array","null"]` (запрос Architect).
2. **Старый UI в message-режиме.** Канал теперь byte-mode по контракту;
   новый `IpcClient` (D) пишет length-prefix в byte-режиме и работает.
   Legacy-конверт поддержан, но старый message-клиент к byte-каналу
   подключиться не сможет — это соответствует ADR 0003
   («UI и служба обновляются одновременно»).
3. **`startedAt` — локальное время с суффиксом `Z`.** Значение приходит
   от планировщика (зона A); схема его принимает, но по DB.md события — UTC
   (запрос A).
4. **Пароль в старом UI** по-прежнему шифруется на клиенте
   (`RunAsPasswordEncrypted`). Сервер принимает это поле только переходно;
   целевой путь — `password` в открытом виде по защищённому каналу (запрос D/E).

## Запросы к другим трекам

См. `./docs/status/b-requests.md`.
