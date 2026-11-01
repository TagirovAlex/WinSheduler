<#
.SYNOPSIS
    Сборка и запуск тестов трека C (хранение).

.DESCRIPTION
    Собирает ./tests/db/db_tests.vcxproj (Release|x64) и запускает полученный
    исполняемый файл с корнем проекта как рабочим каталогом. Тесты создают
    временные файлы только в ./temp/c/db-tests/ и удаляют их при следующем
    запуске. Код возврата ненулевой при любой ошибке сборки или теста.

.PARAMETER Configuration
    Конфигурация сборки. По умолчанию Release.
#>
[CmdletBinding()]
param(
    [string]$Configuration = 'Release'
)

$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$project = Join-Path $PSScriptRoot 'db_tests.vcxproj'

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

    if ($env:ProgramFiles) {
        $candidate = Join-Path $env:ProgramFiles 'Microsoft Visual Studio/2022/Community/MSBuild/Current/Bin/MSBuild.exe'
        if (Test-Path -LiteralPath $candidate) { return $candidate }
    }

    $fromPath = Get-Command 'msbuild.exe' -ErrorAction SilentlyContinue
    if ($fromPath) { return $fromPath.Source }

    return $null
}

# Общий lock сборки: параллельные агенты не портят bin/ и obj/.
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

$msbuild = Find-MSBuild
if (-not $msbuild) {
    throw 'MSBuild не найден. Установите Visual Studio 2022 (MSVC v143) или добавьте msbuild в PATH.'
}

$lockPath = Join-Path $root 'temp/locks/build.lock'
$lock = Enter-BuildLock -Path $lockPath

try {
    Write-Host "==> Сборка тестов трека C ($Configuration|x64)" -ForegroundColor Cyan
    & $msbuild $project "/p:Configuration=$Configuration" '/p:Platform=x64' '/nologo' '/verbosity:minimal'
    if ($LASTEXITCODE -ne 0) {
        throw "Сборка тестов завершилась с кодом $LASTEXITCODE."
    }
}
finally {
    if ($null -ne $lock) { $lock.Dispose() }
}

$exe = Join-Path $PSScriptRoot "bin/$Configuration/WinShedulerDbTests.exe"
if (-not (Test-Path -LiteralPath $exe)) {
    throw "Не найден исполняемый файл тестов: $exe"
}

Write-Host '==> Запуск тестов трека C' -ForegroundColor Cyan
$process = Start-Process -FilePath $exe -WorkingDirectory $root -NoNewWindow -Wait -PassThru
if ($process.ExitCode -ne 0) {
    throw "Тесты трека C завершились с кодом $($process.ExitCode)."
}

Write-Host 'Тесты трека C пройдены.' -ForegroundColor Green
exit 0
