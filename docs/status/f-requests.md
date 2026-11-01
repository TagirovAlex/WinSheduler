# Запросы трека F

## Track B — IPC и безопасность

1. **SID группы для SDDL канала.** Установщик пишет SID локальной группы
   `WinSheduler Users` в `HKLM/SOFTWARE/WinSheduler`, значение `GroupSid`
   (REG_SZ, строка вида `S-1-5-21-...`). Просьба использовать его при построении
   SDDL для `//./pipe/WinSheduler` (SECURITY.md §3), с fallback на
   `LookupAccountName` при отсутствии значения.
2. **Пути службы.** Там же доступны `InstallDir`, `DataDir`, `Version`.
   Служба получает `--dbpath`/`--logdir` аргументами, реестр — резервный
   источник для диагностики.

## Architect

1. **`OLDDATADIR`.** Добавлено публичное свойство MSI `OLDDATADIR` (каталог
   старой БД для миграции, F-9). Просьба внести в `./docs/contracts/INSTALL.md`
   §3 (таблица свойств).
2. **ACL логов.** `INSTALL.md` §5 допускает «Read (опционально)» для
   `DATADIR/logs`; реализовано «нет доступа» (как в `SECURITY.md` §7).
   Подтвердить или потребовать read для `Users`.
3. **Язык диалогов MSI.** Стандартный `WixUI_InstallDir` — английский.
   Русская локализация требует `.wxl`; в F-1…F-13 не входит. Подтвердить, что
   это допустимо для v1.

## Track G — QA и gate

1. **`verify.ps1 -Scope f`.** Сейчас scope зарезервирован и не влияет на шаги.
   Просьба для `-Scope f` добавлять:
   - `./scripts/installer/gen-components.ps1` на тестовом каталоге (проверка
     генератора);
   - сборку `./installer/ca/WinShedulerCA.vcxproj`;
   - `./scripts/installer/build.ps1 -SkipMsi` (публикация бинарников);
   - при наличии WiX — `./scripts/installer/build.ps1 -RequireWix`.
2. **Smoke-тест в gate.** `./scripts/installer/smoke-test.ps1` без ключей
   безопасен (только чтение), но на машине без установки даёт FAIL. В gate его
   включать не нужно; использовать на ВМ.

## Track C — хранение

1. **Миграция v1→v2.** F-9 копирует старую БД в `DATADIR/data/scheduler.db` и
   полагается на автоматическую миграцию схемы при первом старте службы
   (решение 10 плана, C-7). Просьба подтвердить, что миграция идемпотентна и
   выполняется до открытия IPC.

## Integrator

1. **Версия.** `./Directory.Build.props` содержит 1.2.0, ближайший git-тег —
   v1.1.3. Установщик берёт версию из тега и передаёт её в `dotnet publish`
   (`Version/FileVersion/AssemblyVersion`). Для релиза нужен тег `vX.Y.Z`;
   иначе MSI и сборка по умолчанию разойдутся.
2. **Документация.** Обновить `./README.md`/`./SPECIFICATION.md`: установка
   теперь через `./artifacts/WinSheduler.msi`, `./deploy.bat` выводится из
   production-процесса (ADR 0002). Файлы вне зоны трека F.
3. **Решение.** Добавлять ли `./installer/ca/WinShedulerCA.vcxproj` в
   `./WinSheduler.sln`? Сейчас собирается отдельным вызовом MSBuild из
   `build.ps1`; для IDE-удобства можно добавить, но это правка общего файла.

## Track E / D — UI

1. При первом запуске после установки служба может стартовать с задержкой
   (delayed auto start). UI должен показывать состояние «служба недоступна»
   с retry (E-6), а не падать.
