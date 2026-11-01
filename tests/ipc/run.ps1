<#
.SYNOPSIS
    Сборка и запуск контрактных тестов IPC (трек B).

.DESCRIPTION
    Собирает ./tests/ipc/ipc_tests.vcxproj вместе с модулями службы
    (json, ipc_frame, auth_policy, auth_validate) и запускает исполняемый файл.
    Ненулевой код возврата означает провал контрактных тестов.

.PARAMETER Configuration
    Конфигурация сборки. По умолчанию Release.
#>
[CmdletBinding()]
param(
    [string]$Configuration = 'Release'
)

$ErrorActionPreference = 'Stop'

# Корень проекта — два уровня выше ./tests/ipc.
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)

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

$msbuild = Find-MSBuild
if (-not $msbuild) {
    throw 'MSBuild не найден. Установите Visual Studio 2022 (MSVC v143) или добавьте msbuild в PATH.'
}

$project = Join-Path $PSScriptRoot 'ipc_tests.vcxproj'

Write-Host "==> Сборка тестов IPC ($Configuration|x64)" -ForegroundColor Cyan
& $msbuild $project "/p:Configuration=$Configuration" '/p:Platform=x64' '/nologo' '/verbosity:minimal'
if ($LASTEXITCODE -ne 0) {
    throw "Сборка тестов IPC завершилась с кодом $LASTEXITCODE."
}

$exe = Join-Path $PSScriptRoot "bin/$Configuration/ipc_tests.exe"
if (-not (Test-Path -LiteralPath $exe)) {
    throw "Не найден исполняемый файл тестов: $exe"
}

Write-Host '==> Запуск тестов IPC' -ForegroundColor Cyan
& $exe
if ($LASTEXITCODE -ne 0) {
    throw "Тесты IPC завершились с кодом $LASTEXITCODE."
}

Write-Host 'Тесты IPC пройдены.' -ForegroundColor Green
exit 0
