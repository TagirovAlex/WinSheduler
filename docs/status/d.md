# Статус трека D — фундамент UI

## Текущая задача

D-1…D-10. Статус: **готово** (с оговорками ниже).

## Опубликованный контракт для Track E: `IIpcService`

Файл: `./src/WinSheduler.UI/Services/IIpcService.cs`, пространство имён
`WinSheduler.UI.Services`. Все методы асинхронные, `CancellationToken` — последний
параметр. Ошибки — `IpcException` (типизированный код) или
`IpcUnavailableException` (служба недоступна).

```csharp
public interface IIpcService : IAsyncDisposable
{
    Task<PingResult> PingAsync(CancellationToken ct = default);
    Task<ServiceStatus> GetStatusAsync(CancellationToken ct = default);

    Task<DashboardDto> GetDashboardAsync(CancellationToken ct = default);
    Task<IReadOnlyList<TaskDto>> ListTasksAsync(CancellationToken ct = default);
    Task<TaskDto> GetTaskAsync(Guid taskId, CancellationToken ct = default);
    Task<IReadOnlyList<TimeWindowDto>> GetTimeWindowsAsync(Guid taskId, CancellationToken ct = default);
    Task<IReadOnlyList<RunHistory>> GetHistoryAsync(Guid taskId, int limit = 100, CancellationToken ct = default);

    Task<TaskDto> CreateTaskAsync(TaskDto task, CancellationToken ct = default);
    Task<TaskDto> UpdateTaskAsync(TaskDto task, CancellationToken ct = default);
    Task DeleteTaskAsync(Guid taskId, CancellationToken ct = default);
    Task SetTaskEnabledAsync(Guid taskId, bool enabled, CancellationToken ct = default);
    Task RunNowAsync(Guid taskId, CancellationToken ct = default);

    Task<IReadOnlyList<AuditEntryDto>> GetAuditAsync(DateTimeOffset from, DateTimeOffset to, int limit = 500, CancellationToken ct = default);
    Task<IReadOnlyDictionary<string, string>> GetSettingsAsync(CancellationToken ct = default);
    Task SetSettingsAsync(IReadOnlyDictionary<string, string> settings, CancellationToken ct = default);
}
```

Модели: `TaskDto`, `TimeWindowDto`, `RunHistory`, `ServiceStatus` — из
`WinSheduler.Shared`; `PingResult`, `DashboardDto`, `DashboardTaskDto`,
`DashboardLastRunDto`, `AuditEntryDto` — в `./src/WinSheduler.UI/Services/IpcModels.cs`.

Ошибки: `IpcErrorCode` (AuthDenied, NotFound, Validation, Conflict, TooLarge,
UnsupportedVersion, Database, Internal, Unavailable, Timeout), `IpcException.Code`,
`RawCode`, `IsTransient`; русский текст — `IpcErrorText.Describe(ex)`.

Временный слой: `IpcClient.Send/SendAsync(IpcRequest)` (старый конверт) оставлен
для текущих экранов Track E и удаляется после их перевода на `IIpcService`.

## Выполнено

### D-1. MVVM

- `ViewModels/ViewModelBase.cs` — `INotifyPropertyChanged` + `INotifyDataErrorInfo`,
  `SetProperty`, `SetPropertyAndValidate`, `ValidateProperty` (DataAnnotations),
  `ValidateAll`, `AddError`/`SetErrors`/`ClearErrors`, событие `ErrorsChanged`.
- `ViewModels/Commands.cs` — `RelayCommand`, `RelayCommand<T>`, `AsyncCommand`,
  `AsyncCommand<T>`: `CanExecute`, отмена, защита от повторного входа
  (`Interlocked`), обработчик ошибок.
- `Services/IDialogService.cs` + `DialogService.cs` — подтверждение, информация,
  ошибка, выбор файла и папки; ViewModel не работает с `MessageBox` напрямую.

### D-2. Дизайн-система

- `Styles/Tokens.xaml` — отступы 4/8/12/16/24, высота контрола 28, иконка 16,
  шрифт Segoe UI 12/16, радиус 4, кнопка `Padding=12,4`, `MinWidth=90`,
  цвета успеха/ошибки/предупреждения из контракта.
- `Styles/Theme.Light.xaml`, `Styles/Theme.Dark.xaml` — семантические кисти
  (`App_BackgroundBrush`, `App_ForegroundBrush`, `Status_*` и т.д.).
  В тёмной теме цвета статусов осветлены для контраста 4.5:1.
- `Styles/Controls.xaml` — `PrimaryButton`, `SecondaryButton`, `DangerButton`,
  `ToolbarButton`, `FormLabel`, `FormTextBox`, `FormPasswordBox`, `FormComboBox`,
  `FormCheckBox`, `DataGridStyle` (виртуализация, `ObservableCollection`-дружелюбный),
  `EmptyState`, `StatusBanner` (+ варианты Error/Warning/Success/Info),
  `SectionHeader`, `CaptionText`, `Control_FocusVisual`.
- `Styles/IconFont.xaml` — глифы Segoe Fluent Icons с fallback Segoe MDL2 Assets,
  стили `IconText`, `IconTextOnAccent`.
- `Styles/Styles.xaml` — единая точка подключения (App.xaml).
- `Services/ThemeService.cs` — `ThemeMode.System/Light/Dark`, определение темы
  Windows через реестр, замена словаря без перезапуска, учёт высокой контрастности.

### D-3. Локализация

- `Localization/Strings.resx` — русские строки, ключи `Area_Element`
  (общие, главный экран, статусы, админ, диалоги, ошибки, история, валидация,
  время, тема, перечисления).
- `Localization/Strings.cs` — `ResourceManager`, культура `ru-RU`,
  отсутствующий ключ виден как `!Ключ!`.
- `Localization/LocExtension.cs` — `{loc:Loc Main_ActionRefresh}` для XAML.
- `Services/IpcErrorText.cs` — русский текст по коду ошибки, `message` — fallback.

### D-4. IPC-клиент v2

- `IpcClient.cs` — фрейминг «4 байта длины little-endian + JSON», одно соединение,
  последовательные запросы (`SemaphoreSlim`), таймауты подключения и ответа,
  отмена, лимит кадра 1 МиБ, проверка `id` ответа, переподключение.
- Повтор только идемпотентных запросов (`MaxAttempts`, линейная пауза).
- Чтение принимает и v2-конверт (`ok`/`payload`), и legacy (`Success`/`Payload`).
- `Services/IIpcService.cs`, `Services/IpcService.cs` — типизированные действия
  контракта; `Services/IpcException.cs` — коды и исключения;
  `Services/IpcModels.cs` — DTO дашборда и аудита;
  `Services/IpcDateTimeConverter.cs` — даты `yyyy-MM-dd` / ISO 8601.
- `GetDashboard` — единственный batch-запрос для списка; 2N+1 отсутствует.

### D-5. Host

- `App.xaml.cs` — единственный экземпляр (`Local\` мьютекс + событие активации
  существующего окна), глобальные обработчики (`DispatcherUnhandledException`,
  `AppDomain.UnhandledException`, `TaskScheduler.UnobservedTaskException`),
  журнал `%LOCALAPPDATA%/WinSheduler/logs` (формат
  `время | уровень | компонент | сообщение`, защита от log injection, ротация),
  тема, создание главного окна.
- DPI — манифест Phase 0: `PerMonitorV2`, `asInvoker`, `longPathAware`.
- `Services/AppServices.cs` — композиционный корень (`Ipc`, `Dialogs`, `Theme`,
  `Elevation`, `WindowState`, `Logger`), `CreateDashboardViewModel()`.

### D-6. Конвертеры

- `Converters.cs` — дата/время (`dd.MM.yyyy HH:mm:ss`, ru-RU), длительность,
  локализованные перечисления (статус запуска, тип триггера, стиль окна,
  действия, режим сессии, день недели), цвет статуса, bool/видимость, null,
  `EnumToBoolean`. Прежние ключи `BoolToStatusConverter`/`BoolToColorConverter`
  сохранены для текущих экранов.

### D-7. Batch-обновление

- `ViewModels/DashboardViewModel.cs` — `ObservableCollection<TaskListItemViewModel>`,
  `ICollectionView` с поиском и фильтром «Все/Мои/Включённые/Отключённые»,
  `RefreshCommand` (один `GetDashboard`), `CancelRefreshCommand`,
  `RefreshStatusCommand`, `IsLoading`, `ErrorText`/`ErrorCode`,
  `IsServiceUnavailable`, защита от повторного входа, обновление строк на месте,
  восстановление выбора.
- `ViewModels/TaskListItemViewModel.cs` — строка списка с `Apply(dto)`.

### D-8. Настройки окна

- `Services/WindowStateService.cs` — размер, позиция, развёрнутость и последняя
  выбранная задача в `%LOCALAPPDATA%/WinSheduler/ui-state.json`;
  восстановление только если окно попадает на экран; минимальный размер 1024×700;
  повреждённый файл не ломает запуск.

### D-9. Доступность и клавиатура

- `Control_FocusVisual` для кнопок и полей, `RecognizesAccessKey` в шаблонах,
  `FormLabel` под `Label.Target`, контраст текста в обеих темах.
- `Services/ShortcutCatalog.cs` — F5, Ctrl+N, Enter, Delete, Ctrl+R, Esc
  (единый источник для `InputBindings` и справки).

### D-10. Права администратора

- `Services/ElevationService.cs` — `IsAdmin`, `IsOperator`,
  `CanManageSystemTasks` из `GetStatus`, `IsProcessElevated` (filtered token),
  `RoleText`, `RelaunchAsAdmin()` через `runas` с завершением текущего процесса.
- `DashboardViewModel.IsAdmin/CanManageSystemTasks/RoleText` — для скрытия
  привилегированных действий и бейджей.

## Принятые решения

1. **DTO дашборда — в UI-слое.** `DashboardDto`, `AuditEntryDto` и др. — проекции
   IPC-контракта, `Shared` принадлежит Architect. Запрос на перенос — в
   `./docs/status/d-requests.md`.
2. **Legacy-слой IPC сохранён.** `IpcClient.Send/SendAsync(IpcRequest)` работает
   по старому конверту в режиме сообщений, чтобы текущие экраны Track E
   собирались и работали до перевода на `IIpcService`.
3. **Тёмная тема — отдельные кисти.** Токены контракта (`#107C10` и др.) остаются
   в `Tokens.xaml`; в тёмной теме `Status_*Brush` осветлены, иначе контраст ниже
   4.5:1.
4. **Ресурсы локализации — в `Localization/`, не в `Resources/`.**
   `Resources/**` — зона Track E; строки фундамента вынесены отдельно.
5. **Журнал UI — в `%LOCALAPPDATA%`.** У обычного пользователя нет доступа к
   `DATADIR` службы; формат и защита от log injection соответствуют SECURITY.md.
6. **Тема хранится в состоянии окна** (`ui-state.json`), переключается
   `AppServices.SetTheme`.
7. **`FormComboBox` — только сеттеры.** Полный шаблон ComboBox не переопределялся:
   выпадающий список использует системные цвета. При необходимости Track E может
   запросить кастомный шаблон.

## Проверки

- `./scripts/verify.ps1 -Scope d` — успешно (build, test, format, schemas, smoke).
  Замечания: `clang-format` отсутствует; JSON-схемы ещё не добавлены (Track G);
  одно легаси-замечание `end_of_line/charset` в проекте UI (не мои файлы).
- `dotnet test ./tests/ui/WinSheduler.UI.Tests.csproj -c Release` — успешно,
  37 тестов (ViewModel, команды, конвертеры, локализация, IPC-фрейминг и ошибки,
  дашборд, состояние окна, дизайн-система, карта клавиатуры).
- `dotnet format whitespace` для UI — нарушений форматирования нет
  (кроме легаси-замечаний `end_of_line`).

## Осталось

- Добавить `./tests/ui/WinSheduler.UI.Tests.csproj` в решение и в `build/test.ps1`
  (запрос Integrator). Сейчас тесты UI запускаются вручную.
- После реализации B-3 подтвердить с Track B форму `SetSettings`/`GetSettings`
  и убрать legacy-слой IPC после перевода экранов Track E.
- Кастомный шаблон `FormComboBox` для полной поддержки тёмной темы (по запросу).
- Цветовые конвертеры возвращают кисть текущей темы; при смене темы список нужно
  обновить (Track E может использовать `DynamicResource`/триггеры).

## Блокеры

Нет. D-4 проверен на собственном тестовом канале: служба v2 (B-3) ещё не
реализована, интеграционный round-trip появится в I-3.
