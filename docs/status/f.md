# Статус трека F — установщик

## Текущая задача

F-1…F-13: реализованы в коде; сборка MSI не проверена — WiX в окружении
отсутствует, установка из сети не выполнялась (запрещено без разрешения).

## Выполнено

### F-1. WiX-проект, версия из git-тега

- `./installer/WinSheduler.Installer.wixproj` — WiX 5 (`WixToolset.Sdk/5.0.2`),
  расширение `WixToolset.UI.wixext`.
- `./installer/Package.wxs` — `Package` (per-machine, `InstallerVersion=500`),
  `MajorUpgrade` (`afterInstallValidate`, блокировка downgrade), `MediaTemplate`
  со сжатием `high`, свойства, каталоги, feature, `WixUI_InstallDir`,
  последовательности.
- `./installer/Components.wxs` — служба, ярлык, документы, `Binary` + `CustomAction`.
- `./installer/License.rtf`, `./installer/LICENSE.txt`,
  `./installer/THIRD-PARTY-NOTICES.txt`.
- `./scripts/installer/build.ps1` — версия из ближайшего git-тега (`v1.2.3` → `1.2.3`),
  проверка формата `x.y.z`, сборка службы, публикация UI, сборка DLL действий,
  генерация компонентов, `wix build`, подпись/манифест, бюджет размера.
- Список файлов UI генерируется `./scripts/installer/gen-components.ps1`
  (466 компонентов, 13 подкаталогов культур на текущей публикации), GUID
  детерминированы (MD5 относительного пути) — heat и сеть не нужны.

### F-2. INSTALLDIR и валидация

- `./installer/ca/ca.cpp`, действие `ValidatePaths`:
  локальный фиксированный диск, запрет UNC/device/относительных путей,
  запрет корня диска, `..`, `.`, пустых сегментов и недопустимых символов,
  длина ≤ 200, запрет профиля, `%TEMP%`, `Downloads`;
  проверка ACL каталога и родителя по well-known SID (`Users`, `Authenticated Users`)
  на `FILE_ADD_FILE`, `FILE_ADD_SUBDIRECTORY`, `FILE_DELETE_CHILD`, `WRITE_DAC`,
  `WRITE_OWNER`; для родителя `DATADIR` — мягкий набор (без `AD/WD`, иначе
  штатный `%ProgramData%` не прошёл бы).
- Проверка версии ОС через `RtlGetVersion` (build ≥ 17763).
- `ALLOWUNSAFEPATH=1` отключает ACL-проверку с предупреждением в логе MSI,
  синтаксис пути проверяется всегда.

### F-3. DATADIR и ACL

- `DATADIR` по умолчанию `%ProgramData%/WinSheduler`, читается из реестра при
  repair/upgrade, переопределяется свойством.
- Действие `ApplySecurity` (deferred, SYSTEM) создаёт `data`, `logs`,
  `logs/output`, выставляет защищённые DACL через SDDL:
  `SYSTEM`/`Administrators` full; `Users` read+execute только для каталогов
  программы. Повторно применяется при repair/upgrade.
- Маркер `DATADIR/.winsheduler-data` — защита от удаления чужого каталога.
- В реестр `HKLM/SOFTWARE/WinSheduler` пишутся `InstallDir`, `DataDir`,
  `GroupSid`, `Version`.

### F-4. Служба

- `ServiceInstall`: `WinSheduler`, `WinSheduler Task Scheduler`, own process,
  auto, LocalSystem.
- `ConfigureService` задаёт командную строку
  `--dbpath "<DATADIR>/data/scheduler.db" --logdir "<DATADIR>/logs"`,
  delayed auto start, recovery 5/30/60 с (reset 1 день), preshutdown 60 с.
- `ServiceControl`: start при установке, stop при install/uninstall, delete при
  uninstall, `Wait=yes`.

### F-5. Группа и SDDL

- Локальная группа `WinSheduler Users` создаётся при `CREATEGROUP=1`.
- `ADDINSTALLERTOGROUP=1` добавляет устанавливающего пользователя (кроме
  SYSTEM/машинной учётной записи).
- SID группы пишется в `HKLM/SOFTWARE/WinSheduler` (`GroupSid`) — источник для
  SDDL канала в треке B.

### F-6. Self-contained UI и сжатие

- `dotnet publish -c Release -r win-x64 --self-contained true` в
  `./temp/installer/ui`, копия в `./artifacts/ui`; `-p:Version/FileVersion/AssemblyVersion`
  выравниваются по версии пакета.
- `-ReadyToRun` — опционально.
- `MediaTemplate CompressionLevel="high"`.

### F-7. Ярлыки, ARP, repair, uninstall

- Ярлык в меню «Пуск» (`ProgramMenuFolder/WinSheduler`), удаляется при uninstall.
- ARP: `ARPPRODUCTICON`, `ARPCOMMENTS`, имя/издатель/версия.
- Repair повторно применяет ACL и параметры службы.
- Uninstall по умолчанию сохраняет данные и группу (`KEEPDATA=1`).

### F-8. Major upgrade и downgrade

- `MajorUpgrade Schedule="afterInstallValidate"`, `DowngradeErrorMessage`.
- `DATADIR` не входит в компоненты MSI.
- `RemoveData` не выполняется при `UPGRADINGPRODUCTCODE`, поэтому данные
  сохраняются при major upgrade.

### F-9. Миграция со старой раскладки

- Действие `MigrateData`: свойство `OLDDATADIR` или автоопределение
  `%ProgramData%/WinSheduler/service/data` и `.../legacy/data`.
- БД копируется в `DATADIR/data/scheduler.db`; существующая целевая БД не
  перезаписывается (рядом создаётся `.preinstall.bak`), исходный файл не удаляется.

### F-10. Подпись и SHA256

- Заглушка: без сертификата выводится предупреждение, подпись пропускается.
- С сертификатом: `signtool` для UI, службы и MSI (SHA256, timestamp).
- `./artifacts/SHA256SUMS.txt` формируется всегда при сборке MSI.

### F-11. Smoke-тест

- `./scripts/installer/smoke-test.ps1`: MSI и хеш, служба/аргументы/delayed
  auto start, группа, файлы, реестр, ACL, маркер данных, бюджет MSI,
  опционально время старта UI. Установка/удаление — только по явным ключам.

### F-12. Документация

- `./installer/README.md`: свойства, silent install/uninstall, GPO/Intune,
  админ-гайд, ACL, миграция, подпись, бюджеты, ограничения.

### F-13. Бюджеты

- Размер MSI: 80 МБ (ADR 0002), контроль в `build.ps1`
  (предупреждение, `-EnforceBudgets` — ошибка).
- Старт UI: 5 с, контроль `smoke-test.ps1 -MeasureUiStartup`.

### publish.ps1

- Переписан: не удаляет `./published`, публикует в `./artifacts` через
  `./scripts/installer/build.ps1 -SkipMsi`, убрано скачивание sqlite из сети.

## В работе

Ничего.

## Осталось

- Собрать MSI на машине с WiX и проверить установку:
  `dotnet tool install --global wix`, `wix extension add -g WixToolset.UI.wixext`,
  `./scripts/installer/build.ps1 -RequireWix`.
- Прогнать `./scripts/installer/smoke-test.ps1 -Install -MeasureUiStartup` на
  чистой Win10 1809+/Server 2019+ и полный чеклист §13 `INSTALL.md`.
- Проверить major upgrade и uninstall с `KEEPDATA=0` на ВМ.
- Русская локализация диалогов MSI (сейчас стандартные английские WixUI).
- Track G: включить шаги установщика в `verify.ps1 -Scope f`.

## Проверки

- `./scripts/verify.ps1 -Scope f` — успешно (build, test, format, schemas, smoke).
- `./scripts/installer/build.ps1 -SkipMsi` — успешно: служба собрана, UI
  self-contained опубликован (466 файлов UI, ~160 МБ), версия из тега `1.1.3`.
- `./scripts/installer/build.ps1` — успешно до шага WiX: DLL действий собрана,
  компоненты UI сгенерированы (466), MSI пропущен с предупреждением.
- `WinShedulerCA.dll` (x64, /MT) — компиляция MSVC v143 без предупреждений.
- `gen-components.ps1` — сгенерированный `.wxs` корректный XML, 466 компонентов.
- `smoke-test.ps1` — синтаксис и логика проверены на машине без установки
  (ожидаемые FAIL по отсутствующим компонентам, без исключений).
- XML-файлы (`*.wxs`, `*.wixproj`, `*.vcxproj`) — well-formed.

## Блокеры

- WiX Toolset отсутствует. Установка из сети без разрешения запрещена, поэтому
  `wix build` не выполнялся. Команда: `dotnet tool install --global wix`.

## Коммит

`8f603e9` в worktree `./temp/worktrees/f` (ветка `track/f`), 14 файлов.

## Расхождения с контрактами

1. Добавлено публичное свойство `OLDDATADIR` (F-9); в `INSTALL.md` §3 его нет.
   Запрос Architect — см. `./docs/status/f-requests.md`.
2. ACL `DATADIR/logs`: контракт допускает «Read (опционально)» для `Users`;
   выбрано «нет доступа» (совпадает с `SECURITY.md` §7). Доступ к логам — через
   администратора/службу.
3. `STARTUI=1` реализован отложенным действием с имперсонацией перед
   `InstallFinalize`; при установке под SYSTEM UI не запускается.
4. Порядок установки: файлы (`InstallFiles`) идут после создания `DATADIR`,
   ACL и группы (`ApplySecurity Before="InstallFiles"`), затем служба —
   соответствует §6 `INSTALL.md`.

## Запросы к другим трекам

См. `./docs/status/f-requests.md`.
