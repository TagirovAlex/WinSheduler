# Статус трека G (QA и Gatekeeper)

## Текущая задача

G-1 (стратегия тестирования и матрица), G-2 (C++ тест-харнесс и gate),
G-4 (контрактные тесты на общих фикстурах). Дополнительно: G-9 (описание
гигиены репозитория), G-10 (сборка с чистого checkout), G-11 (gate-отчёт).
Статус: **G-1, G-2, G-4 готовы; G-9, G-10, G-11 — частично** (см. ниже).

## Выполнено

### G-1. Стратегия тестирования

- `./tests/README.md` — цели, уровни (unit/contract/integration/e2e),
  матрица покрытия по областям и трекам, бюджеты производительности,
  правила запуска, чего в стратегии нет.
- `./tests/e2e/README.md` — каркас E2E: планируемые сценарии E2E-1…E2E-8,
  привязка к чеклисту `./docs/contracts/INSTALL.md`, п. 13.
- `./tests/contract/README.md` — состав контрактных тестов, запуск, правила
  работы со схемами и `PENDING`.

### G-2. C++ тест-харнесс

- `./tests/contract/cpp/test_harness.h` — минимальный харнесс без внешних
  зависимостей: `TEST_CASE`, `CHECK`, `CHECK_EQ`, `REQUIRE`, `PENDING`.
- `./tests/contract/cpp/main.cpp` — раннер: `--fixtures=<каталог>`,
  `--filter=<подстрока>`, ненулевой код при провале.
- `./tests/contract/cpp/test_json.cpp` — 14 тестов: round-trip, Unicode,
  экранирование, числа, битый/обрезанный вход, fuzz (фиксированное зерно),
  лимит строки 64 КиБ, глубина 64.
- `./tests/contract/cpp/WinSheduler.ContractTests.vcxproj` — отдельный проект
  (`/utf-8`, `stdcpp20`, `/MT`), собирает боевой `./src/WinShedulerSvc/json.cpp`.
  В `./WinSheduler.sln` не входит, чтобы не менять файл решения (зона Integrator).
- Итог прогона: 25 тестов, 2431 проверка, 0 провалов.

### G-4. Контрактные тесты

- `./tests/contract/fixtures/` — 9 общих фикстур: запрос/ответ/ошибка v1,
  legacy-конверт, `TaskDto`, `TimeWindowDto`, `RunHistoryDto`, `ServiceStatus`,
  `manifest.json` (фикстура → схема).
- C++ сторона (`test_contract.cpp`) проверяет инварианты конверта, GUID,
  UTC-время, перечисления, лимиты и round-trip фикстур.
- C# сторона (`./tests/contract/WinSheduler.ContractTests`, xUnit, 46 тестов):
  конверт v1 и legacy, DTO, кадрирование 4+len LE (эталонная реализация
  `IpcFrame`), лимиты 1 МиБ / 64 / 64 КиБ, round-trip, отсутствие секретов
  в фикстурах, валидатор JSON Schema (подмножество draft 2020-12),
  fuzz декодера кадра.
- Проверка по JSON-схемам включается автоматически, когда Architect добавит
  схемы в `./docs/contracts/schemas/`; пока — пропуск с сообщением.

### Gate (G-2, G-11)

- `./scripts/verify.ps1`: добавлены шаги `contract-cpp`, `contract-csharp`,
  опциональный `clean-checkout` (`-CleanCheckout`). Интерфейс `-Scope`
  сохранён, `-Scope g` работает как раньше и включает новые шаги.
- `Invoke-Step` получил `-ShowOutput`, чтобы вывод контрактных тестов был
  виден при провале; остальные шаги вывод по-прежнему подавляют.

### G-9. Гигиена репозитория (описание, без удаления)

В корне основного дерева найдены артефакты, которые не должны попадать в git.
Трек G их **не удалял** (запрещено вне `./temp/`), список — для Integrator:

| Путь | Что это | Действие |
|------|---------|----------|
| `./delete`, `./query`, `./start`, `./stop` | файлы по 13 байт с текстом `WinSheduler` (следы `sc delete`/`sc query` без кавычек) | удалить, добавить в `.gitignore` |
| `./scheduler.db`, `./scheduler.db-shm`, `./scheduler.db-wal` | пустая БД (4 КиБ) от ручного запуска службы | удалить, игнорировать `scheduler.db*` |
| `./WinSheduler-v1.1.1.zip` | удалён в индексе (`D`), но файл ещё есть в рабочем дереве | подтвердить удаление |

Текущий `./.gitignore` игнорирует `query` и `stop`, но не `delete`, `start`
и `scheduler.db*`; комментарий в файле содержит битую кодировку (мохибейк).
Правка `.gitignore` — зона Integrator (запрос в `./docs/status/g-requests.md`).

### G-10. Сборка с чистого checkout

- В `./scripts/verify.ps1` добавлен шаг: `git archive HEAD` → распаковка в
  `./temp/verify/clean` → `./scripts/build.ps1`. Проверено на коммите
  `48101d9`: сборка из снимка зелёная.
- Шаг opt-in (`-CleanCheckout`), потому что удваивает время gate.

### G-11. Gatekeeper

- `./docs/status/gate.md` обновлён записью о прогоне на коммите `48101d9`.

## Расхождения с контрактами

1. **Конверт IPC.** `./docs/contracts/IPC.md` описывает v1
   (`v`/`id`/`action`/`ok`/`error`/`payload`), а служба пока реализует только
   legacy-конверт (`Action`/`Payload` → `Success`/`Error`/`Payload`).
   Контрактные тесты фиксируют оба формата: v1 — целевой, legacy — переходный
   до задачи B-3. Расхождение кода с контрактом — зона B, не G.
2. **Кадрирование.** В `ipc.cpp` используется message-mode пайп и буфер
   64 КиБ без length-prefix; контракт требует 4 байта длины LE и 1 МиБ.
   Эталонная реализация кадрирования в тестах (`IpcFrame`) соответствует
   контракту; серверная часть — задача B-3.
3. **Лимит глубины JSON.** Парсер `json.cpp` не ограничивает глубину;
   контракт — 64. Харденинг — B-4. Тест глубины 64 проходит, 65+ помечен
   `PENDING`.
4. **`json::escape` и невалидный UTF-8.** При битом lead-байте `escape`
   съедает следующий байт и может выдать `\u000d` вместо `\r`. Fuzz это
   воспроизводит; идемпотентность сериализации проверяется только на валидном
   UTF-8, дефект зафиксирован для B-4.
5. **`json::unescape` и суррогатные пары.** Эмодзи кодируется парой
   `\ud83d\ude00`, а `unescape` собирает CESU-8 вместо UTF-8. Тест помечен
   `PENDING`, дефект — B-4.
6. **JSON-схемы отсутствуют.** `./docs/contracts/schemas/` содержит только
   `README.md`; проверка по схемам пропускается. Запрос Architect —
   `./docs/status/g-requests.md`.
7. **`GetHistory` limit.** Контракт: максимум 1000, по умолчанию 100; в
   `config.h` есть `MAX_HISTORY_FETCH = 100`. Противоречия нет, но тест на
   серверный лимит появится вместе с B-5.
8. **`RunHistoryDto.outputPath`.** В контракте путь строится от `DATADIR`
   (`./logs/output/<taskId>/<historyId>.log`); фикстура использует эту форму.

## В работе

- G-3 (C# xUnit для ViewModel/сервисов) — ждёт D-1…D-7.
- G-5 (статический анализ) — **частично**: добавлен opt-in шаг `-Analyze`
  (MSVC `/analyze` для контрактного проекта C++, `clang-tidy` для `json.cpp`
  при наличии). `.NET analyzers` включены через `Directory.Build.props` и
  работают в обычной сборке. `clang-tidy` в окружении отсутствует.
- G-6 (fuzz-цели JSON/IPC) — **частично**: fuzz JSON-парсера и мутационный
  fuzz в C++ харнессе; fuzz декодера кадра в C#. Fuzz IPC-протокола целиком
  ждёт B-3/B-4.
- G-7 (бенчмарки) — ждёт A-15, C-8, D-7; `./scripts/bench.ps1` пока заглушка.
- G-8 (документация README/SPEC/CHANGELOG) — после стабилизации поведения.

## Осталось

- Добавить схемы (Architect) и включить проверку по ним.
- Подключить `./tests/contract/WinSheduler.ContractTests` к `dotnet format`
  в `./scripts/format.ps1` (сейчас проверяется вручную; замечаний нет).
- Решить с Integrator, включать ли контрактные проекты в `./WinSheduler.sln`.
- Реализовать `./scripts/bench.ps1` после появления метрик A/C/D.
- Закрыть `PENDING` после B-4.

## Проверки

- `./scripts/verify.ps1 -Scope g` — успешно: build, test, format,
  contract-cpp (25 тестов), contract-csharp (46 тестов), schemas (пропуск:
  схем нет), smoke.
- `./scripts/verify.ps1 -Scope g -Analyze` — успешно: MSVC `/analyze` без
  замечаний, `clang-tidy` отсутствует (предупреждение).
- `./scripts/verify.ps1 -Scope g -CleanCheckout` — успешно, включая сборку
  снимка `git archive HEAD` в `./temp/verify/clean`.
- `dotnet format whitespace` для контрактного проекта — без замечаний.
- Предупреждения gate (не ошибки): `clang-format` не установлен;
  легаси `end_of_line`/`charset` в `WinSheduler.UI.csproj`; JSON-схемы
  отсутствуют.

## Блокеры

Нет. Внешние инструменты `clang-tidy`, `clang-format` не установлены —
это влияет только на G-5 и проверку форматирования C++, не блокирует gate.

## Запросы к другим трекам

См. `./docs/status/g-requests.md`.
