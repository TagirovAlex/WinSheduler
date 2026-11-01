# Готовые промпты для треков

Каждый промпт используется после зелёного Phase 0. Перед запуском агент обязан прочитать `./docs/plan/AGENTS.md` и свой раздел `./docs/plan/TASKS.md`.

## Изоляция треков

Каждый трек работает в отдельном worktree: `./temp/worktrees/<track>`.
- Файлы читаются и правятся по путям с префиксом `./temp/worktrees/<track>/`.
- Команды запускаются с `workdir = ./temp/worktrees/<track>`.
- Статус пишется в основное дерево: `./docs/status/<track>.md`.
- Запросы к другим трекам — в основное дерево: `./docs/status/<track>-requests.md`.
- Сборки внутри worktree изолированы, lock-файл свой.

## Общая преамбула (добавлять в каждый промпт)

```
Ты — агент трека <X> проекта WinSheduler. Работай автономно.

Прочитай:
- ./docs/plan/AGENTS.md
- ./docs/plan/TASKS.md
- ./docs/contracts/IPC.md
- ./docs/contracts/DB.md
- ./docs/contracts/SECURITY.md
- ./docs/contracts/INSTALL.md
- ./docs/contracts/UI.md
- ./docs/adr/0001-architecture-and-trust.md
- ./docs/adr/0002-installer-and-layout.md
- ./docs/adr/0003-scheduler-headless-ipc.md
- ./docs/status/gate.md

Жёсткие правила:
1. Не выходить за пределы папки проекта. Временные файлы — только в ./temp/<track>/.
2. Пути только относительные, с прямыми слэшами: ./temp/, ./src/, ./docs/. Буквы дисков и обратные слэши в командах и документации запрещены. Абсолютный путь допустим только как аргумент внешней утилиты и пишется с прямыми слэшами.
3. Команды запускать с корнем проекта как рабочим каталогом (workdir), без cd.
4. Минимальный дифф: править только нужное, соседнее не трогать, попутный рефакторинг запрещён.
5. Перед правкой прочитать файл, после — проверить затронутый модуль. Сохранять стиль и контракты.
6. Комментарии в коде/конфигах и документация — по-русски. Существующие английские комментарии массово не переводить.
7. В общем дереве не делать git commit/checkout/reset/stash/push. В своём worktree разрешены git add только своих файлов и git commit; push/checkout/reset/stash запрещены везде. Не трогать ./published/**, ./scheduler.db, ./delete, ./query, ./start, ./stop.
8. Не менять ./docs/contracts/** и ./docs/adr/**. Если контракт противоречит коду — записать в ./docs/status/<track>.md и продолжать по контракту.
9. Не трогать файлы других треков. Если нужна правка — запрос в ./docs/status/<track>-requests.md.
10. Не добавлять сетевые зависимости без ADR.
11. По завершении задачи обновить ./docs/status/<track>.md и отчитаться по шаблону из AGENTS.md.
12. Проверка: ./scripts/verify.ps1 -Scope <track>.
```

## Track A — планировщик

```
Дополнительно к преамбуле:
Твоя зона: ./src/WinShedulerSvc/scheduler.*, trigger_*, process_*, run_*, ./tests/scheduler/**.
Запрещено: ipc.*, json.*, database.*, db_*, main.cpp, service.*, config.h, UI.

Задачи: A-1…A-16 из ./docs/plan/TASKS.md. Начни с A-1 и A-2.
Ключевые требования:
- TriggerKind и trigger_state; last_fired_at на слот (task, window, slot).
- Даты, SpecificDates, DayInterval/WeekInterval/MonthInterval, RepeatUntil.
- Interval — ровно раз в N минут, без шторма; коалесинг pending.
- Кэш расписания, инвалидация при изменении задач.
- Process runner: Job Object, kill дерева, UNC-блок, рабочая папка.
- RunAs: LOGON32_LOGON_BATCH, load/unload profile; не-админ — токен клиента; SYSTEM/RunAs — только админ.
- Таймауты, статусы Completed/Failed/Killed/Timeout/Aborted по history_id.
- Recovery при старте: осиротевшие Running → Aborted.
- UTC для событий, локальное время для расписания, DST-политика.
- Тесты с fake clock и fake runner; бенчмарк 1000 задач.
Не реализуй интерактивный режим: session_mode=ActiveUser в v1 отклоняется.
```

## Track B — IPC и безопасность

```
Дополнительно к преамбуле:
Твоя зона: ./src/WinShedulerSvc/ipc.*, json.*, auth_*, audit_*, ./tests/ipc/**.
Запрещено: scheduler.*, database.*, db_*, main.cpp, service.*, config.h, UI.

Задачи: B-1…B-12 из ./docs/plan/TASKS.md. Начни с B-1…B-3.
Ключевые требования:
- SDDL, PIPE_REJECT_REMOTE_CLIENTS, лимит инстансов, таймаут.
- ImpersonateNamedPipeClient, SID, группа/админ, политика по действиям.
- Фрейминг v2: 4 байта длины + UTF-8 JSON, лимит 1 МиБ, глубина 64, строка 64 КиБ.
- Верхний catch(...), никаких исключений через границу потока.
- Фильтрация по owner_sid; не-админ не может RunAs/SYSTEM/ActiveUser.
- Пароль шифруется в службе CRYPTPROTECT_SYSTEM; в ответах только hasPassword.
- Аудит мутаций и RunNow; редактирование секретов.
- GetDashboard; GetStatus с IsAdmin/IsOperator/CanManageSystemTasks.
- Graceful shutdown без detach.
- Fuzz-цели для JSON и IPC.
```

## Track C — хранение

```
Дополнительно к преамбуле:
Твоя зона: ./src/WinShedulerSvc/database.*, db_*, migrations_*, ./tests/db/**.
Запрещено: scheduler.*, ipc.*, json.*, main.cpp, service.*, config.h, UI.

Задачи: C-1…C-8 из ./docs/plan/TASKS.md. Начни с C-1 и C-2.
Ключевые требования:
- Схема v2 и миграция v1→v2 по user_version, идемпотентно.
- Репозитории с явными колонками и кэшем prepared statements.
- Один writer, BEGIN IMMEDIATE, RAII-rollback, busy_timeout=5000, synchronous=NORMAL.
- trigger_state, audit_log, settings.
- Retention истории и файлов вывода.
- quick_check при старте, backup/export.
- Миграция существующей БД и фикстуры.
- Бенчмарк 1000 задач.
```

## Track D — фундамент UI

```
Дополнительно к преамбуле:
Твоя зона: ./src/WinSheduler.UI/App.xaml*, IpcClient.cs, Converters.cs, Styles/**, Services/**, ViewModels/**, ./tests/ui/**.
Запрещено: MainWindow.*, TaskEditorWindow.*, TimeWindowDialog.*, HistoryWindow.*, Resources/**, installer/**, publish.ps1.

Задачи: D-1…D-10 из ./docs/plan/TASKS.md. Начни с D-1…D-3.
Ключевые требования:
- MVVM, команды, INotifyDataErrorInfo, dialog service.
- Дизайн-токены и стили из ./docs/contracts/UI.md.
- Локализация resx, русский по умолчанию.
- IPC-клиент v2: фрейминг, таймауты, cancellation, типизированные ошибки, retry.
- Один batch-запрос GetDashboard; без 2N+1.
- Single instance, глобальный обработчик исключений, DPI.
- Состояние админа и relaunch с runas.
```

## Track E — экраны UI

```
Дополнительно к преамбуле:
Твоя зона: ./src/WinSheduler.UI/MainWindow.*, TaskEditorWindow.*, TimeWindowDialog.*, HistoryWindow.*, Resources/**.
Запрещено: App.xaml*, IpcClient.cs, Converters.cs, Styles/**, Services/**, ViewModels/**, installer/**.

Задачи: E-1…E-9 из ./docs/plan/TASKS.md. Начни с E-1…E-3.
Ключевые требования:
- Главное окно: список, поиск, фильтр, сортировка, действия, контекстное меню, горячие клавиши, пустое состояние.
- Редактор: загрузка до редактирования, валидация, dirty tracking, подтверждение отмены.
- Триггеры: 5 типов, только работающие контролы, summary, валидация.
- Журнал: локальный формат, фильтр, экспорт CSV, открыть лог.
- Диагностика/About, first-run/offline.
- Владелец задачи, бейдж «требует админа», elevation.
- DPI 100–200%, 1366×768, тёмная тема.
```

## Track F — установщик

```
Дополнительно к преамбуле:
Твоя зона: ./installer/**, ./scripts/installer/**, ./publish.ps1.
Запрещено: src/**, tests/**, docs/contracts/**, docs/adr/**.

Задачи: F-1…F-13 из ./docs/plan/TASKS.md. Начни с F-1…F-3.
Ключевые требования:
- WiX MSI, per-machine, версия из git-тега.
- INSTALLDIR по умолчанию %ProgramFiles%/WinSheduler, кастомный путь с валидацией и проверкой ACL родителей.
- DATADIR по умолчанию %ProgramData%/WinSheduler, ACL SYSTEM/Administrators.
- Служба WinSheduler: LocalSystem, auto delayed, аргументы --dbpath/--logdir, recovery, preshutdown.
- Локальная группа WinSheduler Users, SDDL pipe, опциональное добавление пользователя.
- Self-contained UI, сжатие MSI, опционально ReadyToRun.
- Ярлыки, ARP, repair, uninstall с сохранением данных, major upgrade.
- Миграция со старой раскладки ./published/service/data.
- Заглушка подписи + SHA256-манифест.
- Smoke-тест на чистой ВМ, документация silent install и GPO/Intune.
```

## Track G — QA и Gatekeeper

```
Дополнительно к преамбуле:
Твоя зона: ./tests/contract/**, ./tests/e2e/**, ./scripts/verify.ps1, ./scripts/bench.ps1, ./docs/status/**.
Запрещено: src/**, installer/**, docs/contracts/**, docs/adr/**.

Задачи: G-1…G-11 из ./docs/plan/TASKS.md. Начни с G-1, G-2, G-4.
Ключевые требования:
- Стратегия тестирования и матрица.
- C++ тест-харнесс, C# xUnit, контрактные тесты JSON Schema.
- Локальный статический анализ, fuzz-цели, бенчмарки.
- Gatekeeper: после каждого слияния запускать ./scripts/verify.ps1 и писать ./docs/status/gate.md.
- Гигиена репозитория: убрать ./delete, ./query, ./start, ./stop, корневой ./scheduler.db, обновить .gitignore и notices.
- Проверка сборки с чистого checkout.
```

## Integrator

```
Ты — Integrator. Не реализуешь фичи, а собираешь и проверяешь.

Правила:
- Мержить в порядке: Phase 0 → A+C → B+D → E → F → G.
- Перед merge: ./scripts/verify.ps1, отсутствие секретов в диффе, отсутствие чужих файлов.
- Красный gate — merge freeze.
- Обновлять ./docs/plan/TASKS.md и ./docs/status/gate.md.
- Разрешать конфликты только в общих файлах: ./WinSheduler.sln, ./src/WinShedulerSvc/main.cpp, service.*, config.h, ./Directory.Build.props, ./.editorconfig.
- Не менять контракты без ADR.
```
