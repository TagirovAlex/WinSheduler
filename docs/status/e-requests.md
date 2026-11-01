# Запросы трека E к другим трекам

## Track D — срочно: дефект дизайн-системы ломает запуск UI

1. **`Border_Width` объявлен как `Double`, а используется как `Thickness`.**
   В `./src/WinSheduler.UI/Styles/Tokens.xaml`:
   `<sys:Double x:Key="Border_Width">1</sys:Double>`.
   В `./src/WinSheduler.UI/Styles/Controls.xaml` значение подставляется в
   `BorderThickness` (строки 125, 185, 237, 281, 357, 408). При применении любого
   стиля (`BaseButton`, `FormTextBox`, `FormPasswordBox`, `DataGridStyle`,
   `StatusBanner*`) возникает `XamlParseException`:
   `"1" не является допустимым значением для свойства "BorderThickness"`.
   Реальный запуск `WinSheduler.UI.exe` после merge падает в
   `DispatcherUnhandledException` (журнал `%LOCALAPPDATA%/WinSheduler/logs`).
   Исправление в зоне D: объявить `<Thickness x:Key="Border_Width">1</Thickness>`
   либо заменить `{StaticResource Border_Width}` на литерал `1` в `BorderThickness`.
   Пока дефект не исправлен, экраны E-1/E-2/E-9 нельзя проверить в реальном окне;
   визуальная проверка выполнялась с временной подменой ресурса в памяти.

2. **`FormLabel` нельзя применять к `TextBlock`.** Стиль имеет
   `TargetType="Label"`; для подписей-`TextBlock` нужен отдельный стиль
   (сейчас E использует `CaptionText`). Просьба добавить `FormLabelText`
   или задокументировать ограничение.

3. **`Styles/Tokens.xaml`: `Space_*` — это `Double`, а не `Thickness`.**
   Для `Margin`/`Padding` нужны отдельные `Thickness`-токены
   (`Margin_Panel`, `Padding_Panel` уже есть). E использует числовые литералы,
   чтобы не дублировать токены; после появления `Thickness`-токенов заменим.

4. **Legacy-слой IPC можно удалять.** Экраны E больше не вызывают
   `IpcClient.Send/SendAsync(IpcRequest)` и `CryptoHelper.Encrypt`; используются
   только `IIpcService`/`App.Services.Ipc`. После слияния B-3 слой
   `Send/SendAsync(IpcRequest)` можно удалить из `IpcClient`.

## Track B — служба v2 и права

1. **Служба всё ещё legacy.** В `main` нет обработчиков `GetDashboard`,
   `SetTaskEnabled`, `GetAudit`, `Ping`, v2-конверта и фрейминга. `IIpcService`
   отправляет кадры v2, поэтому на текущей службе главное окно и журнал
   показывают «Служба недоступна». Для приёмки E-1/E-4/E-5 нужны B-3…B-8 и B-12.
2. **`GetStatus`.** Нужны `isAdmin`, `isOperator`, `canManageSystemTasks`,
   `serviceVersion`, `startedAt`, `dbReady`; без них роль и бейдж
   «Требует админа» работают только по значениям по умолчанию.
3. **`GetAudit`.** Раздел аудита в диагностике готов (задел), но без B-7
   показывает «Аудит недоступен».
4. **Пути `DATADIR`/`INSTALLDIR`.** В `ServiceStatus` нет полей; окно
   диагностики показывает пояснение вместо путей. Нужны поля только для админа.
5. **Очистка пароля.** В UI есть кнопка «Очистить». Сейчас при явной очистке
   отправляется `password: ""`. Нужно подтвердить семантику: пустая строка =
   снять пароль, `null` = не менять. Иначе очистка не сработает.

## Architect

1. **`SpecificDates`.** Поле в `./src/WinSheduler.Shared` имеет тип
   `List<DateTime>`; UI передаёт полночь, `IpcDateTimeConverter` сериализует её
   как `yyyy-MM-dd`, что соответствует контракту. Просьба либо закрепить это
   правило в контракте, либо сменить тип на `List<string>`.
2. **`TimeWindowDto.Type` и `Kind`.** UI заполняет оба поля; после A-1 legacy
   `Type` можно удалить.

## Track A / Track C

1. **Время истории.** `scheduler.cpp::now_to_string()` пишет локальное время с
   суффиксом `Z`; UI по контракту трактует `Z` как UTC и переводит в локальное.
   До исправления возможно смещение на часовой пояс.
