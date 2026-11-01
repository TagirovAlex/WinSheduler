# Контрактные тесты IPC (трек B)

Автономный C++ тест-проект для протокола IPC и политики доступа.
Собирается отдельно от `./WinSheduler.sln`, потому что добавление проектов
в решение — зона Integrator.

## Состав

| Файл | Назначение |
|------|------------|
| `ipc_tests.cpp` | контрактные тесты: кадрирование, конверты, JSON-лимиты, UTF-8, политика, валидация, fuzz |
| `ipc_tests.vcxproj` | проект MSBuild; компилирует `json.cpp`, `ipc_frame.cpp`, `auth_policy.cpp`, `auth_validate.cpp` из службы |
| `fuzz_target.cpp` | цель libFuzzer для `clang++ -fsanitize=fuzzer,address` (в обычную сборку не входит) |
| `run.ps1` | сборка и запуск тестов |

## Запуск

```powershell
./tests/ipc/run.ps1
```

Скрипт находит MSBuild через `vswhere` (как `./scripts/build.ps1`), собирает
`./tests/ipc/bin/Release/ipc_tests.exe` и запускает его. Ненулевой код возврата
означает провал контрактных тестов.

## Что проверяется

- Кадрирование v2: 4 байта длины little-endian, лимит 1 МиБ, распознавание
  legacy-сообщения без префикса.
- Конверты: `{v,id,action,payload}` и переходный `{Action,Payload}`,
  `UNSUPPORTED_VERSION`, превышение глубины.
- JSON: лимиты 1 МиБ / 64 / 64 КиБ, синтаксические ошибки, UTF-8 (битые
  последовательности, суррогатные пары), детерминированный fuzz.
- Политика доступа: гость / оператор / администратор, фильтрация по владельцу,
  запрет RunAs и `SessionMode=ActiveUser` для не-администратора.
- Валидация: GUID, локальные пути, `HH:mm`, `yyyy-MM-dd`, триггеры, диапазоны.

## Fuzz-цель

```powershell
clang++ -std=c++20 -fsanitize=fuzzer,address -I./src/WinShedulerSvc `
  ./src/WinShedulerSvc/json.cpp ./src/WinShedulerSvc/ipc_frame.cpp `
  ./tests/ipc/fuzz_target.cpp -o ./temp/ipc_fuzz
```

## Подключение к gate

`./scripts/verify.ps1` пока не вызывает эти тесты (файл принадлежит треку G).
Запрос на добавление шага `contract-cpp` — в `./docs/status/b-requests.md`.
