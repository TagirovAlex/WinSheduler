<#
.SYNOPSIS
    Полный локальный gate: сборка, тесты, форматирование, контрактные тесты,
    схемы, smoke-тест службы.

.DESCRIPTION
    Последовательно выполняет build.ps1, test.ps1, format.ps1, затем контрактные
    тесты C++ и C# на общих фикстурах, проверяет корректность JSON-схем в
    ./docs/contracts/schemas/ и запускает smoke-тест standalone-службы.
    Возвращает ненулевой код при любой ошибке.

.PARAMETER Scope
    Область проверки (трек). Пока зарезервирован и не влияет на набор шагов.

.PARAMETER CleanCheckout
    Дополнительно собрать проект из чистого снимка HEAD (git archive) в
    ./temp/verify/clean. Проверка G-10; по умолчанию выключена из-за времени.

.PARAMETER Analyze
    Дополнительно выполнить статический анализ (G-5): MSVC /analyze для
    контрактного проекта C++ и clang-tidy для json.cpp, если утилита
    установлена. .NET analyzers уже включены в обычную сборку.
#>
[CmdletBinding()]
param(
    [string]$Scope = 'all',
    [switch]$CleanCheckout,
    [switch]$Analyze
)

$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$results = [ordered]@{}

# Поиск MSBuild для отдельного C++ проекта контрактных тестов
# (он не входит в ./WinSheduler.sln, чтобы не менять файл решения).
function Find-MSBuild {
    $vswhereCandidates = @()

    if (${env:ProgramFiles(x86)}) {
        $vswhereCandidates += (Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe')
    }
    if ($env:ProgramFiles) {
        $vswhereCandidates += (Join-Path $env:ProgramFiles 'Microsoft Visual Studio/Installer/vswhere.exe')
    }

    foreach ($vswhere in $vswhereCandidates) {
        if (-not (Test-Path -LiteralPath $vswhere)) { continue }

        $installationPath = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -property installationPath 2>$null
        if (-not $installationPath) { continue }

        $candidate = Join-Path $installationPath 'MSBuild/Current/Bin/MSBuild.exe'
        if (Test-Path -LiteralPath $candidate) { return $candidate }
    }

    $fromPath = Get-Command 'msbuild.exe' -ErrorAction SilentlyContinue
    if ($fromPath) { return $fromPath.Source }

    return $null
}

function Invoke-Step {
    param(
        [string]$Name,
        [scriptblock]$Action,
        [switch]$ShowOutput
    )

    Write-Host ''
    Write-Host "==> $Name" -ForegroundColor Cyan
    try {
        if ($ShowOutput) {
            # Вывод действия уходит в host, а не в pipeline:
            # иначе он смешивается с возвращаемым значением шага.
            & $Action 2>&1 | ForEach-Object { Write-Host $_ }
        }
        else {
            & $Action | Out-Null
        }
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

# 4. Контрактные тесты C++ (общие фикстуры ./tests/contract/fixtures)
if ($results['build']) {
    $results['contract-cpp'] = Invoke-Step 'Контрактные тесты C++' {
        $msbuild = Find-MSBuild
        if (-not $msbuild) {
            throw 'MSBuild не найден: контрактные тесты C++ собрать нельзя.'
        }

        $project = Join-Path $root 'tests/contract/cpp/WinSheduler.ContractTests.vcxproj'
        if (-not (Test-Path -LiteralPath $project)) {
            throw "Не найден проект контрактных тестов C++: $project"
        }

        & $msbuild $project '/p:Configuration=Release' '/p:Platform=x64' '/nologo' '/verbosity:minimal'
        if ($LASTEXITCODE -ne 0) {
            throw "Сборка контрактных тестов C++ завершилась с кодом $LASTEXITCODE."
        }

        $exe = Join-Path $root 'tests/contract/cpp/bin/Release/WinSheduler.ContractTests.exe'
        if (-not (Test-Path -LiteralPath $exe)) {
            throw "Не найден исполняемый файл контрактных тестов C++: $exe"
        }

        $fixtures = (Join-Path $root 'tests/contract/fixtures') -replace '\\', '/'
        & $exe "--fixtures=$fixtures"
        if ($LASTEXITCODE -ne 0) {
            throw "Контрактные тесты C++ завершились с кодом $LASTEXITCODE."
        }
    } -ShowOutput
}
else {
    $results['contract-cpp'] = $false
    Write-Warning 'Контрактные тесты C++ пропущены: сборка не удалась.'
}

# 5. Контрактные тесты C# (те же фикстуры и JSON-схемы)
if ($results['build']) {
    $results['contract-csharp'] = Invoke-Step 'Контрактные тесты C#' {
        $project = Join-Path $root 'tests/contract/WinSheduler.ContractTests/WinSheduler.ContractTests.csproj'
        if (-not (Test-Path -LiteralPath $project)) {
            throw "Не найден проект контрактных тестов C#: $project"
        }

        & dotnet test $project -c 'Release' --nologo
        if ($LASTEXITCODE -ne 0) {
            throw "Контрактные тесты C# завершились с кодом $LASTEXITCODE."
        }
    } -ShowOutput
}
else {
    $results['contract-csharp'] = $false
    Write-Warning 'Контрактные тесты C# пропущены: сборка не удалась.'
}

# 6. JSON-схемы контрактов
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

# 7. Smoke-тест standalone-службы
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

# 8. Сборка с чистого checkout (G-10), только по запросу -CleanCheckout.
# Проверяет, что в HEAD нет забытых файлов: снимок git archive собирается сам.
if ($CleanCheckout) {
    $results['clean-checkout'] = Invoke-Step 'Сборка с чистого checkout (git archive)' {
        $cleanRoot = Join-Path $root 'temp/verify/clean'
        $archive = Join-Path $root 'temp/verify/clean.tar'

        if (Test-Path -LiteralPath $cleanRoot) {
            Remove-Item -LiteralPath $cleanRoot -Recurse -Force
        }
        New-Item -ItemType Directory -Path $cleanRoot -Force | Out-Null

        if (Test-Path -LiteralPath $archive) {
            Remove-Item -LiteralPath $archive -Force
        }

        & git -C $root archive --format=tar -o $archive HEAD
        if ($LASTEXITCODE -ne 0) {
            throw "git archive завершился с кодом $LASTEXITCODE."
        }

        & tar -C $cleanRoot -xf $archive
        if ($LASTEXITCODE -ne 0) {
            throw "Распаковка снимка завершилась с кодом $LASTEXITCODE."
        }

        & (Join-Path $cleanRoot 'scripts/build.ps1') -Configuration 'Release' -Platform 'x64'
        if ($LASTEXITCODE -ne 0) {
            throw "Сборка чистого checkout завершилась с кодом $LASTEXITCODE."
        }
    } -ShowOutput
}

# 9. Статический анализ (G-5), только по запросу -Analyze.
# .NET analyzers включены через ./Directory.Build.props и работают в обычной сборке.
if ($Analyze) {
    $results['analyze'] = Invoke-Step 'Статический анализ' {
        $msbuild = Find-MSBuild
        if (-not $msbuild) {
            throw 'MSBuild не найден: MSVC /analyze недоступен.'
        }

        $project = Join-Path $root 'tests/contract/cpp/WinSheduler.ContractTests.vcxproj'
        & $msbuild $project '/p:Configuration=Release' '/p:Platform=x64' '/p:RunCodeAnalysis=true' '/nologo' '/verbosity:minimal'
        if ($LASTEXITCODE -ne 0) {
            throw "MSVC /analyze завершился с кодом $LASTEXITCODE."
        }

        $clangTidy = Get-Command 'clang-tidy.exe' -ErrorAction SilentlyContinue
        if ($null -eq $clangTidy) {
            $clangTidy = Get-Command 'clang-tidy' -ErrorAction SilentlyContinue
        }

        if ($null -eq $clangTidy) {
            Write-Warning 'clang-tidy не найден — проверка C++ пропущена.'
        }
        else {
            $source = Join-Path $root 'src/WinShedulerSvc/json.cpp'
            & $clangTidy.Source '--quiet' $source '--' '-std=c++20' "-I$(Join-Path $root 'src/WinShedulerSvc')"
            if ($LASTEXITCODE -ne 0) {
                throw "clang-tidy нашёл замечания (код $LASTEXITCODE)."
            }
        }

        Write-Host '    .NET analyzers: работают в обычной сборке (EnableNETAnalyzers=true).'
    } -ShowOutput
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
