# Контракт хранения (SQLite) v2

Статус: заморожен. Изменения — только через ADR.

## 1. Общие правила

- Движок: SQLite (amalgamation, версия фиксируется в `./src/WinShedulerSvc/sqlite3.h`).
- Режим: WAL, `foreign_keys=ON`, `busy_timeout=5000`, `synchronous=NORMAL`.
- Одно соединение на процесс. Все записи — через один writer (очередь или мьютекс).
- Транзакции: `BEGIN IMMEDIATE` + RAII-rollback. Вложенные `BEGIN` запрещены.
- Все SQL — параметризованные.
- Время в БД:
  - расписание (`exactTimes`, `startTime`, `endTime`, `repeatUntil`, `startDate`, `endDate`) — локальное настенное время;
  - события (`createdAt`, `modifiedAt`, `startTime`, `endTime`, `lastFiredAt`, `ts`) — UTC ISO 8601 с `Z`.
- Кодировка текста — UTF-8.
- Файл БД и каталог — только `SYSTEM` + `Administrators`.

## 2. Версия схемы

Версия хранится в `PRAGMA user_version` (целое).

| Версия | Состояние |
|--------|-----------|
| 1 | текущая рабочая схема |
| 2 | целевая схема (этот документ) |

Миграция выполняется при старте службы, идемпотентно, в одной транзакции. При ошибке — откат и отказ старта с записью в лог.

## 3. Схема v2

### `tasks`

| Колонка | Тип | Ограничения | Примечание |
|---------|-----|-------------|------------|
| `id` | TEXT | PK | GUID в нижнем регистре |
| `owner_sid` | TEXT | NOT NULL | SID создателя; для legacy — `S-1-5-18` |
| `name` | TEXT | NOT NULL DEFAULT '' | |
| `description` | TEXT | NOT NULL DEFAULT '' | |
| `enabled` | INTEGER | NOT NULL DEFAULT 1 | 0/1 |
| `program_path` | TEXT | NOT NULL DEFAULT '' | локальный абсолютный путь |
| `arguments` | TEXT | NOT NULL DEFAULT '' | |
| `working_directory` | TEXT | NOT NULL DEFAULT '' | |
| `window_style` | INTEGER | NOT NULL DEFAULT 1 | 0 Normal, 1 Hidden, 2 Minimized, 3 Maximized |
| `session_mode` | INTEGER | NOT NULL DEFAULT 0 | 0 Headless, 1 ActiveUser (резерв) |
| `run_as_user` | TEXT | NULL | только админ |
| `run_as_domain` | TEXT | NULL | только админ |
| `encrypted_password` | TEXT | NULL | DPAPI `CRYPTPROTECT_SYSTEM`, base64 |
| `on_error` | INTEGER | NOT NULL DEFAULT 0 | 0 SkipNext, 1 Retry, 2 Fail |
| `retry_count` | INTEGER | NOT NULL DEFAULT 0 | 0…10 |
| `on_overlap` | INTEGER | NOT NULL DEFAULT 0 | 0 WaitComplete, 1 WaitNext, 2 StartAnother |
| `timeout_minutes` | INTEGER | NULL | 0…10080 |
| `kill_on_timeout` | INTEGER | NOT NULL DEFAULT 0 | |
| `log_output` | INTEGER | NOT NULL DEFAULT 0 | |
| `max_history_records` | INTEGER | NOT NULL DEFAULT 1000 | 1…10000 |
| `created_at` | TEXT | NOT NULL DEFAULT '' | UTC ISO 8601 |
| `modified_at` | TEXT | NOT NULL DEFAULT '' | UTC ISO 8601 |

Индексы: `idx_tasks_owner (owner_sid)`, `idx_tasks_enabled (enabled)`.

### `time_windows`

| Колонка | Тип | Ограничения | Примечание |
|---------|-----|-------------|------------|
| `id` | TEXT | PK | GUID |
| `task_id` | TEXT | NOT NULL, FK → `tasks(id)` ON DELETE CASCADE | |
| `kind` | INTEGER | NOT NULL DEFAULT 1 | 0 Once, 1 Daily, 2 Weekly, 3 Monthly, 4 Interval |
| `start_date` | TEXT | NULL | `yyyy-MM-dd` |
| `end_date` | TEXT | NULL | `yyyy-MM-dd` |
| `days_of_week` | TEXT | NOT NULL DEFAULT '[]' | JSON-массив 0…6, 0=Sunday |
| `days_of_month` | TEXT | NOT NULL DEFAULT '[]' | JSON-массив 1…31 |
| `specific_dates` | TEXT | NOT NULL DEFAULT '[]' | JSON-массив `yyyy-MM-dd` |
| `exact_times` | TEXT | NOT NULL DEFAULT '[]' | JSON-массив `HH:mm` |
| `start_time` | TEXT | NULL | `HH:mm` |
| `end_time` | TEXT | NULL | `HH:mm` |
| `repeat_interval_minutes` | INTEGER | NULL | 1…1440 |
| `repeat_until` | TEXT | NULL | `yyyy-MM-ddTHH:mm` (локальное) |
| `day_interval` | INTEGER | NOT NULL DEFAULT 1 | 1…365 |
| `week_interval` | INTEGER | NOT NULL DEFAULT 1 | 1…52 |
| `month_interval` | INTEGER | NOT NULL DEFAULT 1 | 1…12 |

Индекс: `idx_time_windows_task (task_id)`.

### `trigger_state`

Состояние срабатываний, чтобы не запускать задачу повторно в тот же слот.

| Колонка | Тип | Ограничения |
|---------|-----|-------------|
| `task_id` | TEXT | NOT NULL |
| `window_id` | TEXT | NOT NULL |
| `slot` | TEXT | NOT NULL (`yyyy-MM-ddTHH:mm` локальное) |
| `last_fired_at` | TEXT | NOT NULL (UTC ISO 8601) |
| `PRIMARY KEY (task_id, window_id, slot)` | | |

Индекс: `idx_trigger_state_task (task_id)`.

### `run_history`

| Колонка | Тип | Ограничения | Примечание |
|---------|-----|-------------|------------|
| `id` | TEXT | PK | GUID |
| `task_id` | TEXT | NOT NULL, FK → `tasks(id)` ON DELETE CASCADE | |
| `window_id` | TEXT | NULL | NULL для `RunNow` |
| `start_time` | TEXT | NOT NULL | UTC ISO 8601 |
| `end_time` | TEXT | NULL | UTC ISO 8601 |
| `exit_code` | INTEGER | NULL | |
| `pid` | INTEGER | NOT NULL DEFAULT 0 | |
| `status` | INTEGER | NOT NULL DEFAULT 0 | 0 Running, 1 Completed, 2 Failed, 3 Killed, 4 Timeout, 5 Aborted |
| `error_message` | TEXT | NOT NULL DEFAULT '' | |
| `output_path` | TEXT | NULL | путь к файлу вывода |
| `triggered_by` | INTEGER | NOT NULL DEFAULT 0 | 0 Schedule, 1 Manual |

Индексы: `idx_run_history_task_start (task_id, start_time DESC)`, `idx_run_history_status (status)`.

### `audit_log`

| Колонка | Тип | Ограничения |
|---------|-----|-------------|
| `id` | INTEGER | PK AUTOINCREMENT |
| `ts` | TEXT | NOT NULL (UTC ISO 8601) |
| `actor_sid` | TEXT | NOT NULL |
| `action` | TEXT | NOT NULL |
| `object_type` | TEXT | NOT NULL |
| `object_id` | TEXT | NULL |
| `result` | TEXT | NOT NULL (`ok` / `denied` / `error`) |
| `details` | TEXT | NOT NULL DEFAULT '' (без секретов) |

Индекс: `idx_audit_ts (ts DESC)`.

### `settings`

| Колонка | Тип | Ограничения |
|---------|-----|-------------|
| `key` | TEXT | PK |
| `value` | TEXT | NOT NULL |

Ключи v1: `service.version`, `service.started_at`, `install.owner_sid`, `retention.output_days`.

## 4. Миграция v1 → v2

1. `PRAGMA user_version` = 1 → начать транзакцию.
2. Добавить в `tasks`: `owner_sid`, `session_mode`. Заполнить `owner_sid` значением `install.owner_sid` или `S-1-5-18`; `session_mode=0`.
3. Добавить в `time_windows`: `kind`, `day_interval`, `week_interval`, `month_interval`.
4. Заполнить `kind`:
   - `type=1` (Interval) → `kind=4`;
   - `type=0` и `days_of_month` непустой → `kind=3`;
   - `type=0` и `days_of_week` непустой → `kind=2`;
   - иначе → `kind=1` (Daily).
5. Создать `trigger_state`, `audit_log`, `settings`.
6. Создать индексы.
7. `PRAGMA user_version = 2`, `COMMIT`.

Старые колонки не удаляются в v2 (совместимость и откат). Удаление — в v3 через ADR.

## 5. Транзакции и конкурентность

- Все мутации — через writer-поток. IPC-потоки ставят операции в очередь и ждут результат.
- `BEGIN IMMEDIATE`; при ошибке — `ROLLBACK` в деструкторе guard-объекта.
- Чтение — из тех же репозиториев; допускается параллельное чтение в WAL.
- `busy_timeout=5000`; при `SQLITE_BUSY` — до 3 повторов с backoff 50/200/500 мс.
- Запрещено: `sqlite3_exec("BEGIN")` из обработчиков IPC, вложенные транзакции, `SELECT *` в новом коде.

## 6. Retention

- После вставки истории: удалить записи сверх `tasks.max_history_records` (сначала самые старые).
- Файлы `./logs/output/<taskId>/<historyId>.log` старше `retention.output_days` (по умолчанию 30) удаляются при старте и раз в сутки.
- `audit_log` — хранить 180 дней (настраивается в `settings`).

## 7. Резервное копирование

- Штатный способ: `VACUUM INTO './temp/backup/scheduler-<timestamp>.db'` (или `sqlite3_backup`).
- Копирование файла БД без checkpoint WAL запрещено.
- Восстановление — только при остановленной службе.

## 8. Проверки целостности

- При старте: `PRAGMA quick_check`; при ошибке — запись в лог и отказ старта (данные не трогать).
- Перед миграцией — копия в `./temp/backup/` (не в каталоге данных).
