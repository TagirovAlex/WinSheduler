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
    Версия продукта x.y.z. По умолчанию — 2.1.3.

.PARAMETER SkipMsi
    Собрать только службу и UI, без MSI.

.PARAMETER CertificateThumbprint
    Thumbprint сертификата подписи кода в CurrentUser\My.

.PARAMETER UpdateShare
    SMB-шара для публикации обновления. По умолчанию \\intsrvfs\winsheduler.

.PARAMETER SkipShare
    Не публиковать MSI на шару.

.PARAMETER TimestampUrl
    Сервер меток времени. Пустая строка — без timestamp.
#>
[CmdletBinding()]
param(
    [switch]$ReadyToRun,
    [string]$Version = '2.1.3',
    [switch]$SkipMsi,
    [string]$CertificateThumbprint,
    [string]$UpdateShare = '\\intsrvfs\winsheduler',
    [switch]$SkipShare,
    [string]$TimestampUrl = 'http://timestamp.digicert.com'
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
if ($CertificateThumbprint) { $arguments.CertificateThumbprint = $CertificateThumbprint }
if ($UpdateShare) { $arguments.UpdateUrl = $UpdateShare }
if ($PSBoundParameters.ContainsKey('TimestampUrl')) { $arguments.TimestampUrl = $TimestampUrl }

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

$displayVersion = if ($Version) { $Version } else { '2.1.3' }
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

# --- Публикация обновления на SMB-шару ------------------------------------
if (-not $SkipMsi -and -not $SkipShare -and $UpdateShare) {
    Write-Host "==> Публикация на $UpdateShare" -ForegroundColor Cyan

    if (-not (Test-Path -LiteralPath $UpdateShare)) {
        throw "Шара обновлений недоступна: $UpdateShare"
    }

    $msi = Join-Path $published 'WinSheduler.msi'
    if (-not (Test-Path -LiteralPath $msi)) {
        throw "MSI не найден: $msi"
    }

    $shareMsi = Join-Path $UpdateShare "WinSheduler-$displayVersion.msi"
    Copy-Item -LiteralPath $msi -Destination $shareMsi -Force

    $releases = Join-Path $UpdateShare 'releases'
    New-Item -ItemType Directory -Path $releases -Force | Out-Null
    $notes = Join-Path $root "docs/releases/v$displayVersion.md"
    if (Test-Path -LiteralPath $notes) {
        Copy-Item -LiteralPath $notes -Destination (Join-Path $releases "v$displayVersion.md") -Force
    }

    # Храним текущую и 2 предыдущие версии.
    $versionPattern = '^WinSheduler-(\d+\.\d+\.\d+)\.msi$'
    $versions = Get-ChildItem -LiteralPath $UpdateShare -Filter 'WinSheduler-*.msi' |
        ForEach-Object {
            if ($_.Name -match $versionPattern) {
                [pscustomobject]@{ File = $_; Version = [version]$matches[1] }
            }
        } |
        Sort-Object Version -Descending

    foreach ($old in ($versions | Select-Object -Skip 3)) {
        Remove-Item -LiteralPath $old.File.FullName -Force
        Write-Host "    Удалена старая версия: $($old.File.Name)" -ForegroundColor DarkGray
    }

    $current = Get-Item -LiteralPath $shareMsi
    $hash = (Get-FileHash -LiteralPath $shareMsi -Algorithm SHA256).Hash
    $checksums = Get-ChildItem -LiteralPath $UpdateShare -Filter 'WinSheduler-*.msi' |
        Sort-Object Name |
        ForEach-Object {
            '{0}  {1}' -f (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash, $_.Name
        }
    $checksums | Set-Content -LiteralPath (Join-Path $UpdateShare 'SHA256SUMS.txt') -Encoding ASCII

    $notesPath = Join-Path $releases "v$displayVersion.md"
    $manifest = [ordered]@{
        product = 'WinSheduler'
        version = $displayVersion
        url = $shareMsi
        sha256 = $hash
        size = $current.Length
        publishedAt = (Get-Date).ToUniversalTime().ToString('yyyy-MM-ddTHH:mm:ssZ')
        mandatory = $false
        minVersion = '2.0.0'
        notesUrl = $notesPath
    } | ConvertTo-Json -Depth 3

    $tempManifest = Join-Path $root 'temp/installer/latest.json'
    New-Item -ItemType Directory -Path (Split-Path -Parent $tempManifest) -Force | Out-Null
    [System.IO.File]::WriteAllText(
        $tempManifest,
        $manifest,
        (New-Object System.Text.UTF8Encoding($false)))

    $shareManifestTmp = Join-Path $UpdateShare 'latest.json.tmp'
    Copy-Item -LiteralPath $tempManifest -Destination $shareManifestTmp -Force
    Move-Item -LiteralPath $shareManifestTmp -Destination (Join-Path $UpdateShare 'latest.json') -Force

    Write-Host "    MSI:  $shareMsi" -ForegroundColor Green
    Write-Host "    SHA256: $hash" -ForegroundColor Green
}

Write-Host "Готово: $published" -ForegroundColor Green
exit 0
