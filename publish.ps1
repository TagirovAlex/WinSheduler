<#
.SYNOPSIS
    Публикация бинарников WinSheduler в ./artifacts (без MSI).

.DESCRIPTION
    Обёртка над ./scripts/installer/build.ps1 -SkipMsi: собирает службу и
    публикует self-contained UI в ./artifacts/ui, службу — в ./artifacts/service.
    Каталог ./published не удаляется: данные и старая раскладка остаются на месте
    (перенос данных выполняет MSI или администратор вручную).

.PARAMETER ReadyToRun
    Включить ReadyToRun для UI.

.PARAMETER Version
    Версия продукта x.y.z. По умолчанию — из git-тега.
#>
[CmdletBinding()]
param(
    [switch]$ReadyToRun,
    [string]$Version
)

$ErrorActionPreference = 'Stop'

$arguments = @{ SkipMsi = $true }
if ($ReadyToRun) { $arguments.ReadyToRun = $true }
if ($Version) { $arguments.Version = $Version }

& (Join-Path $PSScriptRoot 'scripts/installer/build.ps1') @arguments
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

exit 0
