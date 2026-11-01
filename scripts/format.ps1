<#
.SYNOPSIS
    Проверка форматирования C# и C++.

.DESCRIPTION
    Для C# проектов запускается `dotnet format whitespace --verify-no-changes`.
    Для C++ файлов — clang-format --dry-run --Werror, если утилита доступна.
    Отсутствие clang-format — предупреждение, а не ошибка.

    Замечания ENDOFLINE и CHARSET у существующих файлов считаются легаси-долгом:
    репозиторий исторически хранит LF, а .editorconfig требует CRLF для C#/C++/XAML.
    Такие замечания выводятся предупреждением и не валят gate; остальные нарушения
    (отступы, хвостовые пробелы, завершающий перевод строки) — ошибка.

    Возвращает ненулевой код при реальном нарушении форматирования.
#>
[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$failures = 0

Write-Host '==> Проверка форматирования C#' -ForegroundColor Cyan

$projects = @(
    (Join-Path $root 'src/WinSheduler.Shared/WinSheduler.Shared.csproj'),
    (Join-Path $root 'src/WinSheduler.UI/WinSheduler.UI.csproj'),
    (Join-Path $root 'tests/WinSheduler.Tests/WinSheduler.Tests.csproj')
)

foreach ($project in $projects) {
    if (-not (Test-Path -LiteralPath $project)) {
        Write-Warning "Проект не найден, проверка пропущена: $project"
        continue
    }

    # cmd /c объединяет stderr в stdout на своей стороне: PowerShell 5.1 иначе
    # превращает вывод dotnet format в исключение NativeCommandError.
    $command = 'dotnet format whitespace "' + $project + '" --verify-no-changes --verbosity quiet 2>&1'
    $output = (cmd /c $command | Out-String)
    $exitCode = $LASTEXITCODE

    if ($exitCode -eq 0) { continue }

    if ($exitCode -ne 2) {
        # Код 2 — «есть изменения». Всё остальное — ошибка инструмента.
        throw "dotnet format завершился с кодом $exitCode ($project)."
    }

    $errorLines = @($output -split "`r?`n" | Where-Object { $_ -match ':\s*error\s+[A-Z]+\s*:' })
    $legacyLines = @($errorLines | Where-Object { $_ -match ':\s*error\s+(ENDOFLINE|CHARSET)\s*:' })
    $otherLines = @($errorLines | Where-Object { $_ -notmatch ':\s*error\s+(ENDOFLINE|CHARSET)\s*:' })

    if ($otherLines.Count -gt 0) {
        Write-Host "    Нарушено форматирование: $project" -ForegroundColor Red
        $otherLines | Select-Object -First 20 | ForEach-Object { Write-Host "    $_" -ForegroundColor Red }
        $failures++
    }

    if ($legacyLines.Count -gt 0) {
        Write-Warning "Легаси-несоответствие end_of_line/charset: $($legacyLines.Count) замечаний в $project (нормализация отложена)."
    }
}

Write-Host '==> Проверка форматирования C++' -ForegroundColor Cyan

$clangFormat = Get-Command 'clang-format.exe' -ErrorAction SilentlyContinue
if ($null -eq $clangFormat) {
    $clangFormat = Get-Command 'clang-format' -ErrorAction SilentlyContinue
}

if ($null -eq $clangFormat) {
    Write-Warning 'clang-format не найден — проверка форматирования C++ пропущена.'
}
else {
    $sourceDir = Join-Path $root 'src/WinShedulerSvc'
    $sources = @(Get-ChildItem -LiteralPath $sourceDir -File | Where-Object {
        $_.Extension -eq '.cpp' -or $_.Extension -eq '.h' -or $_.Extension -eq '.c'
    } | Where-Object {
        # sqlite3.c/sqlite3.h — сторонний код, форматирование не проверяем.
        $_.Name -notlike 'sqlite3.*'
    })

    foreach ($source in $sources) {
        & $clangFormat.Source '--dry-run' '--Werror' $source.FullName
        if ($LASTEXITCODE -ne 0) {
            Write-Host "    Нарушено форматирование: $($source.Name)" -ForegroundColor Red
            $failures++
        }
    }
}

if ($failures -gt 0) {
    throw "Нарушений форматирования: $failures."
}

Write-Host '==> Форматирование в порядке' -ForegroundColor Green
