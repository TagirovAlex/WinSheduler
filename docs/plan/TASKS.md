# План работ WinSheduler

Доска задач для роя агентов. Правила работы — в `./docs/plan/AGENTS.md`.

## Статусы

| Статус | Значение |
|--------|----------|
| `[ ]` | не начата |
| `[~]` | в работе |
| `[x]` | выполнена |
| `[!]` | заблокирована |
| `[-]` | отменена |

Доску правит только Integrator. Агенты пишут прогресс в `./docs/status/<track>.md`.

## Зафиксированные решения

| № | Решение |
|---|---------|
| 1 | Группа `WinSheduler Users` без админ-прав: только свои задачи, запуск от своего имени. SYSTEM и RunAs — только администраторы. |
| 2 | Установка по умолчанию в `%ProgramFiles%/WinSheduler`, но `INSTALLDIR` можно указать. Путь валидируется, ACL родителей проверяется. |
| 3 | Данные — отдельный `DATADIR`, по умолчанию `%ProgramData%/WinSheduler`. |
| 4 | Установщик — WiX MSI. |
| 5 | UI публикуется self-contained, prerequisite .NET не требуется. |
| 6 | Минимальная ОС — Windows 10 1607+ / Server 2016+ (build 14393), ADR 0004. |
| 7 | Язык UI — русский, строки через `resx`. |
| 8 | Сертификата подписи нет: шаг подписи — заглушка, выпускается SHA256-манифест. |
| 9 | CI нет: локальный `./scripts/verify.ps1` и агент Gatekeeper. |
| 10 | Автоматическая миграция существующей БД. |
| 11 | Максимальная параллельность после Phase 0; у каждого трека свои файлы. |
| 12 | Только headless-задачи. Поле `session_mode` резервируется под будущий интерактивный режим. |

> Решение 6 обновлено по ADR 0004 (`./docs/adr/0004-net10-server2016.md`).
> Детали плана — в `./docs/plan/ROADMAP-2.1.md`.

## Phase 0 — контракты и швы

| ID | Задача | Зависит | Владелец | Статус |
|----|--------|---------|----------|--------|
| P0-1 | `./docs/contracts/IPC.md` | — | Architect | `[x]` |
| P0-2 | `./docs/contracts/DB.md` | — | Architect | `[x]` |
| P0-3 | `./docs/contracts/SECURITY.md` | — | Architect | `[x]` |
| P0-4 | `./docs/contracts/INSTALL.md` | — | Architect | `[x]` |
| P0-5 | `./docs/contracts/UI.md` | — | Architect | `[x]` |
| P0-6 | Каркас: `Directory.Build.props`, `.editorconfig`, `app.manifest`, wildcard-включения в `vcxproj`, новые поля в общих моделях и `config.h`, `./temp/`, тестовый проект C# | P0-1…5 | Integrator | `[x]` |
| P0-7 | Локальный gate: `./scripts/build.ps1`, `test.ps1`, `format.ps1`, `verify.ps1`, `bench.ps1`, lock сборки, `./docs/status/gate.md` | P0-6 | Track G | `[x]` |
| P0-8 | Скелет контрактных тестов: JSON-схемы, round-trip IPC, фикстура миграции БД | P0-6 | Track G | `[x]` |
| P0-9 | Базовая сборка зелёная, `verify.ps1` проходит | P0-6, P0-7 | Integrator | `[x]` |

## Track A — планировщик

Зона: `./src/WinShedulerSvc/scheduler.*`, `trigger_*`, `process_*`, `run_*`, `./tests/scheduler/**`.

| ID | Задача | Зависит | Статус |
|----|--------|---------|--------|
| A-1 | Модель триггера v2: `TriggerKind` (Once/Daily/Weekly/Monthly/Interval), типизированные поля, валидация, маппинг в БД | P0-2, P0-6 | `[x]` |
| A-2 | Чистый `TriggerEngine`: `is_due`, `next_run`, перечисление слотов; инъекция часов | A-1 | `[x]` |
| A-3 | Диапазоны дат, `SpecificDates`, `DayInterval`/`WeekInterval`/`MonthInterval`, `RepeatUntil` (дата+время) | A-2 | `[x]` |
| A-4 | Interval: ровно один запуск на интервал, без шторма раз в секунду; коалесинг очереди | A-2 | `[x]` |
| A-5 | `last_fired_at` для пары (задача, триггер, слот), идемпотентность в минуту, multi-slot | A-2, C-2 | `[x]` |
| A-6 | Кэш расписания в памяти + API инвалидации | A-5, C-2 | `[x]` |
| A-7 | Process runner: Job Object, stdout/stderr, рабочая папка, блок UNC, window style, env | P0-6 | `[x]` |
| A-8 | RunAs: `LOGON32_LOGON_BATCH`, load/unload profile, токен клиента для не-админа, SYSTEM/RunAs только для админа | A-7, P0-3 | `[x]` |
| A-9 | Политики наложения + `RunNow` через единый API; привязка к `history_id` | A-7, B-5 | `[x]` |
| A-10 | Таймауты, kill дерева процессов, статусы Completed/Failed/Killed/Timeout/Aborted по `history_id` | A-9 | `[x]` |
| A-11 | Retry/OnError с backoff и лимитом попыток | A-10 | `[x]` |
| A-12 | Recovery при старте: осиротевшие `Running` → `Aborted`; опциональный перезапуск | A-10, C-2 | `[x]` |
| A-13 | UTC/таймзоны/DST, окно catch-up, смена системного времени | A-2 | `[x]` |
| A-14 | Тесты: табличные триггеры, fake clock, fake runner; бенчмарк 1000 задач | A-2…A-13 | `[x]` |
| A-15 | Производительность: убрать N+1, замерить тик, бюджет < 50 мс на 1000 задач | A-6, A-14 | `[x]` |
| A-16 | Резерв: `session_mode` в модели и API, реализация `ActiveUser` не входит в v1 | A-1, P0-2 | `[x]` |

## Track B — IPC и безопасность

Зона: `./src/WinShedulerSvc/ipc.*`, `json.*`, `auth_*`, `audit_*`, `./tests/ipc/**`.

| ID | Задача | Зависит | Статус |
|----|--------|---------|--------|
| B-1 | Pipe: явный SDDL, `PIPE_REJECT_REMOTE_CLIENTS`, лимит инстансов, таймаут соединения | P0-3 | `[x]` |
| B-2 | Аутентификация: impersonate, проверка группы/админа, SID, политика по действиям | B-1, P0-3 | `[x]` |
| B-3 | Фрейминг v2: length-prefix, лимит кадра, чанкинг больших ответов | P0-1 | `[x]` |
| B-4 | Харденинг: верхний `catch(...)`, лимит глубины JSON, fuzz, никаких исключений через границу потока | B-3 | `[x]` |
| B-5 | Обработчики: CRUD/toggle/RunNow с фильтрацией по владельцу и валидацией (GUID, пути, enum, время) | B-2, P0-1 | `[x]` |
| B-6 | Секреты: шифрование в службе `CRYPTPROTECT_SYSTEM`, никогда не отдавать blob, `HasPassword` | B-5, P0-3 | `[x]` |
| B-7 | Аудит: SID, действие, объект, результат; редактирование секретов | B-5 | `[x]` |
| B-8 | Batch-метод `GetDashboard` | B-5 | `[x]` |
| B-9 | Graceful shutdown: прекратить accept, join, drain, без `detach` | B-1 | `[x]` |
| B-10 | Контрактные тесты IPC + fuzz-харнесс | B-3…B-8 | `[x]` |
| B-11 | `IAuthPolicy`: группа/админ, владелец, разрешённые RunAs, аудит | B-2, P0-3 | `[x]` |
| B-12 | `GetStatus` возвращает `IsAdmin`, `IsOperator`, `CanManageSystemTasks` | B-5, B-11 | `[x]` |

## Track C — хранение

Зона: `./src/WinShedulerSvc/database.*`, `db_*`, `migrations_*`, `./tests/db/**`.

| ID | Задача | Зависит | Статус |
|----|--------|---------|--------|
| C-1 | Схема v2 + миграции по `user_version`, идемпотентные | P0-2 | `[x]` |
| C-2 | Репозитории: tasks/windows/history/settings/audit; явные колонки; кэш prepared statements | C-1 | `[x]` |
| C-3 | Один writer (очередь) или мьютекс + `BEGIN IMMEDIATE` + RAII-rollback | C-2 | `[x]` |
| C-4 | Прагмы: `busy_timeout`, `synchronous`, WAL checkpoint, integrity check | C-1 | `[x]` |
| C-5 | Retention: история по задаче + файлы вывода | C-2 | `[x]` |
| C-6 | Валидация пути/ACL БД, `sqlite3_close_v2`, backup/export | C-1 | `[x]` |
| C-7 | Миграция существующей БД v1 + фикстуры | C-1 | `[x]` |
| C-8 | Индексы, планы запросов, бенчмарк 1000 задач | C-2 | `[x]` |

## Track D — фундамент UI

Зона: `./src/WinSheduler.UI/App.xaml*`, `IpcClient.cs`, `Converters.cs`, `Styles/**`, `Services/**`, `ViewModels/**`, `./tests/ui/**`.

| ID | Задача | Зависит | Статус |
|----|--------|---------|--------|
| D-1 | MVVM: `ViewModelBase`, команды, `INotifyDataErrorInfo`, dialog service | P0-6 | `[x]` |
| D-2 | Дизайн-система: токены, стили контролов, иконочный шрифт, light/dark | P0-5 | `[x]` |
| D-3 | Локализация: `resx` + markup extension + русские строки | P0-5 | `[x]` |
| D-4 | IPC-клиент v2: одно соединение/пул, фрейминг, таймауты, cancellation, типизированные ошибки, retry | P0-1, B-3 | `[x]` |
| D-5 | Host: single instance, глобальный обработчик исключений, логирование, DPI | P0-6 | `[x]` |
| D-6 | Конвертеры: дата/время, enum, цвет статуса, bool, null | P0-5 | `[x]` |
| D-7 | Batch-обновление + прогресс + отмена + защита от re-entry | D-4, B-8 | `[x]` |
| D-8 | Настройки окна (размер/позиция, последний выбор) | D-1 | `[x]` |
| D-9 | Доступность и карта клавиатуры | D-2 | `[x]` |
| D-10 | Состояние админа, relaunch с `runas`, скрытие привилегированных действий | D-4, B-12 | `[x]` |

## Track E — экраны UI

Зона: `./src/WinSheduler.UI/MainWindow.*`, `TaskEditorWindow.*`, `TimeWindowDialog.*`, `HistoryWindow.*`, `Resources/**`.

| ID | Задача | Зависит | Статус |
|----|--------|---------|--------|
| E-1 | MainWindow: список, поиск/фильтр/сортировка, вкл/выкл, RunNow, открыть лог, контекстное меню, горячие клавиши, пустое состояние | D-1…D-7 | `[x]` |
| E-2 | TaskEditor: вкладки, загрузка до редактирования, валидация, dirty tracking, подтверждение отмены | D-1…D-3 | `[x]` |
| E-3 | TriggerEditor: 5 типов, валидация, summary, без мёртвых контролов, дублирование | D-1…D-3 | `[x]` |
| E-4 | History: локальный формат, фильтр, обновление, экспорт CSV, открыть лог, тултип ошибки | D-1…D-6 | `[x]` |
| E-5 | Diagnostics/About: версия, uptime, пути, состояние подключения, аудит (админ) | D-4, B-7 | `[x]` |
| E-6 | First-run/offline: служба недоступна, инструкции, retry | D-4 | `[x]` |
| E-7 | UI-тесты: базовый UIAutomation smoke есть, FlaUI — по желанию | E-1…E-4 | `[~]` |
| E-8 | Полировка: DPI 100–200%, 1366×768, тёмная тема | E-1…E-6 | `[~]` |
| E-9 | Колонка «Владелец», бейдж «требует админа», кнопка elevation | D-10, B-12 | `[x]` |

## Track F — установщик и интеграция

Зона: `./installer/**`, `./scripts/installer/**`, `./publish.ps1`.

| ID | Задача | Зависит | Статус |
|----|--------|---------|--------|
| F-1 | WiX-проект, версия из git-тега, скелет MSI | P0-4 | `[x]` |
| F-2 | `INSTALLDIR`: кастомный путь + валидация (локальный фиксированный диск, ACL родителей, запрет UNC/корня/user-writable) | F-1, P0-4 | `[x]` |
| F-3 | `DATADIR` (по умолчанию ProgramData) + ACL-матрица | F-1, P0-4 | `[x]` |
| F-4 | Служба: auto delayed, LocalSystem, аргументы, recovery, preshutdown, start/stop/remove | F-3 | `[x]` |
| F-5 | Локальная группа + SDDL pipe + опциональное добавление пользователя | F-4, B-1 | `[x]` |
| F-6 | Self-contained publish + сжатие MSI + опционально ReadyToRun | F-1 | `[x]` |
| F-7 | Ярлыки, ARP, repair, uninstall с сохранением данных | F-4 | `[x]` |
| F-8 | Major upgrade, блокировка downgrade, сохранение данных | F-7 | `[x]` |
| F-9 | Миграция со старой раскладки (`./published/service/data`) | F-3, C-7 | `[x]` |
| F-10 | Заглушка подписи + SHA256-манифест + документация | F-1 | `[x]` |
| F-11 | Smoke-тест установки на чистой ВМ | F-1…F-10 | `[~]` |
| F-12 | Документация: silent install, свойства, GPO/Intune, админ-гайд | F-1…F-11 | `[x]` |
| F-13 | Бюджет размера MSI и времени старта UI | F-6 | `[x]` |

## Track G — QA, gate, документация

Зона: `./tests/contract/**`, `./tests/e2e/**`, `./scripts/verify.ps1`, `./scripts/bench.ps1`, `./docs/status/**`.

| ID | Задача | Зависит | Статус |
|----|--------|---------|--------|
| G-1 | Стратегия тестирования и матрица | P0-1…5 | `[x]` |
| G-2 | C++ тест-харнесс + gate | P0-6 | `[x]` |
| G-3 | C# xUnit тесты для VM/сервисов | P0-6 | `[x]` |
| G-4 | Контрактные тесты (JSON Schema с обеих сторон) | P0-8 | `[x]` |
| G-5 | Локальный статический анализ: MSVC `/analyze`, clang-tidy, .NET analyzers | P0-6 | `[~]` |
| G-6 | Fuzz-цели для JSON/IPC | B-4 | `[~]` |
| G-7 | Локальные бенчмарки (тик, refresh, память) | A-15, C-8, D-7 | `[~]` |
| G-8 | Документация: README/SPEC, `SECURITY.md`, CHANGELOG, ADR | все | `[~]` |
| G-9 | Гигиена репозитория: убрать `./delete`, `./query`, `./start`, `./stop`, корневой `./scheduler.db`, обновить `.gitignore`, notices | P0-6 | `[x]` |
| G-10 | Проверка сборки с чистого checkout (без CI) | P0-7 | `[x]` |
| G-11 | Gatekeeper: прогон gate после каждого слияния, отчёт в `./docs/status/gate.md` | P0-7 | `[x]` |

## Интеграция и релиз

| ID | Задача | Зависит | Статус |
|----|--------|---------|--------|
| I-1 | Слить Phase 0, gate зелёный | P0-9 | `[x]` |
| I-2 | A+C: планировщик + БД, контрактные тесты | A-1…A-15, C-1…C-8 | `[x]` |
| I-3 | B+D: протокол IPC, round-trip тесты | B-1…B-12, D-4, D-7 | `[x]` |
| I-4 | E: экраны | E-1…E-9 | `[x]` |
| I-5 | F: установщик + аргументы службы + группа | F-1…F-13 | `[x]` |
| I-6 | E2E на чистых Win10/Win11/Server, security review, perf sign-off | I-2…I-5 | `[~]` |
| I-7 | RC: тег `v2.0.2` создан; подпись и release notes — по готовности | I-6 | `[~]` |
| I-8 | Релиз 2.1: .NET 10, Server 2016, экспорт/импорт задач, живой статус; тег `v2.1.0` | H-8, X-7, Y-6 | `[x]` |

## Phase 3 — платформа 2.1 (Server 2016, .NET 10, обмен задачами)

Детальный план: `./docs/plan/ROADMAP-2.1.md`. Изменения контрактов и
минимальной ОС — только после ADR H-1 и X-1.

### Track H — платформа

Зона: `./global.json`, `*.csproj`, `./scripts/build.ps1`, `./scripts/test.ps1`,
`./installer/**`, `./src/WinShedulerSvc/WinShedulerSvc.vcxproj`, документация.

| ID | Задача | Зависит | Владелец | Статус |
|----|--------|---------|----------|--------|
| H-1 | ADR: .NET 10 LTS и минимальная ОС Server 2016 (build 14393) | — | Architect | `[x]` |
| H-2 | `./global.json` (SDK 10.0.4xx), TFM: UI `net10.0-windows`, Shared/тесты `net10.0`; версия 2.1.0 в `Directory.Build.props` | H-1 | Integrator | `[x]` |
| H-3 | Сборка: managed — `dotnet build`, native — MSBuild; убрать хардкод `net8.0` из `test.ps1` и `ui-smoke.ps1`; обновить тестовые пакеты | H-2 | Track G | `[x]` |
| H-4 | Установщик: порог ОС `17763` → `14393`, сообщение; `_WIN32_WINNT=0x0601` в оба vcxproj | H-1 | Track F | `[x]` |
| H-5 | Проверить UI на .NET 10: WPF, `OpenFolderDialog`, `PerMonitorV2`/`activeCodePage` на 1607 | H-2 | Track D | `[x]` |
| H-6 | Публикация self-contained win-x64, опционально ReadyToRun; версия в `publish.ps1`; бюджет MSI/старта | H-2, H-4 | Track F | `[x]` |
| H-7 | Документация: README, SPECIFICATION, INSTALL, OPEN_QUESTIONS, RISKS | H-1 | Track G | `[x]` |
| H-8 | Gate: build/test/verify/MSI + smoke на Server 2016 Desktop Experience и Server Core | H-2…H-7 | Track G | `[~]` |

### Track X — экспорт и импорт задач

Зона: `./src/WinSheduler.Shared/Models/**`, `./src/WinSheduler.UI/Services/**`,
`./src/WinSheduler.UI/ViewModels/**`, `./src/WinSheduler.UI/MainWindow.*`,
`./docs/contracts/UI.md`, `./docs/contracts/EXCHANGE.md`, `./tests/**`.

| ID | Задача | Зависит | Владелец | Статус |
|----|--------|---------|----------|--------|
| X-1 | ADR и контракт `EXCHANGE.md`: формат JSON v1, лимиты, секреты, владелец | — | Architect | `[x]` |
| X-2 | C#-модели обмена в `WinSheduler.Shared`: документ, опции, отчёт импорта; camelCase | X-1 | Architect | `[x]` |
| X-3 | Экспорт в UI: `GetTask` для выбранных, сборка документа, `SaveFileDialog`, без секретов | X-2 | Track D | `[x]` |
| X-4 | Импорт в UI: чтение/валидация, предпросмотр, режимы copy/skip/overwrite, отчёт | X-2 | Track D | `[x]` |
| X-5 | UI: multi-select `DataGrid`, команды «Экспорт…»/«Импорт…», строки `resx` | X-3, X-4 | Track E | `[x]` |
| X-6 | Безопасность: секреты не экспортируются; `hasPassword` без пароля → задача выключена; чужие задачи не перезаписываются | X-1, X-4 | Track B | `[x]` |
| X-7 | Тесты: round-trip, отсутствие секретов, версия, битый JSON, лимиты, конфликты, права, UI | X-1…X-6 | Track G | `[x]` |
| X-8 | Документация: `UI.md`, `SECURITY.md`, README, сценарий эксплуатации | X-5 | Track G | `[x]` |

### Track Y — живой статус выполнения

Зона: `./src/WinSheduler.UI/ViewModels/**`, `./src/WinSheduler.UI/MainWindow.*`,
`./src/WinSheduler.UI/Localization/Strings.resx`, `./tests/ui/**`.

| ID | Задача | Зависит | Владелец | Статус |
|----|--------|---------|----------|--------|
| Y-1 | VM: тихое обновление строки без `IsLoading` и без мигания баннера | — | Track D | `[x]` |
| Y-2 | VM: цикл сопровождения запущенных задач через `GetHistory(taskId, 1)`; 2→5 с, grace 5 с, лимит 24 ч, отмена | Y-1 | Track D | `[x]` |
| Y-3 | Строка списка: `LastRun.Status`, приоритет Running > ошибка > Enabled/Disabled | Y-2 | Track D | `[x]` |
| Y-4 | UI: колонки «Статус»/«Последний запуск» с результатом, баннер завершения/ошибки | Y-3 | Track E | `[x]` |
| Y-5 | Главное окно: запуск сопровождения после `RunNow` и при `running=true`; отмена при закрытии | Y-2 | Track E | `[x]` |
| Y-6 | Тесты: fake IPC (Running→Completed/Failed), гонка старта, отмена, несколько задач, `UNAVAILABLE` | Y-1…Y-5 | Track G | `[x]` |
| Y-7 | Документация `UI.md`: автообновление, интервалы, ограничения | Y-4 | Track G | `[x]` |

## Волны

| Волна | Состав | Условие старта |
|-------|--------|----------------|
| 0 | Architect (контракты), Integrator + Track G (каркас и gate) | — |
| 1 | A, B, C, D, E, F, G — параллельно | P0-9 зелёный |
| 2 | Продолжение A…G + Integrator (слияния A+C, B+D) | контрактные тесты треков |
| 3 | E2E, установщик, релиз | I-2…I-5 |
| 4 | Платформа 2.1: H, X, Y — параллельно после H-2 | H-2, X-1, Y-1 |

## Критический путь

`P0-6 → P0-7 → P0-9 → C-1 → A-1 → A-2 → A-3/A-4/A-5 → A-9 → A-10 → I-2 → I-6 → I-7`

2.1: `H-1 → H-2 → H-4 → H-8`, `X-1 → X-2 → X-4 → X-7`, `Y-1 → Y-2 → Y-6` → `I-8`.

## Журнал изменений

| Дата | Изменение |
|------|-----------|
| 2026-11-01 | Создан план, зафиксированы решения 1–12 |
| 2026-11-01 | Phase 0 завершён, gate зелёный, коммит `6dfbf21` |
| 2026-11-01 | Треки A–G завершены и влиты: C, D, A, B, E, F, G; полный gate зелёный |
| 2026-11-01 | Исправлены UI-стили (`Border_Width`, видимость Tokens в Controls) и проверка gate для контрактных тестов C# |
| 2026-11-06 | Добавлен план 2.1: .NET 10, Server 2016, экспорт/импорт задач, живой статус выполнения (`./docs/plan/ROADMAP-2.1.md`) |
| 2026-11-06 | План 2.1 реализован: .NET 10, порог ОС 14393, экспорт/импорт, живой статус, иконки; gate зелёный, MSI 2.1.0 — 49,8 МБ. `./publish.ps1` собрал финальный комплект в `./published` (SHA256 `57CDFCB4…062A`). Тег `v2.1.0` и GitHub Release опубликованы; остался smoke на Server 2016 |
| 2026-11-07 | Релиз 2.1.1: косметический UI — меню, панель действий, `StatusBar`, поиск/фильтр слева, адаптивная ширина и таблица, исправления XAML-дизайнера. `./publish.ps1` — MSI 49,8 МБ, SHA256 `F22A694F…50E7` |
| 2026-11-09 | Исправлено планирование после импорта: IPC уведомляет `Scheduler::on_task_changed`, кэш расписания сбрасывается при create/update/delete/enable. Добавлен тест `on_task_changed`; scheduler-тесты — 184 |
