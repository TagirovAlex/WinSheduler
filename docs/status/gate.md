# Статус локального gate

CI в проекте нет. Роль gate выполняет `./scripts/verify.ps1` и агент Gatekeeper.

## Правила

- Перед слиянием в `main` Gatekeeper запускает `./scripts/verify.ps1` на интеграционной ветке.
- Красный gate означает merge freeze: сначала исправление, потом слияние.
- Результат фиксируется ниже с датой и коммитом.

## Журнал

| Дата | Коммит | Область | Результат | Примечание |
|------|--------|---------|-----------|------------|
| 2026-11-01 | — | Phase 0 | успешно | `./scripts/verify.ps1 -Scope phase0`: build, test, format, схемы и smoke-тест службы пройдены. Предупреждения: clang-format не установлен (C++ не проверяется); легаси-несоответствие `end_of_line`/`charset` у существующих файлов; JSON-схемы пока отсутствуют. |
| 2026-11-01 | `48101d9` | Track G: контрактные тесты и gate | успешно | `./scripts/verify.ps1 -Scope g`: build, test, format, `contract-cpp` (25 тестов, 2431 проверка), `contract-csharp` (45 тестов), schemas, smoke. Отдельно `-CleanCheckout`: сборка снимка `git archive HEAD` зелёная. Предупреждения: clang-format не установлен; легаси `end_of_line`/`charset` в UI-проекте; JSON-схемы отсутствуют (запрос в `./docs/status/g-requests.md`). |
| 2026-11-01 | `762c227` | Track G: статический анализ и fuzz кадра | успешно | `./scripts/verify.ps1 -Scope g -Analyze`: все шаги зелёные, `contract-csharp` — 46 тестов (добавлен fuzz декодера кадра), MSVC `/analyze` без замечаний. `clang-tidy` отсутствует в окружении (предупреждение, не ошибка). |
| 2026-11-01 | `ba7f26d` | Полная интеграция A–G | успешно | `./scripts/verify.ps1 -Scope integration`: build, test, format, `contract-cpp` (25 тестов, 2431 проверка), `contract-csharp` (46 тестов), 8 JSON-схем, smoke. Отдельно: scheduler 180, DB 226, IPC 148, UI 37 тестов; `./scripts/installer/build.ps1 -SkipMsi` собирает службу и self-contained UI. UI запускается и живёт; исправлены `Border_Width` и видимость Tokens в `Controls.xaml`. WiX отсутствует — MSI не собран. |
| 2026-11-01 | `edd001f` | Активация `DatabaseOptions` | успешно | Служба передаёт `output_dir` и `require_secure_acl=true`; standalone — `output_dir`. Gate повторно зелёный. |
| 2026-11-01 | `a1b4c1f` | Установщик 2.0.0 и локализация | успешно | `./scripts/installer/build.ps1 -Version 2.0.0 -RequireWix`: MSI 54,1 МБ, `ProductLanguage=1049`, 50 из 51 строк `UIText` с кириллицей. Административная распаковка: `service/WinShedulerSvc.exe`, `ui/WinSheduler.UI.exe`, документы в корне `INSTALLDIR`. Основной gate и UI-тесты (37) зелёные. |
| 2026-11-01 | `ff7c02b` | UI automation smoke | успешно | `./tests/e2e/ui-smoke.ps1`: окно UI и кнопка «Добавить» найдены через UI Automation. Служба не установлена — приложение показывает офлайн-состояние и не падает. |
| 2026-11-01 | `bd8ec84` | UTF-8 в действиях установщика | успешно | В `WinShedulerCA.vcxproj` добавлен `/utf-8`; DLL содержит корректную UTF-16 строку «требует» вместо mojibake. MSI пересобран, SHA256 `39FA9DBF…658252D`, `ProductLanguage=1049`. |
| 2026-11-01 | `357b413` | Иконки из `./ico`, баннер и OS-check | успешно | Иконки 16 px размещены на кнопках; `app.ico` заменяет иконку приложения; `banner.bmp`/`dialog.bmp` подключены к WixUI. `RtlGetVersion` в msiexec возвращает 6.0.6000, добавлен резервный источник — реестр `CurrentBuildNumber` (19044); установщик доходит до `WelcomeDlg`. MSI 54,6 МБ, SHA256 `08A3E0AA…6D79`. |
| 2026-11-02 | `34d867f` | Иконки редактирования | успешно | В `./ico` добавлены `edit_task` (документ с карандашом) и `edit_schedule` (часы с карандашом), сгенерированы SVG/PNG 16–48/ICO через `make_assets.py`. Кнопки «Изменить» в главном окне и редакторе используют новые иконки. UI smoke зелёный. MSI 54,6 МБ, SHA256 `7EF1811F…F5C0`. |
| 2026-11-02 | `cb40b1b` | Проверка ОС: реестр и kernel32 | успешно | Основной источник — реестр (`CurrentMajorVersionNumber`/`CurrentBuildNumber`), резерв — версия `kernel32.dll`, `RtlGetVersion` — последний. Лог: `source=registry version=10.0 build=19044`, установщик доходит до `WelcomeDlg`. `ALLOWUNSUPPORTEDOS=1` — только для стенда. MSI 54,7 МБ, SHA256 `C687520C…D973`. |
