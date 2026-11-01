<#
.SYNOPSIS
    Сборка финального комплекта WinSheduler в ./published.

.DESCRIPTION
    Удаляет старые версии в ./published (БД предварительно копируется в
    ./temp/backup/published-old-db), собирает службу, self-contained UI, MSI
    и SHA256 в ./published.

.PARAMETER ReadyToRun
    Включить ReadyToRun для UI.

.PARAMETER Version
    Версия продукта x.y.z. По умолчанию — из git-тега.

.PARAMETER SkipMsi
    Собрать только службу и UI, без MSI.
#>
[CmdletBinding()]
param(
    [switch]$ReadyToRun,
    [string]$Version,
    [switch]$SkipMsi
)

$ErrorActionPreference = 'Stop'

$root = $PSScriptRoot
$published = Join-Path $root 'published'
$backup = Join-Path $root 'temp/backup/published-old-db'

# Сохраняем старую БД, если она есть: удаляем только старые версии,
# пользовательские данные не теряем.
$oldDb = Join-Path $published 'service/data/scheduler.db'
if (Test-Path -LiteralPath $oldDb) {
    New-Item -ItemType Directory -Path $backup -Force | Out-Null
    Copy-Item -LiteralPath $oldDb -Destination (Join-Path $backup 'scheduler.db') -Force

    foreach ($suffix in '-shm', '-wal') {
        $sidecar = "$oldDb$suffix"
        if (Test-Path -LiteralPath $sidecar) {
            Copy-Item -LiteralPath $sidecar -Destination (Join-Path $backup "scheduler.db$suffix") -Force
        }
    }

    Write-Host "Старая БД сохранена в $backup" -ForegroundColor Yellow
}

if (Test-Path -LiteralPath $published) {
    Remove-Item -LiteralPath $published -Recurse -Force
}
New-Item -ItemType Directory -Path $published -Force | Out-Null

$arguments = @{ OutputDir = 'published' }
if ($ReadyToRun) { $arguments.ReadyToRun = $true }
if ($Version) { $arguments.Version = $Version }
if ($SkipMsi) { $arguments.SkipMsi = $true }

& (Join-Path $root 'scripts/installer/build.ps1') @arguments
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

Copy-Item -LiteralPath (Join-Path $root 'installer/LICENSE.txt') -Destination $published -Force
Copy-Item -LiteralPath (Join-Path $root 'installer/THIRD-PARTY-NOTICES.txt') -Destination $published -Force

# Отладочный файл WiX в финальном комплекте не нужен.
$wixpdb = Join-Path $published 'WinSheduler.wixpdb'
if (Test-Path -LiteralPath $wixpdb) {
    Remove-Item -LiteralPath $wixpdb -Force
}

$displayVersion = if ($Version) { $Version } else { '2.0.0' }
$msiLine = if ($SkipMsi) {
    'MSI не собирался (SkipMsi).'
}
else {
    'msiexec /i ./WinSheduler.msi'
}

$readme = @"
WinSheduler $displayVersion — готовый комплект

Установка:
  $msiLine

Silent:
  msiexec /i ./WinSheduler.msi /qn /l*v ./install.log

Кастомные каталоги:
  msiexec /i ./WinSheduler.msi INSTALLDIR="d:/Apps/WinSheduler" DATADIR="d:/WinShedulerData" /qn

Проверка хеша:
  Get-FileHash ./WinSheduler.msi -Algorithm SHA256

Состав:
  WinSheduler.msi       — установщик (служба + UI + группа + ACL)
  SHA256SUMS.txt        — хеши MSI, службы и UI
  service/              — распакованная служба
  ui/                   — self-contained UI
  LICENSE.txt
  THIRD-PARTY-NOTICES.txt

Документация: ./docs/contracts/INSTALL.md
"@

[System.IO.File]::WriteAllText(
    (Join-Path $published 'README.txt'),
    $readme,
    (New-Object System.Text.UTF8Encoding($true)))

Write-Host "Готово: $published" -ForegroundColor Green
exit 0
