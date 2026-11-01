# Статус трека A — планировщик

Дата: 2026-11-01.
Ветка: `track/a`, worktree `./temp/worktrees/a`.
База: `main` (Phase 0 + Track C схема v2/репозитории + Track D UI), merge выполнен.

## Текущая задача

A-1…A-16. Статус: **готово** (с оговорками, перечисленными ниже).

## Выполнено

### A-1. Модель триггера v2

- `./src/WinShedulerSvc/trigger_model.h/.cpp`: `TriggerKind` (Once/Daily/Weekly/
  Monthly/Interval), разбор и форматирование `HH:mm`, `yyyy-MM-dd`,
  `yyyy-MM-ddTHH:mm`, UTC ISO 8601 с `Z`.
- Календарная арифметика без `mktime` (дни от эпохи, день недели 0=Sunday),
  поэтому расчёт расписания не зависит от DST.
- `validate_time_window` — правила IPC.md: непустые обязательные поля по типу,
  диапазоны `dayInterval 1…365`, `weekInterval 1…52`, `monthInterval 1…12`,
  `repeatIntervalMinutes 1…1440`, `startTime < endTime`, корректность дат и
  времени, `startDate <= endDate`.
- `validate_task` — имя, путь, `retryCount 0…10`, `timeoutMinutes 0…10080`,
  `maxHistoryRecords 1…10000`, `session_mode`.
- Маппинг в БД: `trigger_kind_to_db/from_db`, легаси `type`
  (`Interval → 1`, иначе `0`), `trigger_kind_from_legacy_type`,
  JSON-массивы `days_of_week`/`days_of_month`/`specific_dates`/`exact_times`.
  SQL-привязка колонок — в репозиториях трека C (зона C).

### A-2. Чистый TriggerEngine

- `./src/WinShedulerSvc/trigger_engine.h/.cpp`: `date_matches`,
  `slot_minutes`, `is_slot`, `due_slots`, `next_run`.
- Инъекция часов: интерфейс `IClock`, системная реализация `SystemClock`,
  в тестах — `TestClock` с управляемым временем.
- Движок не знает про БД и процессы: только арифметика локального времени.

### A-3. Диапазоны дат и интервалы

- `startDate`/`endDate` включительно для всех типов.
- `Once` — `specificDates × exactTimes`.
- `Daily` — каждый `dayInterval`-й день от `startDate` (без `startDate` — от
  1970-01-01), время из `exactTimes`.
- `Weekly` — `daysOfWeek`, отсчёт недель от понедельника недели `startDate`
  (без неё — от 1970-01-05), шаг `weekInterval`.
- `Monthly` — `daysOfMonth`, отсчёт месяцев от месяца `startDate`
  (без неё — январь 1970), шаг `monthInterval`; 31-е в коротком месяце
  пропускается.
- `repeatUntil` (`yyyy-MM-ddTHH:mm`) ограничивает слоты; поддержано легаси
  значение `HH:mm` (в пределах суток).

### A-4. Interval без шторма

- `Interval` даёт слоты `startTime + k × repeatIntervalMinutes` до `endTime`
  включительно; слот один на минуту.
- Повторный тик в ту же минуту не создаёт второй процесс: слот занимается в
  `trigger_state` до запуска.
- Очередь ожидающих запусков коалесится: на задачу хранится один `PendingRun`.

### A-5. `trigger_state` и идемпотентность

- `./src/WinShedulerSvc/trigger_state.h/.cpp`: `ITriggerStateStore`,
  `MemoryTriggerStateStore` (тесты), `DatabaseTriggerStateStore` (продакшн).
- Продакшн-путь идёт через `db_.mark_trigger_fired(task, window, slot, UTC)`
  (атомарный `INSERT OR IGNORE`), `db_.get_trigger_fired_at`,
  `db_.clear_trigger_state`; слой хранения — трек C, SQL в планировщике не
  дублируется.
- Ключ слота — локальное `yyyy-MM-ddTHH:mm`, `last_fired_at` — UTC ISO 8601.
- `db_.prune_trigger_state` вызывается раз в сутки: слоты старше 90 дней.

### A-6. Кэш расписания

- `./src/WinShedulerSvc/schedule_cache.h/.cpp`: снимок задач и окон в памяти,
  перечитывание только после инвалидации.
- API: `Scheduler::invalidate_schedule()` (глобально) и
  `Scheduler::on_task_changed(task_id)` (кэш + сброс состояния слотов задачи).
- Тик больше не делает N+1 запросов: чтение БД — только при инвалидации.

### A-7. Process runner

- `./src/WinShedulerSvc/process_runner.h/.cpp`: `IProcessRunner` (для тестов) и
  `Win32ProcessRunner`.
- Job Object с `JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE`; процесс создаётся
  `CREATE_SUSPENDED`, попадает в job, затем `ResumeThread`. Kill — по дереву.
- stdout/stderr перенаправляются в `./logs/output/<taskId>/<historyId>.log`.
- Рабочая папка: явная или каталог программы; проверка существования.
- `validate_local_path` по SECURITY.md: только локальный абсолютный путь,
  запрещены UNC/device-пути (`//`, `//?/`), `..`, `.`, пустые сегменты,
  альтернативные потоки (`:` внутри сегмента), корень диска.
- Window style и `CREATE_NO_WINDOW` для `Hidden`; окружение пользователя через
  `CreateEnvironmentBlock`.

### A-8. RunAs

- `LogonUserW(LOGON32_LOGON_BATCH)`, `LoadUserProfile`/`UnloadUserProfile`,
  `CreateEnvironmentBlock`, `CreateProcessAsUserW`.
- Пароль расшифровывается `CryptUnprotectData` с `CRYPTPROTECT_SYSTEM` и
  затирается `SecureZeroMemory` сразу после `LogonUser`.
- Не-админская задача запускается токеном клиента (`client_token`); без токена
  запуск **отклоняется** (fail closed), а не уходит в SYSTEM.
- `RunAs` разрешён только при `caller_is_admin`; окончательная авторизация —
  на уровне IPC (B-5/B-11).

### A-9. Наложение и RunNow

- `WaitNext` — запуск пропускается; `WaitComplete` — один ожидающий запуск на
  задачу (коалесинг); `StartAnother` — параллельные процессы.
- `Scheduler::run_now(task_id, error)` и
  `Scheduler::run_now(task_id, client_token, caller_is_admin, error)` идут через
  общий `request_start` с учётом `onOverlap`.
- Каждый запуск привязан к `history_id`; `window_id` и `triggered_by`
  сохраняются через `db_.insert_history(history, window_id)`.
- `launch_task` сохранён для совместимости с `service.cpp`.

### A-10. Таймауты и статусы

- `timeoutMinutes` (0/NULL — без таймаута), проверка в каждом тике.
- `killOnTimeout=true` — `TerminateJobObject` и статус `Timeout`;
  `false` — статус `Timeout` фиксируется один раз, процесс остаётся под
  наблюдением до завершения.
- Статусы: `Completed` (код 0), `Failed` (ненулевой код), `Killed` (остановка
  службы), `Timeout`, `Aborted` (recovery). Обновление — по `history_id`.

### A-11. Retry/OnError

- `onError=Retry`: до `retryCount` попыток, backoff 1/2/4/8/16/30 минут.
- Повтор идёт через общую очередь `PendingRun` и не занимает слот повторно.
- `SkipNext`/`Fail` — без повтора (интерпретация зафиксирована здесь).

### A-12. Recovery

- При старте `Running` → `Aborted` с `end_time` и сообщением; выполняется до
  запуска рабочего потока.
- Опциональный перезапуск осиротевших задач не реализован (см. «Осталось»).

### A-13. UTC/DST/catch-up

- События (`startTime`, `endTime`, `lastFiredAt`, `startedAt`) — UTC ISO 8601 с
  `Z`; расписание — локальное настенное время.
- Окно catch-up — 2 минуты: задержка тика и переход через полночь не теряют
  слот и не дают шторма после простоя.
- DST: несуществующее локальное время (перевод вперёд) пропускается;
  повторяющийся час (перевод назад) даёт тот же ключ слота, поэтому запуск
  один — идемпотентность в `trigger_state`.
- Перевод системного времени назад не приводит к повтору слота (ключ занят);
  вперёд — ограничен окном catch-up.

### A-14. Тесты

- `./tests/scheduler/`: собственный харнесс без внешних зависимостей,
  fake clock (`TestClock`), fake runner (`FakeProcessRunner`), временная БД
  в `./temp/scheduler/`.
- 180 проверок: календарь, разбор/валидация, маппинг, все 5 типов триггеров,
  диапазоны, `repeatUntil`, catch-up, идемпотентность, наложение, таймаут,
  retry, RunNow, recovery, `ActiveUser`, валидация путей.
- Бенчмарк: тик по 1000 задач — **0,26 мс** (бюджет A-15 — 50 мс).
- Запуск: `./tests/scheduler/run.ps1`. В `verify.ps1` пока не включён (запрос
  треку G).

### A-15. Производительность

- N+1 убран с тика: кэш расписания; чтение БД только при инвалидации.
- Тик по 1000 задач — 0,26 мс; бюджет не ухудшён.

### A-16. `session_mode`

- Поле есть в модели (`SessionMode::Headless/ActiveUser`) и в БД (трек C).
- `ActiveUser` отклоняется `validate_task` и повторно — в `request_start`
  (защита в глубину). Поле зарезервировано под будущий ADR.

## Принятые решения

1. **`timeoutMinutes = 0` — таймаут отключён.** Контракт допускает 0; трактовка
   «0 минут = мгновенный таймаут» непрактична.
2. **Backoff повторов** — 1/2/4/8/16/30 минут, не более `retryCount` попыток.
3. **Retention `trigger_state`** — 90 дней (в контракте не задан).
4. **Fail closed для не-админских задач без токена.** Планировщик не запускает
   их от SYSTEM; нужен токен владельца (см. `a-requests.md`, Architect).
5. **Легаси `repeatUntil = "HH:mm"`** поддерживается как ограничение в пределах
   суток — для существующих БД.
6. **`db_compat`** — тонкий адаптер над `Database`; после слияния Track C
   запасной путь v1 удалён, планировщик использует только API трека C.

## Проверки

- `./scripts/verify.ps1 -Scope a` — успешно (build, test, format, schemas,
  smoke), после merge `main`.
- `./tests/scheduler/run.ps1` — 180 проверок, 0 ошибок.
- Бенчмарк 1000 задач — 0,26 мс на тик.

## Осталось

- Включить `./tests/scheduler/run.ps1` в gate (трек G) и добавить тестовый
  проект в решение (Integrator).
- Вызывать `Scheduler::invalidate_schedule()`/`on_task_changed(task_id)` из
  обработчиков CRUD/toggle (трек B, `ipc.cpp`) — иначе кэш обновится только на
  следующем старте службы.
- Передавать `DatabaseOptions::output_dir` при открытии БД (Integrator), иначе
  `purge_expired_outputs` ничего не делает.
- `RunNow` из IPC должен вызывать вариант с `client_token` и
  `caller_is_admin` (B/Integrator).
- Опциональный перезапуск осиротевших задач после recovery (A-12).
- Повтор при `Timeout` (сейчас retry только для `Failed`) — по решению
  Architect.

## Блокеры

Нет. Все зависимости (C-2, схема v2) получены через merge `main`.

## Запросы к другим трекам

См. `./docs/status/a-requests.md`.
