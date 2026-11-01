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
| `task.png` | «Изменить» задачу (отдельной иконки edit в наборе нет) |
| `schedule.png` | «Изменить триггер» |
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

## Что меняется

1. PNG копируются в `./src/WinSheduler.UI/Resources/Icons/`.
2. `./src/WinSheduler.UI/Resources/Icons.xaml` — ключи `BitmapImage` для нового набора; старые ключи сохраняются как алиасы.
3. `./src/WinSheduler.UI/WinSheduler.UI.csproj` — `Resource` для новых PNG.
4. Кнопки в `MainWindow.xaml`, `TaskEditorWindow.xaml`, `TimeWindowDialog.xaml`,
   `HistoryWindow.xaml`, `DiagnosticsWindow.xaml` получают `StackPanel` с `Image` 16×16 и текстом.
5. `./ico/installer/app.ico` копируется поверх `./src/WinSheduler.UI/170.ico`.
6. `installer/Package.wxs` и `scripts/installer/build.ps1` — баннер, диалог, иконка.

## Ограничение

В наборе нет отдельной иконки «Изменить» (карандаш). Для редактирования задачи
используется `task.png`, для редактирования триггера — `schedule.png`.
Если нужен карандаш, его можно добавить в набор отдельно.
