# Карта иконок и изображений установщика

Источник: `./ico/`.

## Установщик

| Файл | Назначение | Размер |
|------|------------|--------|
| `./ico/installer/banner.bmp` | `WixUIBannerBmp` | 493 × 58 |
| `./ico/installer/dialog.bmp` | `WixUIDialogBmp` | 493 × 312 |
| `./ico/installer/app.ico` | `UiIcon`, `ARPPRODUCTICON`, иконка приложения | 256 × 256 |
| `./ico/logo/logo.png` | запасной логотип для документации/About | — |

Подключение:

- `Package.wxs`: `<WixVariable Id="WixUIBannerBmp" ... />`, `<WixVariable Id="WixUIDialogBmp" ... />`.
- `scripts/installer/build.ps1`: `-d BannerBmp=...`, `-d DialogBmp=...`, `-d UiIcon=...`.
- `170.ico` в UI можно заменить на `./ico/installer/app.ico`.

## UI: 16 px PNG

Копируются в `./src/WinSheduler.UI/Resources/Icons/`.

| Файл | Кнопка/место |
|------|--------------|
| `refresh.png` | «Обновить» (главное окно, журнал, диагностика) |
| `add_task.png` | «Добавить» задачу |
| `add.png` | общее добавление |
| `add_schedule.png` | «Добавить триггер», «Дублировать триггер» |
| `edit_task.png` | «Изменить» задачу (документ с карандашом) |
| `edit_schedule.png` | «Изменить триггер» (часы с карандашом) |
| `task.png` | «Задание (узел)» |
| `schedule.png` | «Расписание (узел)» |
| `delete.png` | «Удалить» задачу/триггер |
| `journal.png` | «Журнал», «Загрузить аудит» |
| `run.png` | «Запустить», «Запустить от администратора» |
| `pause.png` | «Включить/Отключить» |
| `start.png` | «Старт» |
| `stop.png` | «Стоп» |
| `save.png` | «Сохранить», «ОК», «Экспорт CSV», «Копировать» |
| `cancel.png` | «Отмена», «Очистить пароль», «Закрыть» |
| `open_folder.png` | «Обзор программы», «Открыть лог/папку» |
| `calendar.png` | «Выбор периода» |
| `app.png` | «Диагностика», About |
| `export.png` | «Экспорт задачи/выбранных» (лоток, стрелка вверх) |
| `import.png` | «Импорт задач» (лоток, стрелка вниз) |

## Что меняется

1. PNG копируются в `./src/WinSheduler.UI/Resources/Icons/`.
2. `./src/WinSheduler.UI/Resources/Icons.xaml` — ключи `BitmapImage` для нового набора; старые ключи сохраняются как алиасы.
3. `./src/WinSheduler.UI/WinSheduler.UI.csproj` — `Resource` для новых PNG.
4. Кнопки в `MainWindow.xaml`, `TaskEditorWindow.xaml`, `TimeWindowDialog.xaml`,
   `HistoryWindow.xaml`, `DiagnosticsWindow.xaml` получают `StackPanel` с `Image` 16×16 и текстом.
5. `./ico/installer/app.ico` копируется поверх `./src/WinSheduler.UI/170.ico`.
6. `installer/Package.wxs` и `scripts/installer/build.ps1` — баннер, диалог, иконка.

## Иконки редактирования

Добавлены в `./ico` и сгенерированы через `./ico/make_assets.py`:

- `edit_task` — документ с синим бейджем-карандашом;
- `edit_schedule` — часы с синим бейджем-карандашом.

Они используются на кнопках «Изменить» в главном окне и «Изменить» триггера
в редакторе задачи.

## Иконки обмена задачами

Добавлены в `./ico` и сгенерированы через `./ico/make_assets.py`:

- `export` — синий лоток и зелёная стрелка вверх;
- `import` — синий лоток и зелёная стрелка вниз.

Ключи `Icons.xaml`: `IconExport`, `IconImport`. Используются на кнопках
«Экспорт…» и «Импорт…» в главном окне.
