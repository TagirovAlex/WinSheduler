# Запросы трека H к другим трекам

## Track D — решено 2026-11-06: конфликт имени `ThemeMode` в .NET 10

1. **`./src/WinSheduler.UI/App.xaml.cs` не компилируется на `net10.0-windows`.**
   В .NET 10 WPF добавлен `System.Windows.ThemeMode` и свойство
   `Application.ThemeMode`. В `App` одновременно импортированы
   `System.Windows` и `WinSheduler.UI.Services` (там объявлен свой
   `enum ThemeMode`), поэтому имя неоднозначно:

   ```
   App.xaml.cs(199,20): error CS0104: "ThemeMode" является неоднозначной
   ссылкой между "WinSheduler.UI.Services.ThemeMode" и "System.Windows.ThemeMode".
   ```

   Даже алиас `using ThemeMode = WinSheduler.UI.Services.ThemeMode;` не помогает:
   член `Application.ThemeMode` перекрывает alias, и тогда появляются
   `CS0176`/`CS0120` и `WPF0001`.

   Проверенное исправление (зона D, 4 ссылки + 1 директива):

   ```csharp
   using AppThemeMode = WinSheduler.UI.Services.ThemeMode;
   ...
   Services.Theme.Apply(AppThemeMode.Light);                       // строка 40
   ...
   private static AppThemeMode ParseTheme(string? value)           // строка 199
       => Enum.TryParse<AppThemeMode>(value, ignoreCase: true, out var mode)
           ? mode : AppThemeMode.System;                           // строка 200
   ```

   Проверено в копии дерева (`./temp/h/`): после этой правки
   `dotnet build src/WinSheduler.UI/WinSheduler.UI.csproj -c Release` на SDK
   10.0.403 проходит без предупреждений и ошибок. Без правки
   `./scripts/build.ps1` падает на шаге управляемых проектов, а
   `./tests/ui/WinSheduler.UI.Tests.csproj` не собирается (ссылается на UI).
   TFM уже переведены на `net10.0*` (H-2), откат не требуется.

   **Статус:** исправлено алиасом `AppThemeMode` в `App.xaml.cs` и
   `tests/ui/DesignSystemAndAccessibilityTests.cs`; UI и UI.Tests собираются,
   37/37 UI-тестов проходят.
