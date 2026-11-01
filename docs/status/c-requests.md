# Запросы трека C к другим трекам

Дата: 2026-11-01. Контракты `./docs/contracts/DB.md`, `SECURITY.md`, `IPC.md`.

## Integrator

### 1. Передать `DatabaseOptions` при открытии БД

Файлы: `./src/WinShedulerSvc/main.cpp`, `./src/WinShedulerSvc/service.cpp`.

`Database::open` принимает второй аргумент `DatabaseOptions`
(значения по умолчанию сохраняют текущее поведение):

```cpp
DatabaseOptions options;
options.output_dir = log_dir + L"/output";      // retention файлов вывода
options.backup_dir = data_dir + L"/backup";     // копии перед миграцией
options.require_secure_acl = true;              // только для службы
db.open(db_path, options);
```

Без `output_dir` очистка старых файлов вывода не выполняется
(`Database::purge_expired_outputs` ничего не делает). Без
`require_secure_acl` проверка ACL каталога данных не применяется.

### 2. Добавить тестовый проект в решение

`./tests/db/db_tests.vcxproj` не входит в `./WinSheduler.sln`.
Проект самодостаточен (модули трека C + `json.cpp` + `sqlite3.c`), запуск —
`./tests/db/run.ps1`. Просьба добавить его в решение (папка `tests`) или
включить запуск в gate через Track G.

### 3. `/utf-8` в `WinShedulerSvc.vcxproj` (уже добавлен)

В обе конфигурации добавлено `<AdditionalOptions>/utf-8 %(AdditionalOptions)</AdditionalOptions>`.
Причина: без `/utf-8` MSVC читает UTF-8 исходники как ANSI и портит русские
строковые литералы. Просьба учесть при слиянии и не потерять строку.

### 4. Аудит старта/остановки службы

`SECURITY.md`, раздел 6 требует аудит старта и остановки службы. Трек C даёт
`Database::append_audit(AuditEntry&)`; вызовы из `service.cpp` — зона
Integrator/Track B.

## Track B (IPC и безопасность)

### 1. Убрать «сырые» транзакции из `ipc.cpp`

`./src/WinShedulerSvc/ipc.cpp` (строки ~595 и ~618) выполняет
`sqlite3_exec(db_.get_db(), "BEGIN"/"COMMIT")` — это запрещено контрактом
(DB.md, раздел 5) и обходит writer-мьютекс. Замена:

```cpp
db_.replace_time_windows(task->id, windows);   // одна транзакция
```

Для нескольких операций — `Database::Transaction` (RAII-rollback,
`BEGIN IMMEDIATE`, вложенность запрещена).

### 2. Фильтрация по владельцу

Для не-админа используйте `db_.list_tasks_by_owner(sid)` вместо
`list_tasks()` + фильтрации на клиенте.

### 3. История и аудит

- `db_.get_history_records(task_id, limit)` возвращает `HistoryRecord`
  (`RunHistory` + `window_id`) — для `GetDashboard`/`GetHistory`.
- `db_.append_audit(AuditEntry&)` — поля `ts`, `actor_sid`, `action`,
  `object_type`, `object_id`, `result`, `details`; `ts` заполняется
  автоматически, если пуст.
- `db_.list_audit(from, to, limit)` — для `GetAudit` (только админ).

### 4. `owner_sid` при создании

`TaskRepository::insert` пишет `owner_sid` из `TaskDefinition`; при
`update_task` владелец не меняется. Handler создания должен явно выставлять
`owner_sid = SID вызывающего` (для legacy — `S-1-5-18`).

## Track A (планировщик)

### 1. Состояние слотов

```cpp
if (db_.mark_trigger_fired(task_id, window_id, slot, db_utc_now())) {
    // слот свободен — запускаем; повторная отметка вернёт false
}
db_.prune_trigger_state(task_id, L"2026-01-01T00:00"); // старые слоты
```

`slot` — локальное `yyyy-MM-ddTHH:mm`, `last_fired_at` — UTC ISO 8601.

### 2. История

`db_.insert_history(history, window_id)` — `window_id` передаётся отдельно,
потому что поля нет в `RunHistory` (`config.h` — зона Integrator).
`insert_history` сам применяет retention по `tasks.max_history_records`;
повторный `trim_history` не вредит, но не обязателен.

### 3. `kind` и легаси `type`

При вставке окна колонка `type` заполняется автоматически
(`Interval → 1`, иначе `0`). Переходите на `TimeWindow.kind`; колонка `type`
удаляется только в v3 (через ADR).

### 4. Retention файлов вывода

`db_.purge_expired_outputs()` вызывается при старте. Для ежедневной очистки
вызовите её из тика планировщика раз в сутки (или это сделает Integrator).

## Track G (QA и gate)

1. G-2: включить `./tests/db/run.ps1` в `./scripts/verify.ps1 -Scope c`
   (сейчас gate не знает о тестах трека C).
2. G-7: числа бенчмарка 1000 задач из `./tests/db/run.ps1` (отчёт
   `./temp/c/db-tests/db-tests-report.txt`) можно использовать в `bench.ps1`.
3. Контрактные тесты схемы (G-4) могут опираться на `migrations::column_exists`
   и `PRAGMA user_version` — фикстуры v1 есть в `./tests/db/db_tests.cpp`.

## Architect (уточнения контрактов)

1. `DB.md`, раздел 4: в шагах миграции нет `run_history.window_id` и
   `run_history.triggered_by`, хотя они есть в целевой схеме. Трек C добавляет
   их при миграции; просьба зафиксировать в контракте.
2. `DB.md`, раздел 2: v1 = `user_version 1`, но фактические БД имеют
   `user_version 0` (старый код не писал версию). Трек C трактует
   «0 + таблица tasks» как v1; просьба уточнить контракт.
3. `DB.md`, раздел 6: не назван ключ `settings` для срока хранения аудита.
   Используется `retention.audit_days`.
4. `DB.md`, раздел 7: копия «в `./temp/backup/`» неприменима к службе;
   предлагается формулировка «каталог из `DatabaseOptions::backup_dir`,
   по умолчанию `<DATADIR>/backup`».
5. `config.h` (зона Integrator): в `RunHistory` нет `window_id`. Трек C
   обходится `HistoryRecord`; если поле нужно в IPC, просьба добавить его
   в общую модель через Integrator.
