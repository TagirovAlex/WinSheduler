<#
.SYNOPSIS
    Полный локальный gate: сборка, тесты, форматирование, схемы, smoke-тест службы.

.DESCRIPTION
    Последовательно выполняет build.ps1, test.ps1, format.ps1, затем проверяет
    наличие и корректность JSON-схем в ./docs/contracts/schemas/ и запускает
    smoke-тест standalone-службы. Возвращает ненулевой код при любой ошибке.

.PARAMETER Scope
    Область проверки (трек). Пока зарезервирован и не влияет на набор шагов.
#>
[CmdletBinding()]
param(
    [string]$Scope = 'all'
)

$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$results = [ordered]@{}

function Invoke-Step {
    param(
        [string]$Name,
        [scriptblock]$Action
    )

    Write-Host ''
    Write-Host "==> $Name" -ForegroundColor Cyan
    try {
        & $Action | Out-Null
        Write-Host '    OK' -ForegroundColor Green
        return $true
    }
    catch {
        Write-Host "    ОШИБКА: $($_.Exception.Message)" -ForegroundColor Red
        return $false
    }
}

Write-Host "Gate WinSheduler, область: $Scope" -ForegroundColor White
Write-Host "Корень проекта: $root"

# 1. Сборка
$results['build'] = Invoke-Step 'Сборка (build.ps1)' {
    & (Join-Path $PSScriptRoot 'build.ps1') -Configuration 'Release' -Platform 'x64'
}

# 2. Тесты
if ($results['build']) {
    $results['test'] = Invoke-Step 'Тесты (test.ps1)' {
        & (Join-Path $PSScriptRoot 'test.ps1') -Configuration 'Release'
    }
}
else {
    $results['test'] = $false
    Write-Warning 'Тесты пропущены: сборка не удалась.'
}

# 3. Форматирование
if ($results['build']) {
    $results['format'] = Invoke-Step 'Форматирование (format.ps1)' {
        & (Join-Path $PSScriptRoot 'format.ps1')
    }
}
else {
    $results['format'] = $false
    Write-Warning 'Форматирование пропущено: сборка не удалась.'
}

# 4. JSON-схемы контрактов
$results['schemas'] = Invoke-Step 'JSON-схемы контрактов' {
    $schemaDir = Join-Path $root 'docs/contracts/schemas'
    $schemas = @()

    if (Test-Path -LiteralPath $schemaDir) {
        $schemas = @(Get-ChildItem -LiteralPath $schemaDir -Filter '*.json' -File -ErrorAction SilentlyContinue)
    }

    if ($schemas.Count -eq 0) {
        Write-Warning 'JSON-схемы в ./docs/contracts/schemas/ отсутствуют — проверка пропущена.'
        return
    }

    foreach ($schema in $schemas) {
        try {
            Get-Content -LiteralPath $schema.FullName -Raw | ConvertFrom-Json | Out-Null
        }
        catch {
            throw "Некорректный JSON в схеме: $($schema.Name)"
        }
    }

    Write-Host "    Проверено схем: $($schemas.Count)"
}

# 5. Smoke-тест standalone-службы
$results['smoke'] = Invoke-Step 'Smoke-тест standalone-службы' {
    $exe = Join-Path $root 'src/WinShedulerSvc/bin/Release/WinShedulerSvc.exe'
    if (-not (Test-Path -LiteralPath $exe)) {
        Write-Warning "Smoke-тест пропущен: не найден $exe"
        return
    }

    $workDir = Join-Path $root 'temp/verify'
    $dataDir = Join-Path $workDir 'data'
    $logDir = Join-Path $workDir 'logs'

    New-Item -ItemType Directory -Path $dataDir -Force | Out-Null
    New-Item -ItemType Directory -Path $logDir -Force | Out-Null

    $dbPath = (Join-Path $dataDir 'scheduler.db') -replace '\\', '/'
    $logPath = $logDir -replace '\\', '/'

    $process = Start-Process -FilePath $exe `
        -ArgumentList '--standalone', "--dbpath=$dbPath", "--logdir=$logPath" `
        -PassThru -WindowStyle Hidden

    try {
        Start-Sleep -Seconds 2

        if ($process.HasExited) {
            throw "Служба завершилась сразу после старта (код $($process.ExitCode))."
        }

        $processId = $process.Id
        Stop-Process -Id $processId -Force -ErrorAction SilentlyContinue
        $null = $process.WaitForExit(10000)

        if (-not $process.HasExited) {
            throw "Процесс службы (PID $processId) не остановился за 10 секунд."
        }

        Write-Host "    Процесс запущен и остановлен (PID $processId)"
    }
    finally {
        if (-not $process.HasExited) {
            Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
        }
        $process.Dispose()
    }
}

# Итог
Write-Host ''
Write-Host '==> Итог gate' -ForegroundColor Cyan

$failed = @()
foreach ($key in $results.Keys) {
    if ($results[$key]) {
        Write-Host "    [OK]     $key" -ForegroundColor Green
    }
    else {
        Write-Host "    [ОШИБКА] $key" -ForegroundColor Red
        $failed += $key
    }
}

if ($failed.Count -gt 0) {
    Write-Host ''
    Write-Host "Gate НЕ пройден. Провалено шагов: $($failed -join ', ')" -ForegroundColor Red
    exit 1
}

Write-Host ''
Write-Host 'Gate пройден успешно.' -ForegroundColor Green
exit 0
