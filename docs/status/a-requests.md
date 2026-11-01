# Запросы трека A к другим трекам

Дата: 2026-11-01. Контракты: `IPC.md`, `DB.md`, `SECURITY.md`; ADR 0001, 0003.

## Integrator

### 1. Инвалидация кэша расписания после CRUD

Файлы: `./src/WinShedulerSvc/ipc.cpp` (обработчики `CreateTask`, `UpdateTask`,
`DeleteTask`, `SetTaskEnabled`), `./src/WinShedulerSvc/service.cpp`.

После успешной мутации нужно вызвать:

```cpp
g_sched->on_task_changed(task_id);   // кэш + сброс trigger_state задачи
// или g_sched->invalidate_schedule(); — если затронуто несколько задач
```

Без этого планировщик увидит изменения только после перезапуска службы
(кэш перечитывается при инвалидации). `Scheduler::on_task_changed` объявлен в
`./src/WinShedulerSvc/scheduler.h`.

### 2. `RunNow` с токеном вызывающего

`service.cpp` сейчас вызывает `g_sched->launch_task(task)`, то есть запуск идёт
в контексте службы. Для не-админа нужен токен клиента:

```cpp
std::wstring error;
auto history_id = g_sched->run_now(task_id, client_token, caller_is_admin, error);
```

`client_token` — primary-токен из `ImpersonateNamedPipeClient` +
`DuplicateTokenEx(TokenPrimary)` (трек B). `caller_is_admin` — результат
проверки членства в `Administrators` (B-11).

### 3. `DatabaseOptions::output_dir`

`Database::open(path, options)` (трек C) чистит старые файлы вывода только при
заданном `options.output_dir`. Планировщик вызывает
`db_.purge_expired_outputs()` раз в сутки, но без `output_dir` это no-op.

```cpp
DatabaseOptions options;
options.output_dir = log_dir + L"/output";
options.backup_dir = data_dir + L"/backup";
options.require_secure_acl = true;   // только для службы
db.open(db_path, options);
```

### 4. Тестовый проект трека A в решении

`./tests/scheduler/WinShedulerSchedulerTests.vcxproj` не входит в
`./WinSheduler.sln` (чтобы не менять общий файл из трека A). Просьба добавить
его в папку `tests` или поручить включение треку G.

## Track B (IPC и безопасность)

### 1. Инвалидация кэша после мутаций

См. п. 1 для Integrator: `ipc.cpp` — зона B. Вызов
`Scheduler::on_task_changed(task_id)` после `insert_task`/`update_task`/
`delete_task`/`set_task_enabled` и после `replace_time_windows`.

### 2. Токен владельца для не-админских задач

`SECURITY.md` требует запускать не-админскую задачу токеном клиента. Для
`RunNow` токен доступен в обработчике IPC. Для **запуска по расписанию** токена
нет: служба работает в session 0, а сохранять пароль не-админа запрещено.
Сейчас планировщик **отклоняет** такой запуск (fail closed, статус `Failed`,
сообщение «Нет токена владельца задачи»), а не запускает от SYSTEM.

Нужно решение Architect (ADR): либо хранить долгоживущий токен/секрет
(расширяет модель угроз), либо явно разрешить не-админские задачи только через
`RunNow` при активном сеансе, либо иной механизм. До решения поведение
безопасное, но не-админские задачи по расписанию не работают.

### 3. `session_mode=ActiveUser`

Отклоняется в `validate_task` и в `Scheduler::request_start`. IPC должен
возвращать `VALIDATION`, не создавая задачу.

## Track C (хранение)

API используется по назначению, изменений не требуется:

- `db_.mark_trigger_fired` / `get_trigger_fired_at` / `clear_trigger_state` /
  `prune_trigger_state` — через `./src/WinShedulerSvc/db_compat.*`;
- `db_.insert_history(history, window_id)`;
- `db_.purge_expired_outputs()`;
- `db_.task_count()`.

Замечание: recovery осиротевших запусков (`Running → Aborted`) выполняется одним
параметризованным `UPDATE` напрямую через `get_db()` в `Scheduler::start()`.
Это происходит до старта рабочих потоков, поэтому writer-мьютекс не нужен; если
появится `Database::abort_running_runs()`, переключимся на него.

## Track G (QA и gate)

1. Включить `./tests/scheduler/run.ps1` в `./scripts/verify.ps1 -Scope a`.
   Сейчас gate собирает решение и C#-тесты, но C++-тесты планировщика не
   запускает.
2. Числа бенчмарка (тик по 1000 задач ≈ 0,26 мс) можно использовать в
   `bench.ps1` (G-7). Отчёт печатается в stdout тестов.

## Architect (уточнения контрактов)

1. **Не-админские задачи по расписанию** — см. Track B п. 2. Нужен ADR.
   Дополнительно: чтобы отличать админскую задачу (запуск от SYSTEM разрешён) от
   не-админской, в `TaskDefinition` нужен признак, например
   `bool run_as_system` (или `owner_is_admin`), который выставляет IPC при
   создании. Сейчас планировщик может опираться только на `owner_sid` и потому
   консервативно требует токен для любой задачи, кроме legacy `S-1-5-18`.
   Правка `config.h` — зона Integrator, поэтому поле не добавлено.
2. **`timeoutMinutes = 0`** — трактуется как «таймаут отключён». Просьба
   зафиксировать в `IPC.md`/`DB.md`.
3. **Retention `trigger_state`** — 90 дней, в контракте не задан. Просьба
   добавить в `DB.md`, раздел 6.
4. **`onError = Fail`** — трактуется как «без повтора, задача остаётся
   включённой, ошибка в истории». Если имелось в виду «выключить задачу»,
   уточнить.
5. **Повтор при `Timeout`** — сейчас retry только для `Failed`. Нужно ли
   повторять по таймауту?
