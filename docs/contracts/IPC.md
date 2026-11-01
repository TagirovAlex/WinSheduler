# Контракт IPC v1

Статус: заморожен. Изменения — только через ADR.
Транспорт: именованный канал `//./pipe/WinSheduler` (в коде используется каноническая форма Windows).
Кодировка: UTF-8 без BOM.

## 1. Кадрирование

Один кадр — 4 байта длины (little-endian, uint32) + JSON-тело.

- Максимальный размер кадра — 1 МиБ (запрос и ответ).
- Максимальная глубина JSON — 64.
- Максимальная длина строки — 64 КиБ.
- Соединение допускает последовательность кадров «запрос → ответ».
- Сервер обрабатывает один запрос на соединение за раз; клиент не конвейеризует запросы.
- При превышении лимитов сервер отвечает `TOO_LARGE` и закрывает соединение.

## 2. Конверт

Запрос:

```json
{
  "v": 1,
  "id": "8f1c2c1e-4a0b-4c1e-9a6e-1f2d3c4b5a60",
  "action": "GetStatus",
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

Ошибка:

```json
{
  "v": 1,
  "id": "8f1c2c1e-4a0b-4c1e-9a6e-1f2d3c4b5a60",
  "ok": false,
  "error": { "code": "AUTH_DENIED", "message": "Недостаточно прав" },
  "payload": null
}
```

`id` — GUID запроса, возвращается без изменений. `message` — русский текст для fallback; UI локализует по `code`.
`payload` может быть объектом, массивом или `null` (например, `ListTasks` возвращает массив).

## 3. Аутентификация и авторизация

Сервер на каждом соединении:

1. `ImpersonateNamedPipeClient`.
2. Получает SID клиента.
3. Проверяет членство в `WinSheduler Users` (оператор) или `Administrators` (админ).
4. Для не-админа фильтрует задачи по `owner_sid`.
5. Пишет запись в аудит для мутаций и `RunNow`.

| Действие | Гость | Оператор (группа) | Администратор |
|----------|-------|-------------------|---------------|
| `Ping` | да | да | да |
| `GetStatus` | нет | да | да |
| `GetDashboard`, `ListTasks`, `GetTask`, `GetTimeWindows`, `GetHistory` | нет | только свои | все |
| `CreateTask`, `UpdateTask`, `DeleteTask`, `SetTaskEnabled` | нет | только свои; без `RunAs`; `SessionMode=Headless` | все |
| `RunNow` | нет | только свои; от своего имени | все |
| `GetAudit`, `GetSettings`, `SetSettings` | нет | нет | да |

Не-админ не может задать `RunAsUser`, `RunAsDomain`, `Password` и `SessionMode=ActiveUser`.
Админ, работающий без elevation, имеет filtered token и обрабатывается как оператор. Для привилегированных действий UI перезапускается с `runas`.

## 4. Действия

### `Ping`

Payload: `{}`. Ответ: `{ "pong": true, "serviceVersion": "1.0.0" }`.

### `GetStatus`

Ответ:

```json
{
  "running": true,
  "isAdmin": false,
  "isOperator": true,
  "canManageSystemTasks": false,
  "serviceVersion": "1.0.0",
  "startedAt": "2026-11-01T09:00:00Z",
  "taskCount": 12,
  "runningTaskCount": 2,
  "dbReady": true
}
```

Пути к БД и логам не возвращаются не-админам.

### `GetDashboard`

Ответ:

```json
{
  "tasks": [
    {
      "id": "…",
      "name": "Ночной бэкап",
      "enabled": true,
      "isOwn": true,
      "ownerLabel": "Вы",
      "programPath": "d:/scripts/backup.cmd",
      "scheduleSummary": "Ежедневно 02:00",
      "lastRun": {
        "startTime": "2026-11-01T02:00:00Z",
        "status": "Completed",
        "exitCode": 0
      },
      "running": false
    }
  ]
}
```

`GetDashboard` заменяет серию `ListTasks + N × GetTimeWindows + N × GetHistory`.

### `ListTasks`, `GetTask`, `GetTimeWindows`, `GetHistory`

- `GetTask`, `GetTimeWindows`, `GetHistory`: payload `{ "taskId": "guid" }`.
- `GetHistory`: дополнительно `{ "limit": 100 }`, максимум 1000.
- `ListTasks`: payload `{}`.

`GetTask` возвращает `TaskDto`. `GetTimeWindows` — массив `TimeWindowDto`. `GetHistory` — массив `RunHistoryDto`.

### `CreateTask`, `UpdateTask`

Payload — `TaskDto`. Для `UpdateTask` обязателен `id`.

Правила:

- `id` валидируется как GUID на сервере; клиентский `id` не используется в путях.
- `owner_sid` при создании = SID вызывающего; при обновлении не меняется.
- `Password` — write-only. В ответах всегда `hasPassword`, сам пароль не возвращается.
- Пароль передаётся только по защищённому каналу и шифруется в службе `CRYPTPROTECT_SYSTEM`.
- `TimeWindows` заменяют набор триггеров целиком в одной транзакции.
- Неизвестные поля игнорируются (прямая совместимость).

### `DeleteTask`, `SetTaskEnabled`

Payload: `{ "taskId": "guid", "enabled": true }` для `SetTaskEnabled`; для `DeleteTask` — `{ "taskId": "guid" }`.

### `RunNow`

Payload: `{ "taskId": "guid" }`. Запуск проходит через планировщик с учётом `onOverlap`. Не-админ запускает от своего имени.

### `GetAudit`

Payload: `{ "from": "2026-11-01T00:00:00Z", "to": "2026-11-02T00:00:00Z", "limit": 500 }`. Только админ.

### `GetSettings`, `SetSettings`

Только админ. Набор ключей определяется в `./docs/contracts/DB.md` (таблица `settings`).

## 5. Модели

### `TaskDto`

| Поле | Тип | Примечание |
|------|-----|------------|
| `id` | GUID | |
| `name` | string | обязательное |
| `description` | string | |
| `enabled` | bool | |
| `programPath` | string | локальный абсолютный путь, прямые слэши |
| `arguments` | string | |
| `workingDirectory` | string | локальный абсолютный путь или пусто |
| `windowStyle` | `"Normal" / "Hidden" / "Minimized" / "Maximized"` | для headless рекомендуется `Hidden` |
| `sessionMode` | `"Headless" / "ActiveUser"` | `ActiveUser` в v1 отклоняется |
| `runAsUser` | string? | только админ |
| `runAsDomain` | string? | только админ |
| `hasPassword` | bool | только чтение |
| `password` | string? | только запись, только админ |
| `onError` | `"SkipNext" / "Retry" / "Fail"` | |
| `retryCount` | int | 0…10 |
| `onOverlap` | `"WaitComplete" / "WaitNext" / "StartAnother"` | |
| `timeoutMinutes` | int? | 0…10080 |
| `killOnTimeout` | bool | |
| `logOutput` | bool | |
| `maxHistoryRecords` | int | 1…10000 |
| `timeWindows` | `TimeWindowDto[]` | |
| `createdAt` | string | UTC ISO 8601 с `Z` |
| `modifiedAt` | string | UTC ISO 8601 с `Z` |
| `owner` | object | `{ "isOwn": true, "label": "Вы" }`; `sid` только админу |
| `isOwn` | bool | дублирует `owner.isOwn` для удобства |

### `TimeWindowDto`

| Поле | Тип | Примечание |
|------|-----|------------|
| `id` | GUID | |
| `kind` | `"Once" / "Daily" / "Weekly" / "Monthly" / "Interval"` | |
| `startDate` | `"yyyy-MM-dd"`? | локальная дата |
| `endDate` | `"yyyy-MM-dd"`? | локальная дата |
| `daysOfWeek` | `"Sunday"…"Saturday"[]` | для Weekly |
| `daysOfMonth` | int[] | 1…31, для Monthly |
| `specificDates` | `"yyyy-MM-dd"[]` | для Once |
| `exactTimes` | `"HH:mm"[]` | для Once/Daily/Weekly/Monthly |
| `startTime` | `"HH:mm"`? | для Interval |
| `endTime` | `"HH:mm"`? | для Interval |
| `repeatIntervalMinutes` | int? | для Interval, 1…1440 |
| `repeatUntil` | `"yyyy-MM-ddTHH:mm"`? | локальное время окончания повторов |
| `dayInterval` | int | для Daily, 1…365, по умолчанию 1 |
| `weekInterval` | int | для Weekly, 1…52, по умолчанию 1 |
| `monthInterval` | int | для Monthly, 1…12, по умолчанию 1 |

Правила валидации:

- `Once`: непустые `specificDates` и `exactTimes`.
- `Daily`: `exactTimes` непустой, `dayInterval >= 1`.
- `Weekly`: `daysOfWeek` непустой, `exactTimes` непустой, `weekInterval >= 1`.
- `Monthly`: `daysOfMonth` непустой, `exactTimes` непустой, `monthInterval >= 1`.
- `Interval`: `startTime < endTime`, `repeatIntervalMinutes >= 1`.
- Время — `HH:mm` в 24-часовом формате; невалидное значение отклоняется.
- `dayInterval`, `weekInterval`, `monthInterval` — неотрицательные целые со значением по умолчанию 1; это согласовано с `./docs/contracts/DB.md` (`NOT NULL DEFAULT 1`).
- `specificDates` на проводе — строки `yyyy-MM-dd`. C#-модель может хранить `DateTime`, но клиент обязан преобразовывать на границе IPC; `DateTime` в формате ISO с временем не допускается.

### `RunHistoryDto`

| Поле | Тип |
|------|-----|
| `id` | GUID |
| `taskId` | GUID |
| `startTime` | UTC ISO 8601 с `Z` |
| `endTime` | UTC ISO 8601 с `Z`? |
| `exitCode` | int? |
| `pid` | int |
| `status` | `"Running" / "Completed" / "Failed" / "Killed" / "Timeout" / "Aborted"` |
| `errorMessage` | string |
| `outputPath` | string? |
| `triggeredBy` | `"Schedule" / "Manual"` |

## 6. Коды ошибок

| Код | HTTP-аналог | Когда |
|-----|-------------|-------|
| `AUTH_DENIED` | 403 | нет прав |
| `NOT_FOUND` | 404 | задача/триггер не найдены |
| `VALIDATION` | 400 | невалидные поля |
| `CONFLICT` | 409 | дубль, гонка, занято |
| `TOO_LARGE` | 413 | превышен размер/глубина |
| `UNSUPPORTED_VERSION` | 400 | неизвестная версия протокола |
| `DB` | 500 | ошибка БД |
| `INTERNAL` | 500 | прочее |

## 7. Совместимость

- v1 — текущая версия. Неизвестная мажорная версия отклоняется `UNSUPPORTED_VERSION`.
- Переходный режим: если в запросе нет `v`, но есть `Action`, сервер обрабатывает legacy-конверт `{Action, Payload}` и отвечает `{Success, Error, Payload}`. Режим считается устаревшим и удаляется в v2.
- Новые поля добавляются только как необязательные; удаление и переименование — через мажорную версию.
