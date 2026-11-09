# WinSheduler

Собственный планировщик задач для Windows: привилегированная служба, WPF-интерфейс и локальный IPC. Не зависит от встроенного Task Scheduler.

Текущая версия — **2.1.2**. Режим выполнения задач — **headless** (без видимого окна); интерактивный режим зарезервирован на будущее.

## Возможности

- 5 типов триггеров: однократно, ежедневно, еженедельно, ежемесячно, интервал.
- Точные времена, интервалы N дней/недель/месяцев, диапазоны дат, конкретные даты, повтор до указанного момента.
- Планировщик с состоянием слотов: задача не запускается дважды в одну минуту.
- Политики наложения: ждать завершения, пропустить, запустить параллельно.
- Таймауты, завершение дерева процессов, повторы при ошибке.
- Запуск от имени пользователя (только администратор) или от имени вызывающего пользователя.
- Журнал запусков, аудит действий, ротация истории и файлов вывода.
- SQLite (WAL), миграция схемы v1 → v2.
- WPF-интерфейс: поиск, фильтры, сортировка, горячие клавиши, локальные даты, диагностика.
- Экспорт и импорт задач (одна или несколько выбранных) в JSON без секретов.
- Автообновление статуса после запуска: завершение и ошибка видны без F5.
- Локальная группа `WinSheduler Users` для доступа к IPC без прав администратора.
- Установщик WiX MSI с поддержкой кастомных каталогов и ACL.

## Архитектура

```
WinSheduler.UI.exe (WPF, .NET 10, self-contained)
  │  named pipe //./pipe/WinSheduler, IPC v1
  │  4 байта длины LE + UTF-8 JSON
  ▼
WinShedulerSvc.exe (C++ Win32, /MT)
  ├─ планировщик: trigger_engine, trigger_state, process_runner
  ├─ IPC: ipc_frame, auth_policy, auth_token, audit_log
  ├─ SQLite: migrations, db_repository, db_stmt
  └─ процессы задач (headless)
```

- Служба работает от `LocalSystem` в session 0.
- UI работает с правами обычного пользователя (`asInvoker`).
- Обмен — только локальный именованный канал; удалённые клиенты отклоняются.
- Данные и логи — в `%ProgramData%/WinSheduler`, программа — в `%ProgramFiles%/WinSheduler` (по умолчанию).

## Модель доступа

| Операция | `WinSheduler Users` | Администратор |
|----------|---------------------|---------------|
| Подключение к IPC | да | да |
| Свои задачи: просмотр, создание, изменение, удаление, запуск | да | да |
| Чужие задачи | нет | да |
| Запуск от `SYSTEM` или другого пользователя | нет | да |
| Аудит, настройки, управление службой | нет | да |

Не-администратор запускает задачи **токеном своего пользователя**, поэтому членство в группе не повышает привилегии. Администратор без elevation обрабатывается как оператор; для привилегированных действий UI перезапускается через `runas`.

## Требования

- Windows 10 1607+ / Windows Server 2016+ (x64).
- UI требует Desktop Experience и интерактивной сессии; служба работает и на Server Core.
- Для сборки: Visual Studio 2022/2026 Build Tools (MSVC v143/v145), .NET 10 SDK.
- Для MSI: WiX Toolset 5 (`dotnet tool install --global wix`).
- Для запуска UI: .NET Runtime не требуется (self-contained).

## Сборка

Все команды — из корня проекта.

### Дизайн формы в Visual Studio

UI-проект multi-target: `net8.0-windows` (для XAML-дизайнера VS 2022)
и `net10.0-windows` (релиз). VS 2022 17.14 не поддерживает дизайнер для
`net10.0`; VS 2026 поддерживает обе цели.

1. Откройте `./WinSheduler.Design.slnf` — в нём только UI и Shared.
2. Назначьте `WinSheduler.UI` стартовым проектом (ПКМ → Set as Startup Project).
3. В свойствах `WinSheduler.UI` выберите Target Framework: `net8.0-windows`
   для VS 2022 или `net10.0-windows` для VS 2026.
4. Соберите проект (`Build → Build WinSheduler.UI`).
5. Откройте `./src/WinSheduler.UI/MainWindow.xaml`. Если открылся XML:
   ПКМ → Open With… → XAML Designer → Set as Default.
6. Если дизайнер выключен: Tools → Options → XAML Designer → General →
   Enable XAML Designer, перезапустить VS.

Релизная сборка и публикация — `net10.0-windows`:
`dotnet publish ... -f net10.0-windows`.

```powershell
# Сборка решения
./scripts/build.ps1

# Тесты
./scripts/test.ps1

# Полный локальный gate: сборка, тесты, формат, контракты, схемы, smoke
./scripts/verify.ps1 -Scope integration

# Финальный комплект: служба + self-contained UI + MSI + SHA256 в ./published
./publish.ps1

# Промежуточная сборка без MSI
./scripts/installer/build.ps1 -SkipMsi -OutputDir published
```

Отдельные тесты:

```powershell
./tests/scheduler/run.ps1
./tests/db/run.ps1
./tests/ipc/run.ps1
dotnet test ./tests/ui/WinSheduler.UI.Tests.csproj -c Release
```

## Установка

```bat
msiexec /i ./published/WinSheduler.msi /qn
```

Кастомные каталоги:

```bat
msiexec /i ./published/WinSheduler.msi INSTALLDIR="d:/Apps/WinSheduler" DATADIR="d:/WinShedulerData" /qn
```

Свойства MSI: `INSTALLDIR`, `DATADIR`, `OLDDATADIR`, `CREATEGROUP`, `ADDINSTALLERTOGROUP`, `KEEPDATA`, `ALLOWUNSAFEPATH`, `ALLOWUNSUPPORTEDOS`.

Установщик:

- создаёт службу `WinSheduler` (auto delayed, recovery, preshutdown);
- создаёт локальную группу `WinSheduler Users`;
- выставляет ACL на программу и данные;
- переносит старую БД из `./published/service/data`, если она найдена;
- сохраняет данные при удалении по умолчанию.

Удаление:

```bat
msiexec /x ./published/WinSheduler.msi /qn
```

Старый `./deploy.bat` выведен из production-процесса: он регистрирует службу из каталога, доступного на запись пользователям.

## Данные и логи

```
%ProgramFiles%/WinSheduler/service/WinShedulerSvc.exe
%ProgramFiles%/WinSheduler/ui/WinSheduler.UI.exe
%ProgramData%/WinSheduler/data/scheduler.db
%ProgramData%/WinSheduler/logs/scheduler.log
%ProgramData%/WinSheduler/logs/output/<taskId>/<historyId>.log
```

Пароли учётных записей шифруются в службе через DPAPI `CRYPTPROTECT_SYSTEM`; UI никогда не получает зашифрованный blob и не шифрует пароль сам.

## IPC

Формат: 4 байта длины (little-endian) + UTF-8 JSON.

```json
{
  "v": 1,
  "id": "8f1c2c1e-4a0b-4c1e-9a6e-1f2d3c4b5a60",
  "action": "GetDashboard",
  "payload": {}
}
```

Ответ:

```json
{
  "v": 1,
  "id": "8f1c2c1e-4a0b-4c1e-9a6e-1f2d3c4b5a60",
  "ok": true,
  "error": null,
  "payload": {}
}
```

Основные действия: `Ping`, `GetStatus`, `GetDashboard`, `ListTasks`, `GetTask`, `GetTimeWindows`, `GetHistory`, `CreateTask`, `UpdateTask`, `DeleteTask`, `SetTaskEnabled`, `RunNow`, `GetAudit`, `GetSettings`, `SetSettings`.

Полный контракт — `./docs/contracts/IPC.md`.

## Структура проекта

```
./src/WinShedulerSvc/     C++ служба: планировщик, IPC, SQLite
./src/WinSheduler.UI/     WPF-интерфейс
./src/WinSheduler.Shared/ общие модели
./installer/              WiX MSI и нативные действия установщика
./scripts/                сборка, тесты, gate, установщик
./tests/                  scheduler, db, ipc, ui, contract, e2e
./docs/contracts/         IPC, DB, безопасность, установка, UI
./docs/plan/              план работ, правила агентов, риски, вопросы
./docs/status/            статусы треков и gate
```

## Документация

- `./docs/contracts/IPC.md` — протокол.
- `./docs/contracts/DB.md` — схема и миграции.
- `./docs/contracts/SECURITY.md` — модель доверия и права.
- `./docs/contracts/INSTALL.md` — установка и ACL.
- `./docs/contracts/UI.md` — дизайн-система и экраны.
- `./docs/plan/TASKS.md` — доска задач.
- `./docs/plan/AGENTS.md` — правила работы агентов.
- `./docs/status/gate.md` — результаты локального gate.
- `./docs/adr/` — принятые решения.
- `./CHANGELOG.md` — история изменений.
- `./docs/releases/v2.1.2.md` — заметки релиза 2.1.2.

## Ограничения v1

- Только headless-задачи: окно процесса не отображается на рабочем столе пользователя.
- Интерактивный режим (`session_mode = ActiveUser`) зарезервирован, но не реализован.
- MSI собирается только при установленном WiX; без него доступна публикация бинарников.
- Подпись кода отложена: выпускается `SHA256SUMS.txt`.
- CI отсутствует: проверки выполняются локально через `./scripts/verify.ps1`.

## Лицензия

MIT.
