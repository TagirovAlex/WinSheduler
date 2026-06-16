# WinSheduler

**Windows Scheduler** — собственная служба планирования задач для Windows, полностью независимая от встроенного Task Scheduler.

> ⚠️ Встроенный планировщик Windows (Task Scheduler) катастрофически ненадёжен. WinSheduler реализует свой движок планирования, гарантирующий предсказуемое выполнение задач по расписанию.

---

## Возможности

- ✅ Независимый движок планирования — никаких зависимостей от Task Scheduler
- ✅ Неограниченное количество задач с произвольным числом триггеров
- ✅ 5 типов триггеров: Однократно, Ежедневно, Еженедельно, Ежемесячно, Интервал
- ✅ Точное время запуска — список конкретных моментов (09:00, 13:00, 18:00)
- ✅ Запуск задач под указанным пользователем (логин/пароль)
- ✅ Шифрование учётных данных через DPAPI (ключ привязан к машине)
- ✅ SQLite — встраиваемая БД, без серверов
- ✅ WPF-интерфейс с вкладками: Основные / Расписание / Параметры
- ✅ Иконки в ресурсах приложения (Segoe MDL2 Assets)
- ✅ Детальный журнал запусков с ротацией
- ✅ Индивидуальные настройки: таймаут, поведение при ошибках, логирование
- ✅ Windows-служба (SYSTEM) или standalone режим

---

## Архитектура

```
┌──────────────────────────────────────────────┐
│  WinSheduler.UI (C# WPF, .NET 8)             │
│  GUI с вкладками, иконки, триггеры            │
│  Требует .NET 8 Desktop Runtime              │
│       ↕ Named Pipes (\\.\pipe\WinSheduler)   │
├──────────────────────────────────────────────┤
│  WinShedulerSvc.exe (C++ Win32, /MT)         │
│  Планировщик, ~1.5 MB, без .NET Runtime      │
│  1-секундный тик, проверка всех задач        │
│       ↕                                       │
│  SQLite (data\scheduler.db, WAL journal)     │
│       ↕                                       │
│  DPAPI (CryptProtectData / LocalMachine)     │
│       ↕                                       │
│  Запущенные процессы (CreateProcessW)        │
└──────────────────────────────────────────────┘
```

---

## Модель данных

### Task (Задача)

| Поле | Тип | Описание |
|------|-----|----------|
| Id | `Guid` | Уникальный идентификатор |
| Name | `string` | Название задачи |
| Description | `string` | Описание |
| Enabled | `bool` | Включена / отключена |
| ProgramPath | `string` | Путь к исполняемому файлу |
| Arguments | `string` | Аргументы командной строки |
| WorkingDirectory | `string` | Рабочая папка |
| WindowStyle | `WindowStyle` | Normal / Hidden / Minimized / Maximized |
| RunAsUser | `RunAsCredentials` | Учётные данные для запуска |
| OnError | `OnErrorAction` | SkipNext / Retry / Fail |
| OnOverlap | `OnOverlapAction` | WaitComplete / WaitNext / StartAnother |
| TimeoutMinutes | `int?` | Макс. время выполнения (null — без лимита) |
| KillOnTimeout | `bool` | Завершать при таймауте |
| LogOutput | `bool` | Сохранять stdout/stderr |
| MaxHistoryRecords | `int` | Лимит записей журнала (по умолчанию 50) |

### TimeWindow (Триггер)

5 типов триггеров:

| Тип | Описание | Поля |
|-----|----------|------|
| **Однократно** | Запуск в указанную дату и время | OnceDate, OnceTime |
| **Ежедневно** | Каждые N дней в указанные времена | DayInterval, ExactTimes |
| **Еженедельно** | Каждые N недель в указанные дни и времена | WeekInterval, DaysOfWeek, ExactTimes |
| **Ежемесячно** | В указанное число месяца каждые N месяцев | MonthDay, MonthInterval, ExactTimes |
| **Интервал** | Повтор каждые N минут в диапазоне времени | StartTime, EndTime, RepeatIntervalMinutes |

Общие поля:
- `StartDate` / `EndDate` — диапазон дат действия
- `RepeatUntil` — время последнего запуска (для интервала)
- `DaysOfWeek` — дни недели
- `DaysOfMonth` — числа месяца
- `SpecificDates` — конкретные даты (для однократного)

### RunHistory (Журнал)

| Поле | Описание |
|------|----------|
| StartTime | Время запуска |
| EndTime | Время завершения |
| Status | Running / Completed / Failed / Killed / Timeout |
| ExitCode | Код возврата процесса |
| ErrorMessage | Текст ошибки |

---

## Поведение задач

### Ошибка запуска (OnError)

| Значение | Поведение |
|----------|-----------|
| `SkipNext` | Пропустить, ждать следующий |
| `Retry` | Повторить N раз с интервалом |
| `Fail` | Прекратить, зафиксировать ошибку |

### Наложение запусков (OnOverlap)

| Значение | Поведение |
|----------|-----------|
| `WaitComplete` | Ждать завершения текущего |
| `WaitNext` | Пропустить запуск |
| `StartAnother` | Запустить параллельно |

### Таймаут

При превышении `TimeoutMinutes`:
- `KillOnTimeout = true` → процесс завершается, статус `Timeout`
- `KillOnTimeout = false` → процесс продолжает работу, предупреждение в журнал

---

## Установка

### Требования

- Windows 10 x64 / Windows Server 2016+
- .NET 8 Desktop Runtime (для UI)
- Visual Studio 2022 (для сборки C++ компонента)

### Сборка

```powershell
# Полная сборка (C++ + C#)
.\publish.ps1

# Или вручную:
# C++ служба
msbuild src\WinShedulerSvc\WinShedulerSvc.vcxproj /p:Configuration=Release /p:Platform=x64

# C# UI
dotnet publish src\WinSheduler.UI\WinSheduler.UI.csproj -c Release -o published\ui
```

### Установка службы

```powershell
# Запуск от администратора
.\deploy.bat
```

Или вручную:
```powershell
sc create WinSheduler binPath="C:\WinSheduler\published\service\WinShedulerSvc.exe" start=auto DisplayName="WinSheduler Task Scheduler"
sc start WinSheduler
```

### Удаление службы

```powershell
sc stop WinSheduler
sc delete WinSheduler
```

### Standalone режим (без службы)

```powershell
.\service\WinShedulerSvc.exe --standalone
```

---

## Структура проекта

```
WinSheduler/
├── src/
│   ├── WinShedulerSvc/           # C++ служба (Win32, SQLite)
│   │   ├── *.cpp, *.h            # Исходный код
│   │   ├── sqlite3.c, sqlite3.h  # SQLite amalgamation
│   │   └── WinShedulerSvc.vcxproj
│   ├── WinSheduler.UI/           # WPF-приложение (C# .NET 8)
│   │   ├── MainWindow.xaml       # Главное окно
│   │   ├── TaskEditorWindow.xaml # Редактор задач (вкладки)
│   │   ├── TimeWindowDialog.xaml # Диалог создания триггера
│   │   ├── HistoryWindow.xaml    # Журнал запусков
│   │   ├── IpcClient.cs          # Named Pipe клиент
│   │   ├── Resources/Icons/      # PNG иконки (Build Action: Resource)
│   │   └── WinSheduler.UI.csproj
│   └── WinSheduler.Shared/       # Модели, контракты (C#)
├── published/                    # Готовые сборки
│   ├── service/                  # WinShedulerSvc.exe + data/ + logs/
│   └── ui/                       # WinSheduler.UI.exe + DLL
├── deploy.bat                    # Установка службы
├── publish.ps1                   # Скрипт сборки
├── WinSheduler.sln               # Visual Studio решение
├── SPECIFICATION.md              # Полная спецификация
└── README.md
```

---

## IPC Протокол

Named Pipe `\\.\pipe\WinSheduler`, message-mode, JSON.

| Действие | Описание |
|----------|----------|
| `GetStatus` | Статус службы |
| `ListTasks` | Список всех задач |
| `GetTask` | Детали задачи |
| `GetTimeWindows` | Триггеры задачи |
| `GetHistory` | Журнал запусков |
| `CreateTask` | Создание задачи |
| `UpdateTask` | Обновление задачи |
| `DeleteTask` | Удаление задачи |
| `EnableTask` / `DisableTask` | Включение / отключение |

---

## Безопасность

- **Пароли** — DPAPI (`CryptProtectData` с `CRYPTPROTECT_LOCAL_MACHINE`)
- **IPC** — Named Pipe, только локально
- **Память** — `SecureZeroMemory()` после использования пароля
- **Служба** — работает под SYSTEM

---

## Лицензия

MIT
