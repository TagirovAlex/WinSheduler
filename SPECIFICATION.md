# WinSheduler — Техническая спецификация

## 1. Назначение

Windows-сервис с графическим интерфейсом для планирования и запуска задач по расписанию. Полностью независим от встроенного Task Scheduler.

## 2. Целевые платформы

- Windows 10 x64 и выше
- Windows Server 2016 и выше

## 3. Стек технологий

- **Service (C++)** — Win32 API, sqlite3 (amalgamation), DPAPI, Named Pipes
- **UI (C# WPF)** — .NET 8, XAML, System.Text.Json
- **IPC** — Named Pipes (локально, `\\.\pipe\WinSheduler`, message-mode)
- **Хранилище** — SQLite (WAL journal, FK включены)
- **Шифрование** — DPAPI (`CryptProtectData` / `CRYPTPROTECT_LOCAL_MACHINE`)

## 4. Архитектура

```
WinShedulerSvc.exe (C++, /MT, ~1.5 MB)
  ├─ Планировщик (1-секундный тик)
  ├─ IPC сервер (Named Pipe)
  └─ SQLite (data\scheduler.db)

WinSheduler.UI.exe (C# WPF, .NET 8)
  ├─ MainWindow (список задач)
  ├─ TaskEditorWindow (вкладки: Основные / Расписание / Параметры)
  ├─ TimeWindowDialog (создание триггера)
  ├─ HistoryWindow (журнал)
  └─ IpcClient (Named Pipe клиент)
```

## 5. Модель данных

### 5.1. Task (Задача)

| Поле | Тип | Описание |
|------|-----|----------|
| Id | GUID | Уникальный идентификатор |
| Name | string | Название задачи |
| Description | string | Описание |
| Enabled | bool | Включена / отключена |
| ProgramPath | string | Путь к исполняемому файлу |
| Arguments | string | Аргументы командной строки |
| WorkingDirectory | string | Рабочая папка |
| WindowStyle | enum | Normal=0 / Hidden=1 / Minimized=2 / Maximized=3 |
| RunAsUser | string? | Имя пользователя (null — SYSTEM) |
| RunAsDomain | string? | Домен |
| EncryptedPassword | string? | Пароль (DPAPI + base64) |
| OnError | enum | SkipNext=0 / Retry=1 / Fail=2 |
| RetryCount | int | Количество повторов (при OnError=Retry) |
| OnOverlap | enum | WaitComplete=0 / WaitNext=1 / StartAnother=2 |
| TimeoutMinutes | int? | Лимит выполнения (null — без лимита) |
| KillOnTimeout | bool | Завершать при таймауте |
| LogOutput | bool | Сохранять stdout/stderr |
| MaxHistoryRecords | int | Лимит записей журнала (по умолчанию 50) |
| CreatedAt | string | ISO 8601 время создания |
| ModifiedAt | string | ISO 8601 время изменения |

### 5.2. TimeWindow (Триггер)

5 типов триггеров:

#### Однократно (Once)
Запуск в указанную дату и время.

| Поле | Тип | Описание |
|------|-----|----------|
| SpecificDates | List<DateTime> | Дата запуска |
| ExactTimes | List<string> | Время запуска (HH:mm) |

#### Ежедневно (ExactTimes, без DaysOfWeek)
Каждые N дней в указанные времена.

| Поле | Тип | Описание |
|------|-----|----------|
| ExactTimes | List<string> | Времена запуска |
| DayInterval | int | Интервал в днях (хранится через вычисление) |

#### Еженедельно (ExactTimes + DaysOfWeek)
Каждые N недель в указанные дни и времена.

| Поле | Тип | Описание |
|------|-----|----------|
| DaysOfWeek | List<int> | Дни недели (0=Sun..6=Sat) |
| ExactTimes | List<string> | Времена запуска |

#### Ежемесячно (ExactTimes + DaysOfMonth)
В указанное число месяца каждые N месяцев.

| Поле | Тип | Описание |
|------|-----|----------|
| DaysOfMonth | List<int> | Числа месяца (1-31) |
| ExactTimes | List<string> | Времена запуска |

#### Интервал (Interval)
Повтор каждые N минут в диапазоне времени.

| Поле | Тип | Описание |
|------|-----|----------|
| StartTime | string | Начало окна (HH:mm) |
| EndTime | string | Конец окна (HH:mm) |
| RepeatIntervalMinutes | int | Интервал повтора (мин) |
| RepeatUntil | string? | Время последнего запуска |

#### Общие поля

| Поле | Тип | Описание |
|------|-----|----------|
| Id | GUID | Уникальный идентификатор |
| TaskId | GUID | Ссылка на задачу |
| Type | enum | ExactTimes=0 / Interval=1 |
| StartDate | string? | Дата начала (yyyy-MM-dd) |
| EndDate | string? | Дата окончания |

### 5.3. RunHistory (Журнал)

| Поле | Тип | Описание |
|------|-----|----------|
| Id | GUID | Уникальный идентификатор |
| TaskId | GUID | Ссылка на задачу |
| StartTime | string | Время запуска (ISO 8601) |
| EndTime | string? | Время завершения |
| ExitCode | int? | Код возврата |
| Pid | int | ID процесса |
| Status | enum | Running=0 / Completed=1 / Failed=2 / Killed=3 / Timeout=4 |
| ErrorMessage | string | Текст ошибки |
| OutputPath | string? | Путь к файлу вывода |

## 6. IPC Протокол

### Формат сообщения

```json
// Запрос
{ "Action": "CreateTask", "Payload": "{...}" }

// Ответ
{ "Success": true, "Error": "", "Payload": "..." }
```

### Действия

| Действие | Payload | Ответ (Payload) |
|----------|---------|-----------------|
| `GetStatus` | — | ServiceStatus JSON |
| `ListTasks` | — | [TaskDto] JSON |
| `GetTask` | Task ID | TaskDto JSON |
| `GetTimeWindows` | Task ID | [TimeWindow] JSON |
| `GetHistory` | Task ID | [RunHistory] JSON |
| `CreateTask` | TaskDto JSON | — |
| `UpdateTask` | TaskDto JSON | — |
| `DeleteTask` | Task ID | — |
| `EnableTask` | Task ID | — |
| `DisableTask` | Task ID | — |

## 7. Алгоритм планирования

### tick() — вызывается каждую секунду

1. Загрузить все задачи из БД
2. Для каждой задачи:
   - Пропустить, если `enabled = false`
   - Загрузить триггеры из `time_windows`
   - Для каждого триггера проверить `is_in_window()`
   - Если текущее время совпадает с точным временем → `should_run = true`
   - Проверить `on_overlap` (если задача уже выполняется)
   - Запустить через `CreateProcessW` или `CreateProcessWithLogonW`
3. Проверить завершённые процессы (`GetExitCodeProcess`)
4. Проверить таймауты

### is_in_window() — проверка триггера

1. Проверить `DaysOfWeek` (если заданы)
2. Проверить `DaysOfMonth` (если заданы)
3. Для ExactTimes: `abs(current_minutes - et_minutes) < 1`
4. Для Interval: проверить диапазон и интервал

## 8. Безопасность

- Пароли: DPAPI `CryptProtectData` с `CRYPTPROTECT_LOCAL_MACHINE`
- IPC: Named Pipe, только локально
- Память: `SecureZeroMemory()` после использования пароля
- Служба: работает под SYSTEM

## 9. Сборка и развёртывание

### Требования

- Visual Studio 2022 (MSVC v143, Windows 10 SDK)
- .NET 8 SDK
- .NET 8 Desktop Runtime (для UI)

### Сборка

```powershell
# Полная сборка
.\publish.ps1

# Или вручную
msbuild src\WinShedulerSvc\WinShedulerSvc.vcxproj /p:Configuration=Release /p:Platform=x64
dotnet publish src\WinSheduler.UI\WinSheduler.UI.csproj -c Release -o published\ui
```

### Установка

```powershell
# Через скрипт (от администратора)
.\deploy.bat

# Или вручную
sc create WinSheduler binPath="C:\path\to\WinShedulerSvc.exe" start=auto
sc start WinSheduler
```

### Standalone режим

```powershell
.\WinShedulerSvc.exe --standalone
```
