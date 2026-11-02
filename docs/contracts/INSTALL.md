# Контракт установки

Статус: заморожен. Изменения — только через ADR.

## 1. Решения

- Технология: WiX Toolset, MSI.
- UI: self-contained publish (`win-x64`), .NET Runtime не требуется.
- Служба: C++ `/MT`, VC++ Redist не требуется.
- SQLite: встроен.
- Подпись: сертификата нет, шаг подписи — заглушка, выпускается SHA256-манифест.
- CI: нет, сборка локальная через `./scripts/installer/build.ps1`.

## 2. Раскладка

```
%ProgramFiles%/WinSheduler/
  service/WinShedulerSvc.exe
  ui/WinSheduler.UI.exe
  ui/*.dll
  ui/*.json
  LICENSE.txt
  THIRD-PARTY-NOTICES.txt

%ProgramData%/WinSheduler/
  data/scheduler.db
  logs/scheduler.log
  logs/output/<taskId>/<historyId>.log
```

- `INSTALLDIR` по умолчанию — `%ProgramFiles%/WinSheduler`.
- `DATADIR` по умолчанию — `%ProgramData%/WinSheduler`.
- Оба пути можно переопределить при установке.

## 3. Свойства MSI

| Свойство | По умолчанию | Назначение |
|----------|--------------|------------|
| `INSTALLDIR` | `%ProgramFiles%/WinSheduler` | каталог программы |
| `DATADIR` | `%ProgramData%/WinSheduler` | БД и логи |
| `CREATEGROUP` | `1` | создать локальную группу `WinSheduler Users` |
| `ADDINSTALLERTOGROUP` | `1` | добавить устанавливающего пользователя в группу |
| `STARTUI` | `0` | запустить UI после установки |
| `KEEPDATA` | `1` | сохранить данные при удалении |
| `ALLOWUNSAFEPATH` | `0` | разрешить небезопасный `INSTALLDIR` (только лаборатория) |
| `ALLOWUNSUPPORTEDOS` | `0` | пропустить проверку версии ОС (только лаборатория/стенд) |
| `OLDDATADIR` | пусто | каталог старой БД для миграции; при пустом значении выполняется автоопределение |

Свойства публичные (UPPERCASE), доступны для silent install и GPO.

## 4. Валидация `INSTALLDIR`

Установщик обязан проверить:

1. Путь локальный, фиксированный диск; UNC, сетевые диски, `//?/`, `//./` запрещены.
2. Путь не является корнем диска.
3. Путь не находится в профиле пользователя, `%TEMP%`, `%USERPROFILE%/Downloads`.
4. В пути нет `..`, пустых сегментов, недопустимых символов.
5. Длина пути — в пределах 200 символов (запас под `ui/` и `logs/`).
6. Каталог и его родитель не доступны не-администраторам на:
   - `FILE_ADD_FILE`;
   - `FILE_ADD_SUBDIRECTORY`;
   - `FILE_DELETE_CHILD`;
   - `WRITE_DAC`;
   - `WRITE_OWNER`.
7. Если проверка не пройдена — установка прерывается с сообщением. Обход только `ALLOWUNSAFEPATH=1` с явным предупреждением.

`DATADIR` проверяется аналогично, но требования к родителю мягче: важны `WRITE_DAC`/`WRITE_OWNER` и запись для не-админов.

## 5. ACL

| Каталог | SYSTEM | Administrators | Users |
|---------|--------|----------------|-------|
| `INSTALLDIR` | Full | Full | Read + Execute |
| `INSTALLDIR/service` | Full | Full | Read + Execute |
| `INSTALLDIR/ui` | Full | Full | Read + Execute |
| `DATADIR` | Full | Full | нет доступа |
| `DATADIR/data` | Full | Full | нет доступа |
| `DATADIR/logs` | Full | Full | нет доступа |

ACL выставляется установщиком явно и повторно применяется при repair/upgrade. Наследуемые права, дающие запись не-админам, удаляются.

Доступ к логам — только через администратора или службу. Это совпадает с `./docs/contracts/SECURITY.md` §7 и выбрано осознанно: в логах могут быть пути и аргументы задач.

## 6. Служба

| Параметр | Значение |
|----------|----------|
| Имя | `WinSheduler` |
| DisplayName | `WinSheduler Task Scheduler` |
| Тип | own process |
| Старт | auto, delayed |
| Учётная запись | `LocalSystem` |
| Аргументы | `--dbpath "%DATADIR%/data/scheduler.db" --logdir "%DATADIR%/logs"` |
| Recovery | restart 5 с / 30 с / 60 с, reset 1 день |
| Управление | `SERVICE_ACCEPT_STOP`, `SERVICE_ACCEPT_SHUTDOWN`, `SERVICE_ACCEPT_PRESHUTDOWN` |
| Удаление | stop + delete при uninstall |

Порядок установки: создать `DATADIR` и ACL → установить файлы → создать группу → зарегистрировать службу → запустить службу.

## 7. Группа

- Локальная группа `WinSheduler Users`, без прав администратора.
- Создаётся при `CREATEGROUP=1`.
- Устанавливающий пользователь добавляется при `ADDINSTALLERTOGROUP=1`.
- SID группы подставляется в SDDL pipe (см. `./docs/contracts/SECURITY.md`).
- При удалении группа удаляется, если `KEEPDATA=0`; иначе сохраняется.

## 8. Сборка

Все команды — из корня проекта, без `cd`.

```powershell
# Финальный комплект: служба + self-contained UI + MSI + SHA256 в ./published
./publish.ps1

# Промежуточная сборка без MSI
./scripts/installer/build.ps1 -SkipMsi -OutputDir published
```

Требования к сборке:

- Visual Studio 2022 Build Tools (MSVC v143, Windows 10 SDK).
- .NET 8 SDK.
- WiX Toolset (`dotnet tool install --global wix`).
- Опционально: `signtool` (если появится сертификат).
- Версия пакета — из git-тега `vX.Y.Z`; при отсутствии тега используется `2.0.0` из `./Directory.Build.props`.

## 9. Установка

Интерактивно: запустить `./published/WinSheduler.msi`.

Silent install (абсолютный путь — только как аргумент внешней утилиты, прямые слэши):

```bat
msiexec /i ./published/WinSheduler.msi INSTALLDIR="d:/Apps/WinSheduler" DATADIR="d:/WinShedulerData" /qn /l*v ./temp/install.log
```

Silent uninstall:

```bat
msiexec /x ./published/WinSheduler.msi /qn
```

## 10. Обновление и удаление

- Major upgrade, `Schedule="afterInstallValidate"`, блокировка downgrade.
- `DATADIR` не входит в компоненты MSI: данные не удаляются при upgrade.
- При uninstall по умолчанию данные сохраняются (`KEEPDATA=1`). Удаление данных — только явный выбор пользователя.
- Repair повторно применяет ACL и перезапускает службу.
- Пользовательский `INSTALLDIR` не удаляется рекурсивно: удаляются только файлы, установленные MSI.

## 11. Миграция со старой раскладки

Если найден `./published/service/data/scheduler.db` или старый каталог данных:

1. Остановить службу.
2. Скопировать БД в `DATADIR/data/` с резервной копией.
3. Запустить службу: миграция схемы v1→v2 выполнится автоматически.
4. Старый файл не удалять (оставить на усмотрение администратора).

## 11.1. Нормализация путей в реестре

Старые версии писали `InstallDir`/`DataDir` с прямыми слэшами. Windows Installer
считает такой путь сетевым и блокирует удаление/обновление.

- `NormalizePaths` нормализует `INSTALLDIR`/`DATADIR`/`OLDDATADIR` до `CostFinalize`.
- `FixRegistryPaths` исправляет старые значения в `HKLM/SOFTWARE/WinSheduler`
  перед `RemoveExistingProducts`.
- Новые значения записываются с обратными слэшами.

## 12. Подпись

- Если задан сертификат: подписать exe и MSI через `signtool`, timestamp SHA256.
- Если сертификата нет: шаг пропускается с предупреждением, формируется `./published/SHA256SUMS.txt`.
- Документировать предупреждение SmartScreen и способы обхода в enterprise (GPO/Intune).

## 13. Smoke-тест на чистой ВМ

- [ ] Установка на Win10 1809+ / Server 2019+ без .NET Runtime.
- [ ] Служба запущена, `GetStatus` отвечает.
- [ ] UI запускается у пользователя из группы без elevation.
- [ ] Задача создаётся, запускается от своего имени, история пишется.
- [ ] Задача от SYSTEM доступна только админу.
- [ ] `DATADIR` и `INSTALLDIR` имеют ожидаемые ACL.
- [ ] Перезагрузка: служба стартует, задачи сохранились.
- [ ] Upgrade: данные и задачи сохранились.
- [ ] Uninstall: данные сохранены, служба удалена.
- [ ] Кастомный `INSTALLDIR` на другом диске: установка успешна, ACL корректны.
- [ ] Небезопасный `INSTALLDIR` (каталог, доступный на запись пользователям): установка отклонена.

## 14. Ограничения

- MSIX не используется: не поддерживает Windows-службы.
- Установка per-user не поддерживается: служба требует per-machine.
- Portable-режим не поддерживается.
- Диалоги MSI локализованы на русский: `./installer/ru-RU.wxl`, `-culture ru-RU`, `Package/@Codepage="1251"`, `ProductLanguage=1049`. Отсутствующие строки берутся из английского fallback `WixUI`.
- Проект `./installer/ca/WinShedulerCA.vcxproj` собирается отдельным вызовом MSBuild; в `./WinSheduler.sln` не входит, чтобы не усложнять основную сборку.
