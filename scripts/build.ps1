<#
.SYNOPSIS
    Сборка решения WinSheduler: C++ служба, C# UI, общие модели и тесты.

.DESCRIPTION
    Управляемые проекты (C#) собираются через dotnet build из SDK 10.0.4xx,
    нативные (C++) — через MSBuild из VS 2022. Решение целиком через
    dotnet build не собирается: в нём есть C++ проект.
    Сборки сериализуются lock-файлом ./temp/locks/build.lock (ожидание до 10 минут).

.PARAMETER Configuration
    Конфигурация сборки. По умолчанию Release.

.PARAMETER Platform
    Платформа сборки. По умолчанию x64.
#>
[CmdletBinding()]
param(
    [string]$Configuration = 'Release',
    [string]$Platform = 'x64'
)

$ErrorActionPreference = 'Stop'

# Корень проекта — родитель каталога ./scripts.
$root = Split-Path -Parent $PSScriptRoot

function Write-Step {
    param([string]$Message)
    Write-Host "==> $Message" -ForegroundColor Cyan
}

# Поиск MSBuild: vswhere (ProgramFiles(x86), затем ProgramFiles),
# явный путь VS 2022 Community, затем msbuild из PATH.
# MSBuild нужен только для нативных (C++) проектов.
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

        # Проект использует toolset по умолчанию (v143/v145): сначала ищем VS 2022 (17.x),
        # затем любой MSBuild с C++-инструментами, затем любой MSBuild.
        $installationPath = & $vswhere -latest -version '[17.0,18.0)' -products * -requires Microsoft.Component.MSBuild Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath 2>$null
        if (-not $installationPath) {
            $installationPath = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath 2>$null
        }
        if (-not $installationPath) {
            $installationPath = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -property installationPath 2>$null
        }
        if (-not $installationPath) { continue }

        $candidate = Join-Path $installationPath 'MSBuild/Current/Bin/MSBuild.exe'
        if (Test-Path -LiteralPath $candidate) { return $candidate }
    }

    if ($env:ProgramFiles) {
        $candidate = Join-Path $env:ProgramFiles 'Microsoft Visual Studio/2022/Community/MSBuild/Current/Bin/MSBuild.exe'
        if (Test-Path -LiteralPath $candidate) { return $candidate }
    }

    $fromPath = Get-Command 'msbuild.exe' -ErrorAction SilentlyContinue
    if ($fromPath) { return $fromPath.Source }

    return $null
}

# Эксклюзивная блокировка файла: параллельные агенты не портят bin/ и obj/.
function Enter-BuildLock {
    param(
        [string]$Path,
        [int]$TimeoutMinutes = 10
    )

    $directory = Split-Path -Parent $Path
    if (-not (Test-Path -LiteralPath $directory)) {
        New-Item -ItemType Directory -Path $directory -Force | Out-Null
    }

    $deadline = (Get-Date).AddMinutes($TimeoutMinutes)
    while ($true) {
        try {
            return [System.IO.File]::Open(
                $Path,
                [System.IO.FileMode]::OpenOrCreate,
                [System.IO.FileAccess]::ReadWrite,
                [System.IO.FileShare]::None)
        }
        catch [System.IO.IOException] {
            if ((Get-Date) -ge $deadline) {
                throw "Не удалось получить блокировку сборки '$Path' за $TimeoutMinutes минут."
            }
            Start-Sleep -Seconds 2
        }
    }
}

$lockPath = Join-Path $root 'temp/locks/build.lock'
$lock = Enter-BuildLock -Path $lockPath

try {
    $projects = @(
        (Join-Path $root 'src/WinSheduler.Shared/WinSheduler.Shared.csproj'),
        (Join-Path $root 'src/WinSheduler.UI/WinSheduler.UI.csproj'),
        (Join-Path $root 'tests/WinSheduler.Tests/WinSheduler.Tests.csproj')
    )

    Write-Step 'Восстановление пакетов C# проектов'
    foreach ($project in $projects) {
        if (-not (Test-Path -LiteralPath $project)) {
            throw "Не найден проект: $project"
        }

        & dotnet restore $project --nologo | Out-Null
        if ($LASTEXITCODE -ne 0) {
            throw "dotnet restore завершился с кодом $LASTEXITCODE ($project)."
        }
    }

    # Управляемые проекты: dotnet build (SDK 10.0.4xx). MSBuild из VS 2022
    # не поддерживает targeting net10.0 (NETSDK1209/NETSDK1233).
    Write-Step "Сборка управляемых проектов ($Configuration)"
    foreach ($project in $projects) {
        & dotnet build $project -c $Configuration --no-restore --nologo
        if ($LASTEXITCODE -ne 0) {
            throw "dotnet build завершился с кодом $LASTEXITCODE ($project)."
        }
    }

    # Нативные проекты: только MSBuild из VS 2022.
    $msbuild = Find-MSBuild
    if (-not $msbuild) {
        throw 'MSBuild не найден. Установите Visual Studio 2022/2026 с C++-инструментами или добавьте msbuild в PATH.'
    }

    Write-Step "Сборка нативных проектов ($Configuration|$Platform)"
    Write-Host "    MSBuild: $msbuild"

    $nativeProject = Join-Path $root 'src/WinShedulerSvc/WinShedulerSvc.vcxproj'
    & $msbuild $nativeProject "/p:Configuration=$Configuration" "/p:Platform=$Platform" '/m' '/nologo' '/verbosity:minimal'
    if ($LASTEXITCODE -ne 0) {
        throw "Сборка завершилась с кодом $LASTEXITCODE."
    }

    Write-Step 'Сборка успешно завершена'
}
finally {
    if ($null -ne $lock) { $lock.Dispose() }
}
