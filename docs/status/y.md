# Статус Track Y — живой статус выполнения

Дата: 2026-11-06.

## Сделано

- Y-1: `DashboardViewModel.RefreshSilentlyAsync` — «тихое» обновление дашборда:
  не включает `IsLoading`, не блокирует кнопки, не показывает прогресс,
  не очищает и не перезаписывает текущую ошибку пользователя. Сбой тихого
  обновления остаётся незаметным — его покажет следующий ручной `RefreshAsync`.
- Y-2: цикл сопровождения запущенных задач в `DashboardViewModel`:
  - множество Id и один общий цикл `FollowLoopAsync`;
  - задачи с `Running=true` ставятся на сопровождение после `RefreshAsync`
    и после тихого обновления; для ручного запуска есть публичный `TrackRun(Guid)`;
  - опрос `IIpcService.GetHistoryAsync(taskId, 1, token)` по каждой задаче;
  - интервалы 2 с первые 30 с, затем 5 с, лимит сопровождения задачи — 24 ч;
  - grace 5 с: первый `Running=false` не останавливает цикл и не считается
    завершением, пока не истёк grace-период;
  - остановка: два опроса не `Running` подряд после grace; задача удалена
    (`NOT_FOUND` или пропала из списка); `StopFollowing()`;
    `UNAVAILABLE`; превышение 24 ч;
  - запись истории от прошлого запуска (старше начала сопровождения) не даёт
    ложного «завершения»;
  - при `IpcException` с кодом `Unavailable` цикл останавливается, ошибка
    показывается один раз и не перекрывает уже видимую ошибку; прочие ошибки
    опроса баннер не трогают;
  - публичный `StopFollowing()` — отмена при закрытии окна.
- Y-3: `TaskListItemViewModel`:
  - `RunStatus? LastRunStatus` и `bool HasRunError`;
  - `ApplyRunStatus(RunHistory?)` обновляет `Running`, `LastRun`
    (`StartTime`/`Status`/`ExitCode`) и поднимает `PropertyChanged`;
  - ошибкой считаются `Failed`, `Killed`, `Timeout`, `Aborted`.
- Событие `RunFinished` (`TaskId`, `Name`, `RunStatus`, `ExitCode`,
  `ErrorMessage`) для MainWindow: ViewModel не содержит пользовательских
  текстов, баннер формирует Track E.
- Публичное поведение `RefreshAsync` для ручного F5 не изменено: `IsLoading`,
  очистка ошибок и `RefreshCompleted` остались прежними.

## Проверка

- `dotnet build ./src/WinSheduler.UI/WinSheduler.UI.csproj -c Release --nologo` —
  успешно, 0 предупреждений, 0 ошибок.
- `dotnet test ./tests/ui/WinSheduler.UI.Tests.csproj -c Release --nologo` —
  успешно, 46 тестов (37 прежних + 9 новых), 0 ошибок.
- Новый файл `./tests/ui/RunFollowTests.cs`, fake IPC дополнен `HistoryFactory`.
  Покрыты сценарии: `Running → Completed`, `Running → Failed`, гонка старта,
  `TrackRun` без обновления дашборда, старая история, две задачи, `UNAVAILABLE`,
  отмена, тихое обновление без `IsLoading` и без потери ошибки.
- Тесты не требуют реальной службы.

- Y-4: в `MainWindow` колонка «Статус» показывает «Выполняется» / «Ошибка» /
  «Включена/Отключена», в «Последнем запуске» — статус с цветом;
  завершение/ошибка отражаются в статус-баре и баннере.
- Y-5: после `RunNow` вызывается `TrackRun(task.Id)` и `RefreshSilentlyAsync()`;
  `StopFollowing()` — при закрытии окна; задачи с `running=true` подхватываются
  после любого обновления дашборда.
- Y-7: поведение и интервалы описаны в `./docs/contracts/UI.md`.

## Проверка (дополнительно)

- `dotnet build` UI — успешно, 0 предупреждений.
- UI.Tests — 60/60 (включая 9 тестов `RunFollowTests`).
- `./scripts/verify.ps1 -Scope integration` — зелёный.

## Осталось

- Ручной smoke на реальной службе: запустить задачу и увидеть завершение/ошибку
  без F5. Выполняется в рамках H-8/I-8.

## Блокеры

- Нет.
