# Запросы трека G

Запросы на правку чужих файлов и найденные дефекты вне зоны G.
Сам трек G эти файлы не меняет.

## Architect

### 1. JSON-схемы контрактов (G-4)

Нужны схемы в `./docs/contracts/schemas/` (draft 2020-12, все новые поля —
необязательные). Имена ожидаются по `./tests/contract/fixtures/manifest.json`:

| Файл схемы | Фикстуры |
|------------|----------|
| `IpcRequest.schema.json` | `ipc-request.json` |
| `IpcResponse.schema.json` | `ipc-response-ok.json`, `ipc-response-error.json` |
| `IpcLegacyRequest.schema.json` | `ipc-legacy-request.json` |
| `IpcLegacyResponse.schema.json` | `ipc-legacy-response.json` |
| `TaskDto.schema.json` | `task-dto.json` |
| `TimeWindowDto.schema.json` | `time-window-dto.json` |
| `RunHistoryDto.schema.json` | `run-history-dto.json` |
| `ServiceStatus.schema.json` | `service-status.json` |

Требования, которые уже проверяются тестами:

- `$schema` = `https://json-schema.org/draft/2020-12/schema`;
- `IpcResponse`: `ok=true` → `error=null`, `payload` — объект;
  `ok=false` → `payload=null`, `error.code` из списка IPC.md, п. 6;
- `TaskDto`: нет полей `password`, `encryptedPassword`, `ownerSid`
  (секреты не возвращаются);
- `TimeWindowDto.kind` — `Once/Daily/Weekly/Monthly/Interval`;
- время — строки `HH:mm`, даты — `yyyy-MM-dd`, события — UTC ISO 8601 с `Z`.

Проверка включается автоматически, как только файл появляется; правок в тестах
не требуется. Пока схем нет, шаг печатает пропуск и gate остаётся зелёным.

### 2. Конверт IPC: v1 против legacy

`./docs/contracts/IPC.md` (п. 2 и 7) требует v1-конверт и legacy как переходный
режим. Служба (`ipc.cpp`) реализует только legacy. Просьба подтвердить, что
v1 — целевой формат для B-3, и что legacy удаляется в v2. Контрактные тесты
сейчас проверяют оба формата и не требуют правки кода.

## Track B (IPC и безопасность)

Дефекты `./src/WinShedulerSvc/json.cpp`, найденные fuzz-тестами G-2/G-6.
Тесты помечены `PENDING` и gate не валят.

1. **`escape` съедает байт после битого lead-байта.** В цикле декодирования
   UTF-8 (`json.cpp`, строки ~42–47) `++i` выполняется даже если следующий
   байт не является continuation. Пример: `CD 60` даёт `\u000d` вместо `\r`,
   а байт `` ` `` теряется. Ожидание: невалидный lead-байт → `?`, следующие
   байты обрабатываются независимо.
2. **`unescape` не собирает суррогатную пару.** `\ud83d\ude00` превращается
   в CESU-8 (6 байт) вместо UTF-8 (4 байта). Ожидание: пара суррогатов →
   один codepoint.
3. **Нет лимита глубины JSON.** Контракт — 64 (IPC.md, п. 1). Парсер
   рекурсивный, глубина не проверяется. Ожидание: `TOO_LARGE` при > 64.
4. **Кадрирование не по контракту.** `ipc.cpp` использует message-mode пайп
   и буфер 64 КиБ без 4-байтового length-prefix; контракт — 4 байта длины LE,
   максимум 1 МиБ, ответ `TOO_LARGE` и закрытие соединения.
   Эталонная реализация — `./tests/contract/WinSheduler.ContractTests/IpcFrame.cs`.
5. **`IpcServer::stop` делает `detach`** для клиентских потоков (B-9 требует
   join/drain). Наблюдение из чтения кода, тестами G не покрыто.

## Integrator

1. **`.gitignore` и корневые артефакты (G-9).** Добавить `delete`, `start`,
   `scheduler.db*`; убрать битый комментарий (мохибейк). Удалить файлы:
   `./delete`, `./query`, `./start`, `./stop`, `./scheduler.db`,
   `./scheduler.db-shm`, `./scheduler.db-wal`; подтвердить удаление
   `./WinSheduler-v1.1.1.zip` (в индексе уже `D`).
2. **`./scripts/README.md`.** Обновить описание gate: новые шаги
   `contract-cpp`, `contract-csharp`, ключи `-CleanCheckout` и `-Analyze`,
   расположение фикстур и контрактных проектов.
3. **`./scripts/format.ps1`.** Добавить
   `./tests/contract/WinSheduler.ContractTests/WinSheduler.ContractTests.csproj`
   в список проверяемых проектов (сейчас проверен вручную — замечаний нет).
4. **`./WinSheduler.sln`.** Решить, включать ли контрактные проекты в решение.
   Сейчас они собираются напрямую из `verify.ps1`, чтобы не менять файл
   решения. Если включать — нужен отдельный проект C++ в решении и
   `dotnet test` для C#.

## Track A, C, D

Для G-7 (бенчмарки) нужны точки замера и тестовые данные:

- A-15: функция тика на 1000 задач с инъекцией часов и fake runner;
- C-8: наполнение БД 1000 задачами и запрос истории;
- D-7: batch-обновление `GetDashboard` с отменой.

Пока этих точек нет, `./scripts/bench.ps1` остаётся заглушкой с явным
сообщением.
