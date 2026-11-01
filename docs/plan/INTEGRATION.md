# Порядок интеграции

Документ для Integrator. Треки работают в изолированных worktree `./temp/worktrees/<track>` на ветках `track/<track>`.

## 1. Проверка ветки

Для каждого трека:

```powershell
git log main..track/<track> --oneline
git diff --stat main...track/<track>
```

Убедиться, что изменения не выходят за зону трека (см. `./docs/plan/AGENTS.md`). Если выходят — вернуть на доработку, не мержить.

## 2. Порядок слияния

| Шаг | Ветка | Причина |
|-----|-------|---------|
| 1 | `track/c` | схема БД и `database.h` — база для A и B |
| 2 | `track/a` | планировщик использует репозитории C и `trigger_state` |
| 3 | `track/b` | IPC использует `scheduler.h` и `database.h` |
| 4 | `track/d` | UI-клиент использует протокол B |
| 5 | `track/e` | экраны используют `IIpcService` и стили D |
| 6 | `track/f` | установщик использует аргументы службы и SDDL B |
| 7 | `track/g` | QA, схемы и gate — после всех |

Если трек не завершён, его ветку не мержить; остальные шаги можно продолжать, если зависимости выполнены.

## 3. Слияние

```powershell
git merge --no-ff track/<track> -m "merge: track/<track>"
```

Конфликты в общих файлах (`config.h`, `main.cpp`, `service.cpp`, `WinSheduler.sln`, `Directory.Build.props`) разрешает Integrator. Конфликты в файлах трека — владелец трека.

## 4. Gate после каждого слияния

```powershell
./scripts/verify.ps1 -Scope integration
```

- Зелёный — обновить `./docs/status/gate.md`, обновить `./docs/plan/TASKS.md`.
- Красный — merge freeze: исправить в ветке трека или откатить merge, затем повторить.

## 5. Обновление доски

После слияния отметить задачи `[x]` в `./docs/plan/TASKS.md`, добавить запись в журнал изменений и в `./docs/status/gate.md`.

## 6. Финальная проверка

- `./scripts/verify.ps1` с чистого checkout.
- Smoke-тест установки на чистой ВМ (Track F).
- Security-чеклист из `./docs/contracts/SECURITY.md`.
- Проверка отсутствия секретов в диффе.
- Проверка, что `./temp/` и `./artifacts/` не попали в коммит.
