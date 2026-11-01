# Статус трека C (хранение)

## Текущая задача

C-1…C-8 — все задачи трека закрыты. Статус: **готово**.

## Выполнено

### C-1. Схема v2 и миграции

- `./src/WinShedulerSvc/migrations.h/.cpp`: целевая версия `DB_SCHEMA_VERSION = 2`
  в `PRAGMA user_version`; DDL v2 (`tasks`, `time_windows`, `trigger_state`,
  `run_history`, `audit_log`, `settings`, индексы контракта); идемпотентное
  создание через `CREATE ... IF NOT EXISTS`; миграция v1 → v2 (`ALTER TABLE`,
  заполнение `kind`, новые таблицы и индексы).
- `Database::migrate()`: версия 0 без таблицы `tasks` → новая БД сразу v2;
  версия 0 с `tasks` → legacy v1; версия 1 → миграция; версия 2 → досоздание
  недостающих объектов; версия больше 2 → отказ старта.
- Миграция выполняется в `BEGIN IMMEDIATE`; при ошибке — откат и отказ открытия.
- Перед миграцией — резервная копия через `sqlite3_backup`.
- Факт миграции пишется в `audit_log` (`action = db.migrate`).

### C-2. Репозитории

- `./src/WinShedulerSvc/db_repository.h/.cpp`: `TaskRepository`,
  `TimeWindowRepository`, `HistoryRepository`, `SettingsRepository`,
  `AuditRepository`, `TriggerStateRepository`.
- Все запросы перечисляют колонки явно, `SELECT *` в новом коде отсутствует.
- `./src/WinShedulerSvc/db_stmt.h/.cpp`: пул подготовленных выражений
  (переиспользование по тексту SQL, потокобезопасная выдача, RAII-обёртка
  `Statement`, повтор шага при `SQLITE_BUSY` — 50/200/500 мс).
- `Database` остаётся фасадом: существующие методы сохранены, добавлены
  `list_tasks_by_owner`, `replace_time_windows`, `get_history_records`,
  настройки, аудит, `trigger_state`, retention, backup/export.

### C-3. Один writer и транзакции

- `Database::Transaction` — `BEGIN IMMEDIATE` + `std::recursive_mutex` +
  RAII-rollback; вложенные транзакции отклоняются `DbError`.
- Все мутации проходят через writer-мьютекс; чтения идут параллельно в WAL.

### C-4. Прагмы и целостность

- `busy_timeout=5000`, `journal_mode=WAL`, `synchronous=NORMAL`,
  `foreign_keys=ON`, `wal_autocheckpoint=1000`.
- `PRAGMA quick_check` при старте: ошибка → отказ открытия, данные не меняются.
- `PRAGMA wal_checkpoint(TRUNCATE)` при закрытии, затем `sqlite3_close_v2`.

### C-5. Retention

- `HistoryRepository::insert` сразу удаляет записи сверх
  `tasks.max_history_records` (самые старые по `start_time`).
- `trim_history` — то же по явному лимиту (1…10000).
- `Database::purge_expired_outputs()` — файлы
  `logs/output/<taskId>/<historyId>.log` старше `retention.output_days`
  (по умолчанию 30); удаляются только каталоги и файлы с GUID-именами.
- `Database::purge_expired_audit()` — аудит старше `retention.audit_days`
  (по умолчанию 180).
- Обе очистки выполняются при старте; их ошибки не блокируют открытие.

### C-6. Путь, ACL, backup/export

- `./src/WinShedulerSvc/db_path.h/.cpp`:
  - `db_path_is_valid` — только локальный абсолютный путь; запрещены UNC,
    device paths, `..`, `.`, пустые сегменты, альтернативные потоки,
    управляющие символы, путь каталога;
  - `db_dir_acl_is_restricted` — проверяет, что запись в каталог не выдана
    Everyone / Users / Authenticated Users / Interactive;
  - `db_ensure_directory` — рекурсивное создание каталога.
- ACL проверяется при `DatabaseOptions::require_secure_acl = true`
  (по умолчанию выключено, чтобы не ломать standalone и smoke-тест).
- `backup_to` — `sqlite3_backup`, запрещён внутри транзакции.
- `export_history_csv` — UTF-8 с BOM, экранирование CSV, валидация пути.

### C-7. Миграция существующей БД и фикстуры

- `./tests/db/db_tests.cpp` создаёт фикстуры v1: `user_version=0` (legacy)
  с `install.owner_sid` и `user_version=1` без него.
- Проверено: данные сохраняются; `owner_sid` = `install.owner_sid` или
  `S-1-5-18`; `kind` = 4/3/2/1 по правилам контракта; интервалы = 1;
  `session_mode = 0`; `run_history` получает `window_id` и `triggered_by`;
  запись аудита миграции ровно одна, повторное открытие не мигрирует.

### C-8. Индексы, планы запросов, бенчмарк

- Созданы индексы контракта. `EXPLAIN QUERY PLAN` подтверждает использование
  `idx_tasks_owner`, `idx_tasks_enabled`, `idx_time_windows_task`,
  `idx_run_history_task_start`, `idx_run_history_status`, `idx_audit_ts`.
- Бенчмарк 1000 задач (2 окна и 1 запись истории на задачу):
  вставка ~60–70 мс, `list_tasks` ~2.7 мс, `list_tasks_by_owner` ~2.4 мс,
  1000 × `get_time_windows` ~120 мс, 1000 × `get_history(1)` ~70 мс.

## Принятые решения

1. **Legacy `user_version = 0`.** Старый код не писал `user_version`, поэтому
   БД с `user_version=0` и таблицей `tasks` трактуется как v1 и мигрирует;
   пустая БД без `tasks` создаётся сразу как v2.
2. **Легаси-колонки сохраняются.** `time_windows.type` остаётся в схеме v2
   (контракт, раздел 4) и синхронизируется при вставке: `Interval → 1`,
   остальные → `0`. Это сохраняет совместимость с текущим `scheduler.cpp`
   до перехода Track A на `kind`.
3. **`run_history.window_id` и `triggered_by`.** Колонки есть в целевой схеме,
   но не упомянуты в шагах миграции (DB.md, раздел 4). Добавлены в миграцию.
   В `config.h` поля `window_id` нет, поэтому оно передаётся отдельным
   аргументом `insert_history` и читается через `HistoryRecord`.
4. **Пул подготовленных выражений.** `StatementPool` хранит свободные
   выражения по тексту SQL и выдаёт их через RAII-обёртку `Statement` —
   это и есть требуемый кэш prepared statements.
5. **Один writer — рекурсивный мьютекс + `BEGIN IMMEDIATE`.** Рекурсивность
   нужна, чтобы методы `Database` вызывались внутри транзакции без
   самоблокировки; вложенный `Transaction` всё равно запрещён.
6. **`/utf-8` для C++.** В `WinShedulerSvc.vcxproj` добавлен `/utf-8`: без него
   MSVC читает UTF-8 исходники как ANSI и портит русские строковые литералы.
   Изменение минимальное — одна строка в каждой конфигурации.
7. **Каталог резервных копий по умолчанию — `<каталог БД>/backup`.**
   Контракт упоминает `./temp/backup/`, но служба не должна писать в каталог
   репозитория; фактический путь задаётся `DatabaseOptions::backup_dir`.
8. **Retention файлов вывода включается только при заданном `output_dir`.**
   Иначе `purge_expired_outputs` ничего не делает.

## Расхождения с контрактами

1. `DB.md`, раздел 4, шаг 3: не перечислены `run_history.window_id` и
   `run_history.triggered_by`, хотя они есть в целевой схеме (раздел 3).
   Добавлены при миграции.
2. `DB.md`, раздел 7: резервная копия «в `./temp/backup/`». По умолчанию
   используется `<каталог БД>/backup`; путь переопределяется через
   `DatabaseOptions::backup_dir`.
3. `DB.md`, раздел 2: v1 определена как `user_version = 1`, но фактические
   БД имеют `user_version = 0`. Поддержаны оба варианта.
4. `DB.md`, раздел 6: ключ настройки срока хранения аудита не назван;
   используется `retention.audit_days`.
5. `DB.md`, раздел 1: «файл БД и каталог — только SYSTEM + Administrators».
   Проверка ACL реализована, но по умолчанию не включена:
   `DatabaseOptions::require_secure_acl`. Включение — на стороне Integrator.

## В работе

Ничего.

## Осталось

- Integrator: передать `DatabaseOptions` из `main.cpp`/`service.cpp`
  (`output_dir`, `backup_dir`, `require_secure_acl = true` для службы), иначе
  retention файлов вывода и строгая проверка ACL не активны.
- Integrator: добавить `./tests/db/db_tests.vcxproj` в `./WinSheduler.sln`.
- Track G (G-2): включить `./tests/db/run.ps1` в `./scripts/verify.ps1`
  и числа бенчмарка в `./scripts/bench.ps1`.
- Track B: заменить `sqlite3_exec(db_.get_db(), "BEGIN"/"COMMIT")` в `ipc.cpp`
  на `Database::replace_time_windows` / `Database::Transaction`; использовать
  `list_tasks_by_owner`, `AuditEntry`, `get_history_records`.
- Track A: перейти на `mark_trigger_fired` / `get_trigger_fired_at` /
  `prune_trigger_state`, передавать `window_id` в `insert_history`.

## Проверки

- `./scripts/verify.ps1 -Scope c` — успешно (build, test, format, schemas, smoke).
- `./tests/db/run.ps1` — успешно, 226 проверок, 0 ошибок.
- Бенчмарк 1000 задач — см. C-8.
- Пересборка `WinShedulerSvc.vcxproj` с `/t:Rebuild` — без новых предупреждений.

## Блокеры

Нет.
