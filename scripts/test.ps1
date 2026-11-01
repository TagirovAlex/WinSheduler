<#
.SYNOPSIS
    Запуск тестов C#.

.DESCRIPTION
    Запускает ./tests/WinSheduler.Tests в конфигурации Release без повторной сборки
    (сборка выполняется в ./scripts/build.ps1). Если тестовый проект консольный
    (без xUnit), запускается его исполняемый файл.
    Возвращает ненулевой код при ошибке.
#>
[CmdletBinding()]
param(
    [string]$Configuration = 'Release'
)

$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$project = Join-Path $root 'tests/WinSheduler.Tests/WinSheduler.Tests.csproj'

Write-Host "==> Тесты ($Configuration)" -ForegroundColor Cyan

if (-not (Test-Path -LiteralPath $project)) {
    throw "Не найден тестовый проект: $project"
}

$content = Get-Content -LiteralPath $project -Raw
$isTestProject = $content -match '<IsTestProject>\s*true\s*</IsTestProject>'

if ($isTestProject) {
    & dotnet test $project -c $Configuration --no-build --nologo
    if ($LASTEXITCODE -ne 0) {
        throw "Тесты завершились с кодом $LASTEXITCODE."
    }
}
else {
    # Консольный тестовый проект без внешних пакетов.
    $exe = Join-Path $root "tests/WinSheduler.Tests/bin/$Configuration/net8.0/WinSheduler.Tests.exe"
    if (-not (Test-Path -LiteralPath $exe)) {
        throw "Не найден исполняемый файл тестов: $exe"
    }

    & $exe
    if ($LASTEXITCODE -ne 0) {
        throw "Тесты завершились с кодом $LASTEXITCODE."
    }
}

Write-Host '==> Тесты успешно пройдены' -ForegroundColor Green
