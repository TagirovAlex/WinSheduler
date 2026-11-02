# Установщик WinSheduler

MSI-пакет (WiX Toolset 5), установка per-machine. Контракт:
`./docs/contracts/INSTALL.md`, решение: `./docs/adr/0002-installer-and-layout.md`.

## 1. Состав

```
./installer/
  Package.wxs                  свойства, каталоги, feature, UI, последовательности
  Components.wxs               служба, ярлык, документы, DLL действий
  WinSheduler.Installer.wixproj  проект для IDE/dotnet build
  License.rtf                  лицензия для диалога MSI
  LICENSE.txt                  лицензия в INSTALLDIR
  THIRD-PARTY-NOTICES.txt      уведомления сторонних компонентов
  ca/ca.cpp                    нативные действия (validate, ACL, служба, миграция)
  ca/ca.def, ca/*.vcxproj      сборка DLL действий (x64, /MT)

./scripts/installer/
  build.ps1                    сборка службы, UI, MSI, подпись, SHA256
  gen-components.ps1           генерация компонентов UI (замена heat, без сети)
  smoke-test.ps1               smoke-тест установки на чистой ВМ

./published/
  WinSheduler.msi              пакет
  SHA256SUMS.txt               хеши MSI и бинарников
  service/WinShedulerSvc.exe   служба (для справки/отладки)
  ui/...                       self-contained публикация UI
```

## 2. Требования к сборочной машине

- Windows 10 1809+ / Server 2019+.
- Visual Studio 2022 Build Tools (MSVC v143, Windows 10 SDK).
- .NET 8 SDK.
- WiX Toolset 5:

```powershell
dotnet tool install --global wix
wix extension add -g WixToolset.UI.wixext
```

`signtool` нужен только при наличии сертификата подписи.

## 3. Сборка

Все команды — из корня проекта.

```powershell
# MSI + манифест SHA256
./scripts/installer/build.ps1

# Конкретная версия (по умолчанию — из git-тега vX.Y.Z)
./scripts/installer/build.ps1 -Version 1.2.0

# С ReadyToRun (быстрее старт UI, больше размер)
./scripts/installer/build.ps1 -ReadyToRun

# Только бинарники в ./published (без MSI)
./publish.ps1
```

Поведение:

- версия берётся из ближайшего git-тега (`v1.2.3` → `1.2.3`); тег должен иметь вид
  `vX.Y.Z`, иначе сборка падает;
- `./published/**` не изменяется и не удаляется;
- временные файлы — только `./temp/installer/`;
- если WiX не установлен, сборка MSI пропускается с предупреждением (бинарники
  публикуются). Для строгого режима: `-RequireWix`;
- бюджет размера MSI — 80 МБ (ADR 0002). Превышение — предупреждение, с
  `-EnforceBudgets` — ошибка.

**Статус проверки:** в окружении трека F WiX отсутствовал, поэтому сборка MSI
не проверялась. Проверены: компиляция `WinShedulerCA.dll`, генерация компонентов
UI, синтаксис скриптов. Команда для проверки на машине с WiX:
`./scripts/installer/build.ps1 -RequireWix`.

## 4. Свойства MSI

Все свойства публичные (UPPERCASE), пригодны для silent install, MST и Intune.

| Свойство | По умолчанию | Назначение |
|----------|--------------|------------|
| `INSTALLDIR` | `%ProgramFiles%/WinSheduler` | каталог программы |
| `DATADIR` | `%ProgramData%/WinSheduler` | БД и логи |
| `CREATEGROUP` | `1` | создать группу `WinSheduler Users` |
| `ADDINSTALLERTOGROUP` | `1` | добавить устанавливающего пользователя в группу |
| `STARTUI` | `0` | запустить UI после установки |
| `KEEPDATA` | `1` | сохранить данные при удалении |
| `ALLOWUNSAFEPATH` | `0` | отключить проверку ACL путей (только лаборатория) |
| `OLDDATADIR` | не задано | каталог старой БД для миграции (см. раздел 9) |

`INSTALLDIR` и `DATADIR` запоминаются в реестре (`HKLM/SOFTWARE/WinSheduler`) и
подставляются при repair/upgrade. Значения из командной строки имеют приоритет.

## 5. Тихая установка

```powershell
msiexec /i ./published/WinSheduler.msi /qn /l*v ./temp/install.log

msiexec /i ./published/WinSheduler.msi /qn INSTALLDIR="d:/Apps/WinSheduler" DATADIR="d:/WinShedulerData" /l*v ./temp/install.log

msiexec /x ./published/WinSheduler.msi /qn /l*v ./temp/uninstall.log
```

Коды возврата стандартные: `0` — успех, `3010` — нужна перезагрузка,
`1603` — фатальная ошибка (смотреть лог).

Удаление данных — только явно:

```powershell
msiexec /x ./published/WinSheduler.msi /qn KEEPDATA=0
```

`KEEPDATA=0` удаляет `DATADIR` только при наличии маркера `.winsheduler-data`,
созданного установщиком. Это защита от удаления чужого каталога.

## 6. Развёртывание через GPO и Intune

### GPO (групповая политика)

GPO «Software Installation» не передаёт свойства MSI. Варианты:

1. Собрать MST с нужными свойствами (Orca или `msitran`) и опубликовать
   MSI+MST парой.
2. Развернуть через startup-скрипт GPO:

```bat
msiexec /i "//fileserver/share/WinSheduler.msi" /qn /l*v "%TEMP%/winsheduler-install.log"
```

Для установки в тихом режиме под SYSTEM свойство `ADDINSTALLERTOGROUP`
бессмысленно (пользователя нет) — группу наполняет администратор.

### Intune

Win32-приложение:

- install: `msiexec /i "WinSheduler.msi" /qn /l*v "%TEMP%/winsheduler-install.log"`
- uninstall: `msiexec /x "{ProductCode}" /qn` (или `msiexec /x "WinSheduler.msi" /qn`);
- detection: MSI product code или ключ реестра
  `HKLM/SOFTWARE/WinSheduler` (значение `Version`);
- требования: Windows 10 1809+, x64, права SYSTEM (по умолчанию для Win32-приложений).

Для кастомных путей добавьте свойства в командную строку установки.

## 7. Администратору

### Раскладка

```
%ProgramFiles%/WinSheduler/service/WinShedulerSvc.exe
%ProgramFiles%/WinSheduler/ui/WinSheduler.UI.exe
%ProgramFiles%/WinSheduler/LICENSE.txt
%ProgramFiles%/WinSheduler/THIRD-PARTY-NOTICES.txt
%ProgramData%/WinSheduler/data/scheduler.db
%ProgramData%/WinSheduler/logs/scheduler.log
%ProgramData%/WinSheduler/logs/output/<taskId>/<historyId>.log
```

### ACL

| Каталог | SYSTEM | Administrators | Users |
|---------|--------|----------------|-------|
| `INSTALLDIR`, `service`, `ui` | Full | Full | Read + Execute |
| `DATADIR`, `data`, `logs` | Full | Full | нет доступа |

ACL защищённые (наследование отключено), повторно применяются при repair/upgrade.
Права `Users` на запись удаляются.

### Служба

| Параметр | Значение |
|----------|----------|
| Имя | `WinSheduler` |
| Отображаемое имя | `WinSheduler Task Scheduler` |
| Учётная запись | `LocalSystem` |
| Старт | auto, delayed |
| Аргументы | `--dbpath "<DATADIR>/data/scheduler.db" --logdir "<DATADIR>/logs"` |
| Recovery | restart 5/30/60 с, reset 1 день |
| Preshutdown | таймаут 60 с |

Управление: `sc query WinSheduler`, `sc stop WinSheduler`, `sc start WinSheduler`.
Удаление — только через MSI (`msiexec /x`), иначе останутся файлы и ACL.

### Группа

Локальная группа `WinSheduler Users` даёт только право подключаться к IPC. Она
не даёт прав администратора и не позволяет запускать задачи от SYSTEM.
SID группы записывается в `HKLM/SOFTWARE/WinSheduler` (`GroupSid`) для SDDL
именованного канала.

Добавление пользователя вручную:

```powershell
Add-LocalGroupMember -Group 'WinSheduler Users' -Member 'DOMAIN/user'
```

### Проверка безопасности путей

Установщик отклоняет `INSTALLDIR`/`DATADIR`, если:

- путь не локальный фиксированный диск (UNC, сетевые диски, `//?/`, `//./` запрещены);
- это корень диска;
- путь в профиле пользователя, `%TEMP%` или `Downloads`;
- есть `..`, пустые сегменты или недопустимые символы;
- длина больше 200 символов;
- каталог или его родитель доступны не-администраторам на запись/смену ACL
  (для родителя `DATADIR` достаточно запрета удаления дочерних объектов и смены ACL).

Обход — только `ALLOWUNSAFEPATH=1` (лаборатория), с предупреждением в логе MSI.

## 8. Обновление, repair и удаление

- Major upgrade: `Schedule=afterInstallValidate`, downgrade блокируется
  (`DowngradeErrorMessage`).
- `DATADIR` не входит в компоненты MSI: данные при upgrade не удаляются.
- Repair (Программы и компоненты → Изменить → Восстановить) повторно применяет
  ACL, аргументы службы и recovery-политику.
- Uninstall по умолчанию (`KEEPDATA=1`) сохраняет `DATADIR` и группу.
- Пользовательский `INSTALLDIR` не удаляется рекурсивно: удаляются только
  файлы, установленные MSI.

## 9. Миграция со старой раскладки

Старая установка (`./deploy.bat`) хранила БД рядом со службой, например
`./published/service/data/scheduler.db`. Порядок:

1. Остановить старую службу: `sc stop WinSheduler`.
2. Установить MSI с указанием старого каталога данных:

```powershell
msiexec /i ./published/WinSheduler.msi /qn OLDDATADIR="c:/OldWinSheduler/published/service/data"
```

3. Установщик скопирует `scheduler.db` в `DATADIR/data/`. Если целевая БД уже
   существует, она не перезаписывается: рядом создаётся
   `scheduler.db.preinstall.bak`.
4. Старый файл не удаляется. Схема v1→v2 мигрирует при первом старте службы.

Без `OLDDATADIR` установщик проверяет `%ProgramData%/WinSheduler/service/data`
и `%ProgramData%/WinSheduler/legacy/data`.

## 10. Подпись и SHA256

- Сертификата нет: шаг подписи — заглушка, выводится предупреждение.
- Обязательный `./published/SHA256SUMS.txt` содержит хеши MSI, службы и UI.
- Проверка: `Get-FileHash ./published/WinSheduler.msi -Algorithm SHA256`.
- SmartScreen будет предупреждать о неизвестном издателе. В enterprise:
  подпись сертификатом организации, Intune/GPO-политика SmartScreen,
  добавление хеша в allow-list.
- При появлении сертификата:

```powershell
./scripts/installer/build.ps1 -CertificatePath ./temp/cert.pfx
```

Пароль передаётся только параметром `-CertificatePassword` и не логируется.

### Внутренний AD CA или публичный CA

- Сертификат внутреннего УЦ AD подходит, если у него есть EKU **Code Signing**
  (`1.3.6.1.5.5.7.3.3`) и целевые машины доверяют этому УЦ. Для доменных машин
  подпись будет доверенной; для внешних пользователей и SmartScreen — нет.
- Обычный пользовательский сертификат (Client Authentication, Smart Card Logon)
  для подписи кода не подходит.
- Для публикации на GitHub и внешних пользователей нужен публичный
  OV/EV Code Signing сертификат. EV быстрее формирует репутацию SmartScreen.
- Подписываются служба, UI, DLL действий и MSI; используется SHA256 и timestamp.

## 10.1. Локализация MSI

- Русские строки WixUI — `./installer/ru-RU.wxl`, кодировка MSI `1251`.
- Сборка передаёт `-culture ru-RU`; `ProductLanguage` пакета — `1049`.
- Отсутствующие строки берутся из английского fallback WixUI.
- Для другого языка добавьте `./installer/<culture>.wxl` и культуру в `build.ps1`.

## 11. Smoke-тест на чистой ВМ

```powershell
# Проверки уже установленного продукта
./scripts/installer/smoke-test.ps1

# Установка, проверки, замер старта UI
./scripts/installer/smoke-test.ps1 -Install -MeasureUiStartup

# Удаление (данные сохраняются)
./scripts/installer/smoke-test.ps1 -Uninstall
```

Скрипт проверяет: MSI и SHA256, службу и её аргументы, delayed auto start,
группу, файлы, реестр, ACL, маркер данных, бюджет MSI и (опционально) время
старта UI. Чеклист — раздел 13 `./docs/contracts/INSTALL.md`.

## 12. Бюджеты

| Метрика | Бюджет | Контроль |
|---------|--------|----------|
| Размер MSI | 80 МБ | `build.ps1` (предупреждение/`-EnforceBudgets`) |
| Старт UI до окна | 5 с | `smoke-test.ps1 -MeasureUiStartup` |

Self-contained UI даёт пакет 60–80 МБ — это принято ADR 0002.

## 12.1. Проверка версии ОС

- Основной источник — реестр `HKLM/SOFTWARE/Microsoft/Windows NT/CurrentVersion`
  (`CurrentMajorVersionNumber`, `CurrentBuildNumber`); он не подвержен
  compatibility shim.
- Резерв — версия `kernel32.dll`; последний резерв — `RtlGetVersion`.
- В процессе `msiexec` `RtlGetVersion` может вернуть 6.0.6000; это не версия ОС,
  поэтому она не используется как основной источник.
- Для стенда с неподдерживаемой ОС: `ALLOWUNSUPPORTEDOS=1` (только для тестов).

## 12.2. Старые версии и пути в реестре

Старые сборки записывали `InstallDir`/`DataDir` с прямыми слэшами
(`C:/ProgramData/...`). Windows Installer считает такой путь сетевым и при
удалении/обновлении выдаёт «Не удалось получить доступ к расположению в сети».

Новый MSI:

- нормализует `INSTALLDIR`/`DATADIR` до `CostFinalize` (`NormalizePaths`);
- исправляет старые значения в `HKLM/SOFTWARE/WinSheduler` перед
  `RemoveExistingProducts` (`FixRegistryPaths`);
- записывает новые значения с обратными слэшами.

Если старая версия не удаляется через «Программы и компоненты», установите
новый MSI поверх: он исправит пути и выполнит major upgrade. После этого
удаление работает штатно.

## 12.3. Обновление поверх установленной версии

- `MajorUpgrade` разрешает **same-version upgrade**: сборку 2.0.2 можно
  поставить поверх 2.0.2 с другим ProductCode.
- Перед заменой файлов действие `CloseUi` закрывает запущенный
  `WinSheduler.UI.exe`, служба останавливается через `ServiceControl`.
- Для production версию всё равно нужно поднимать: `2.0.2`, `2.0.3`, …
- Если UI был запущен с несохранёнными изменениями, установщик закроет его
  без дополнительного вопроса.

## 13. Ограничения

- Диалоги MSI локализованы на русский (`./installer/ru-RU.wxl`, `-culture ru-RU`);
  отсутствующие строки берутся из английского fallback WixUI.
- Per-user установка и portable-режим не поддерживаются: служба требует
  per-machine.
- MSIX не используется: не поддерживает Windows-службы.
- `STARTUI=1` запускает UI отложенным действием в контексте устанавливающего
  пользователя; при установке под SYSTEM (GPO/Intune) UI не запускается.
