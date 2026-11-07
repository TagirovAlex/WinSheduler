<#
.SYNOPSIS
    Сборка и запуск тестов планировщика (трек A).

.DESCRIPTION
    Собирает ./tests/scheduler/WinShedulerSchedulerTests.vcxproj и запускает
    полученный исполняемый файл. Тестовый проект не входит в решение, поэтому
    gate ./scripts/verify.ps1 его пока не запускает (запрос треку G).

.PARAMETER Configuration
    Конфигурация сборки. По умолчанию Release.
#>
[CmdletBinding()]
param(
    [string]$Configuration = 'Release'
)

$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8

$project = Join-Path $PSScriptRoot 'WinShedulerSchedulerTests.vcxproj'

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
    throw 'MSBuild не найден. Установите Visual Studio 2022/2026 с C++-инструментами.'
}

Write-Host "==> Сборка тестов планировщика ($Configuration|x64)" -ForegroundColor Cyan
& $msbuild $project "/p:Configuration=$Configuration" '/p:Platform=x64' '/nologo' '/verbosity:minimal'
if ($LASTEXITCODE -ne 0) {
    throw "Сборка тестов завершилась с кодом $LASTEXITCODE."
}

$exe = Join-Path $PSScriptRoot "bin/$Configuration/WinShedulerSchedulerTests.exe"
if (-not (Test-Path -LiteralPath $exe)) {
    throw "Не найден исполняемый файл тестов: $exe"
}

Write-Host "==> Запуск тестов" -ForegroundColor Cyan
& $exe
if ($LASTEXITCODE -ne 0) {
    throw "Тесты завершились с кодом $LASTEXITCODE."
}

Write-Host '==> Тесты планировщика пройдены' -ForegroundColor Green
