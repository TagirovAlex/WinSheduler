# WinSheduler — техническая спецификация

Актуальная версия: 2.1.1. Детальные контракты — в `./docs/contracts/`; этот документ описывает архитектуру и ключевые решения.

## 1. Назначение

Служба Windows с графическим интерфейсом для планирования и запуска задач. Не зависит от встроенного Task Scheduler. В v1 задачи выполняются headless: окно процесса не отображается на рабочем столе пользователя.

## 2. Платформы

- Windows 10 1607+ / Windows Server 2016+ (x64). UI требует Desktop Experience.
- Сборка: Visual Studio 2022/2026 Build Tools (MSVC v143/v145), .NET 10 SDK, WiX 5 для MSI.
- UI: self-contained, .NET Runtime не требуется.

## 3. Стек

| Компонент | Технология |
|-----------|------------|
| Служба | C++20, Win32, `/MT`, SQLite amalgamation |
| UI | C# WPF, .NET 10 |
| IPC | именованный канал `//./pipe/WinSheduler`, v1, length-prefix |
| Хранилище | SQLite, WAL, схема v2 |
| Шифрование | DPAPI `CRYPTPROTECT_SYSTEM` |
| Установка | WiX MSI, self-contained UI |

## 4. Архитектура

```
WinSheduler.UI.exe (asInvoker)
  │  IPC v1: 4 байта длины LE + UTF-8 JSON
  ▼
WinShedulerSvc.exe (LocalSystem, session 0)
  ├─ trigger_model / trigger_engine / trigger_state
  ├─ process_runner (Job Object, RunAs, таймауты)
  ├─ ipc_frame / auth_policy / auth_token / audit_log
  ├─ migrations / db_repository / db_stmt
  └─ headless-процессы задач
```

## 5. Модель данных

Схема v2, миграция по `PRAGMA user_version`:

- `tasks` — задача, `owner_sid`, `session_mode`, параметры запуска.
- `time_windows` — триггеры: `kind`, даты, дни, времена, интервалы.
- `trigger_state` — занятые слоты `(task_id, window_id, slot)`.
- `run_history` — запуски, `window_id`, `triggered_by`, статусы.
- `audit_log` — аудит действий.
- `settings` — ключи retention и служебные значения.

Полный DDL и правила миграции — `./docs/contracts/DB.md`.

## 6. Алгоритм планирования

1. Тик раз в секунду.
2. Кэш расписания; при изменении задач — инвалидация.
3. Для каждого триггера вычисляется слот `yyyy-MM-ddTHH:mm` в локальном времени.
4. `trigger_state` атомарно занимает слот; повторный запуск в том же слоте запрещён.
5. Учитываются диапазоны дат, `SpecificDates`, интервалы дней/недель/месяцев, `RepeatUntil`.
6. Проверяется политика наложения: `WaitComplete`, `WaitNext`, `StartAnother`.
7. Запуск через `process_runner`; таймаут, Job Object, retry.
8. При старте осиротевшие `Running` помечаются `Aborted`.

События хранятся в UTC, расписание — в локальном настенном времени. Политика DST — в `./docs/contracts/DB.md` и ADR 0003.

## 7. IPC

- Кадр: 4 байта длины little-endian + UTF-8 JSON.
- Конверт: `v`, `id`, `action`, `payload`; ответ: `ok`, `error`, `payload`.
- Лимиты: 1 МиБ, глубина JSON 64, строка 64 КиБ.
- Аутентификация: impersonation, SID, группа `WinSheduler Users` или `Administrators`.
- Не-администратор видит только свои задачи и запускает их от своего имени.
- `GetDashboard` заменяет серию вызовов `ListTasks + N × GetTimeWindows + N × GetHistory`.
- Legacy-конверт `{Action, Payload}` поддерживается переходно.

Полный контракт — `./docs/contracts/IPC.md`.

## 8. Безопасность

- Группа `WinSheduler Users` — маркер доступа к IPC, без прав администратора.
- SYSTEM/RunAs — только администраторы.
- Не-админская задача запускается токеном клиента.
- Пароли шифруются в службе `CRYPTPROTECT_SYSTEM`; UI получает только `hasPassword`.
- Pipe: явный SDDL, `PIPE_REJECT_REMOTE_CLIENTS`, лимит инстансов, таймаут.
- Пути валидируются: локальные, без UNC/device/`..`.
- Аудит мутаций и `RunNow`.

Подробности — `./docs/contracts/SECURITY.md`.

## 9. Сборка и развёртывание

```powershell
./scripts/build.ps1
./scripts/test.ps1
./scripts/verify.ps1 -Scope integration
./scripts/installer/build.ps1
```

Раскладка:

```
%ProgramFiles%/WinSheduler/service/WinShedulerSvc.exe
%ProgramFiles%/WinSheduler/ui/WinSheduler.UI.exe
%ProgramData%/WinSheduler/data/scheduler.db
%ProgramData%/WinSheduler/logs/
```

Кастомные каталоги задаются свойствами MSI `INSTALLDIR` и `DATADIR`; установщик проверяет ACL родителей. Подробности — `./docs/contracts/INSTALL.md`.

## 10. Тестирование

- `./tests/scheduler` — триггеры, слоты, наложение, таймауты, retry, recovery; fake clock.
- `./tests/db` — схема, миграции, retention, ACL, бенчмарк.
- `./tests/ipc` — кадрирование, конверты, авторизация, лимиты, fuzz.
- `./tests/ui` — ViewModel, команды, конвертеры, IPC-клиент.
- `./tests/contract` — JSON-схемы, round-trip, эталонный `IpcFrame`.
- `./tests/e2e` — сценарии на чистой ВМ (каркас).

## 11. Ограничения v1

- Только headless-задачи; интерактивный режим зарезервирован (`session_mode = ActiveUser`).
- Подпись кода отложена; выпускается `SHA256SUMS.txt`.
- CI отсутствует; проверки — локальный `./scripts/verify.ps1`.
- MSI требует установленного WiX.
