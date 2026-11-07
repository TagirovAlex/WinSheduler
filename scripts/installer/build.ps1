<#
.SYNOPSIS
    Сборка установщика WinSheduler: служба, self-contained UI, MSI, манифест SHA256.

.DESCRIPTION
    Выполняет шаги контракта ./docs/contracts/INSTALL.md:
      1) собирает C++ службу (MSBuild, Release|x64);
      2) публикует UI как self-contained win-x64 (опционально ReadyToRun);
      3) собирает нативную DLL действий установщика;
      4) генерирует компоненты UI для WiX;
      5) собирает MSI через `wix build` (WiX 5);
      6) подписывает (если задан сертификат) или пишет SHA256SUMS.txt.

    Данные не удаляются: ./published не трогается, артефакты пишутся в ./artifacts.
    Временные файлы — только в ./temp/installer.

.PARAMETER Configuration
    Конфигурация сборки. По умолчанию Release.

.PARAMETER Version
    Версия продукта (x.y.z). По умолчанию — из ближайшего git-тега (v1.2.3 -> 1.2.3).

.PARAMETER OutputDir
    Каталог готовых файлов. По умолчанию ./artifacts; для финальной сборки — ./published.

.PARAMETER SkipMsi
    Только опубликовать бинарники в ./artifacts (без MSI).

.PARAMETER ReadyToRun
    Включить ReadyToRun для UI (быстрее старт, больше размер).

.PARAMETER RequireWix
    Считать отсутствие WiX ошибкой (по умолчанию MSI пропускается с предупреждением).

.PARAMETER EnforceBudgets
    Падать при превышении бюджета размера MSI (80 МБ).

.PARAMETER CertificatePath
    PFX-сертификат для подписи. Если не задан, подпись пропускается (заглушка).

.PARAMETER CertificatePassword
    Пароль PFX (не логируется).

.PARAMETER TimestampUrl
    Сервер меток времени для signtool.

.EXAMPLE
    ./scripts/installer/build.ps1

.EXAMPLE
    ./scripts/installer/build.ps1 -Version 1.2.0 -ReadyToRun
#>
[CmdletBinding()]
param(
    [string]$Configuration = 'Release',
    [string]$Version,
    [string]$OutputDir = 'artifacts',
    [switch]$SkipMsi,
    [switch]$ReadyToRun,
    [switch]$RequireWix,
    [switch]$EnforceBudgets,
    [string]$CertificatePath,
    [string]$CertificatePassword,
    [string]$TimestampUrl = 'http://timestamp.digicert.com'
)

$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$artifacts = if ([System.IO.Path]::IsPathRooted($OutputDir)) {
    $OutputDir
}
else {
    Join-Path $root $OutputDir
}
$tempRoot = Join-Path $root 'temp/installer'
$uiPublish = Join-Path $tempRoot 'ui'
$uiArtifacts = Join-Path $artifacts 'ui'
$serviceArtifacts = Join-Path $artifacts 'service'
$msiPath = Join-Path $artifacts 'WinSheduler.msi'
$maxMsiBytes = 80MB

function Write-Step {
    param([string]$Message)

    Write-Host "==> $Message" -ForegroundColor Cyan
}

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

        # Служба и DLL установщика используют toolset по умолчанию (v143/v145): сначала VS 2022,
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

    $fromPath = Get-Command 'msbuild.exe' -ErrorAction SilentlyContinue
    if ($fromPath) { return $fromPath.Source }

    return $null
}

function Find-Wix {
    $command = Get-Command 'wix.exe' -ErrorAction SilentlyContinue
    if ($command) { return $command.Source }

    $candidates = @(
        (Join-Path $env:USERPROFILE '.dotnet/tools/wix.exe'),
        (Join-Path $env:LOCALAPPDATA 'Microsoft/WinGet/Links/wix.exe')
    )
    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath $candidate) { return $candidate }
    }

    return $null
}

function Find-SignTool {
    $command = Get-Command 'signtool.exe' -ErrorAction SilentlyContinue
    if ($command) { return $command.Source }

    $kits = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits/10/bin'
    if (Test-Path -LiteralPath $kits) {
        $candidate = Get-ChildItem -LiteralPath $kits -Directory |
            Sort-Object Name -Descending |
            ForEach-Object { Join-Path $_.FullName 'x64/signtool.exe' } |
            Where-Object { Test-Path -LiteralPath $_ } |
            Select-Object -First 1
        if ($candidate) { return $candidate }
    }

    return $null
}

function Get-ProductVersion {
    $tag = & git describe --tags --abbrev=0 2>$null
    if ($LASTEXITCODE -ne 0 -or -not $tag) {
        Write-Warning 'git-тег не найден, используется версия 1.0.0.'
        return '1.0.0'
    }

    $value = $tag.Trim() -replace '^v', ''
    $numbers = @()
    foreach ($part in $value.Split('.')) {
        if ($part -match '^\d+$') { $numbers += $part } else { break }
    }
    while ($numbers.Count -lt 3) { $numbers += '0' }
    if ($numbers.Count -gt 3) { $numbers = $numbers[0..2] }

    return ($numbers -join '.')
}

function Invoke-Sign {
    param([string]$FilePath)

    $signTool = Find-SignTool
    if (-not $signTool) {
        throw 'signtool не найден: установите Windows SDK или уберите CertificatePath.'
    }

    $arguments = @('sign', '/fd', 'SHA256', '/tr', $TimestampUrl, '/td', 'SHA256',
        '/f', $CertificatePath)
    if ($CertificatePassword) {
        $arguments += @('/p', $CertificatePassword)
    }
    $arguments += $FilePath

    & $signTool @arguments
    if ($LASTEXITCODE -ne 0) {
        throw "Не удалось подписать: $FilePath"
    }
}

# --- Версия и каталоги -----------------------------------------------------

if (-not $Version) {
    $Version = Get-ProductVersion
}

if ($Version -notmatch '^\d+\.\d+\.\d+$') {
    throw "Версия должна иметь вид x.y.z, получено: $Version"
}

Write-Host "WinSheduler installer, версия $Version" -ForegroundColor White

foreach ($directory in @($artifacts, $uiArtifacts, $serviceArtifacts)) {
    if (-not (Test-Path -LiteralPath $directory)) {
        New-Item -ItemType Directory -Path $directory -Force | Out-Null
    }
}

# --- Служба ----------------------------------------------------------------

Write-Step 'Сборка службы (C++, Release|x64)'
$msbuild = Find-MSBuild
if (-not $msbuild) {
    throw 'MSBuild не найден. Установите Visual Studio 2022/2026 Build Tools с C++-инструментами.'
}

& $msbuild (Join-Path $root 'src/WinShedulerSvc/WinShedulerSvc.vcxproj') `
    "/p:Configuration=$Configuration" '/p:Platform=x64' '/nologo' '/verbosity:minimal'
if ($LASTEXITCODE -ne 0) {
    throw "Сборка службы завершилась с кодом $LASTEXITCODE."
}

$serviceExe = Join-Path $root "src/WinShedulerSvc/bin/$Configuration/WinShedulerSvc.exe"
if (-not (Test-Path -LiteralPath $serviceExe)) {
    throw "Не найден exe службы: $serviceExe"
}

Copy-Item -LiteralPath $serviceExe -Destination $serviceArtifacts -Force

# --- UI self-contained -----------------------------------------------------

Write-Step 'Публикация UI (self-contained, win-x64)'
if (Test-Path -LiteralPath $uiPublish) {
    Remove-Item -LiteralPath $uiPublish -Recurse -Force
}

$publishArguments = @(
    'publish', (Join-Path $root 'src/WinSheduler.UI/WinSheduler.UI.csproj'),
    '-c', $Configuration, '-f', 'net10.0-windows', '-r', 'win-x64', '--self-contained', 'true',
    '-o', $uiPublish, "-p:Version=$Version", "-p:FileVersion=$Version.0",
    "-p:AssemblyVersion=$Version.0", '--nologo'
)
if ($ReadyToRun) {
    $publishArguments += '-p:PublishReadyToRun=true'
}

& dotnet @publishArguments
if ($LASTEXITCODE -ne 0) {
    throw "Публикация UI завершилась с кодом $LASTEXITCODE."
}

$uiExe = Join-Path $uiPublish 'WinSheduler.UI.exe'
if (-not (Test-Path -LiteralPath $uiExe)) {
    throw "В публикации UI нет WinSheduler.UI.exe: $uiPublish"
}

Copy-Item -Path (Join-Path $uiPublish '*') -Destination $uiArtifacts -Recurse -Force

if ($CertificatePath) {
    Write-Step 'Подпись бинарников'
    Invoke-Sign -FilePath (Join-Path $uiPublish 'WinSheduler.UI.exe')
    Invoke-Sign -FilePath (Join-Path $serviceArtifacts 'WinShedulerSvc.exe')
}

if ($SkipMsi) {
    Write-Step 'MSI пропущен (-SkipMsi)'
    Write-Host "    Бинарники: $uiArtifacts, $serviceArtifacts" -ForegroundColor Green
    exit 0
}

# --- DLL действий установщика ---------------------------------------------

Write-Step 'Сборка DLL действий установщика'
$caProject = Join-Path $root 'installer/ca/WinShedulerCA.vcxproj'
& $msbuild $caProject "/p:Configuration=$Configuration" '/p:Platform=x64' '/nologo' '/verbosity:minimal'
if ($LASTEXITCODE -ne 0) {
    throw "Сборка WinShedulerCA завершилась с кодом $LASTEXITCODE."
}

$caDll = Join-Path $root "installer/ca/bin/$Configuration/WinShedulerCA.dll"
if (-not (Test-Path -LiteralPath $caDll)) {
    throw "Не найдена DLL действий: $caDll"
}

# --- Компоненты UI для WiX -------------------------------------------------

Write-Step 'Генерация компонентов UI'
$generated = Join-Path $tempRoot 'generated/UiComponents.wxs'
& (Join-Path $PSScriptRoot 'gen-components.ps1') `
    -SourceDirectory $uiPublish `
    -DirectoryId 'UIDIR' `
    -ComponentGroupId 'UiComponents' `
    -OutputPath $generated
if ($LASTEXITCODE -ne 0) {
    throw 'Генерация компонентов UI завершилась ошибкой.'
}

# --- MSI -------------------------------------------------------------------

Write-Step 'Сборка MSI (WiX)'
$wix = Find-Wix
if (-not $wix) {
    $message = 'WiX не найден. Установите: dotnet tool install --global wix; затем: wix extension add -g WixToolset.UI.wixext'
    if ($RequireWix) {
        throw $message
    }

    Write-Warning $message
    Write-Warning 'MSI не собран. Бинарники опубликованы в ./artifacts.'
    exit 0
}

$wixArguments = @(
    'build',
    '-arch', 'x64',
    '-ext', 'WixToolset.UI.wixext',
    '-culture', 'ru-RU',
    '-d', "ProductVersion=$Version",
    '-d', "ServiceExe=$(Join-Path $serviceArtifacts 'WinShedulerSvc.exe')",
    '-d', "UiIcon=$(Join-Path $root 'ico/installer/app.ico')",
    '-d', "BannerBmp=$(Join-Path $root 'ico/installer/banner.bmp')",
    '-d', "DialogBmp=$(Join-Path $root 'ico/installer/dialog.bmp')",
    '-d', "CaDll=$caDll",
    '-d', "LicenseTxt=$(Join-Path $root 'installer/LICENSE.txt')",
    '-d', "ThirdPartyNotices=$(Join-Path $root 'installer/THIRD-PARTY-NOTICES.txt')",
    '-d', "LicenseRtf=$(Join-Path $root 'installer/License.rtf')",
    '-o', $msiPath,
    (Join-Path $root 'installer/Package.wxs'),
    (Join-Path $root 'installer/Components.wxs'),
    (Join-Path $root 'installer/ru-RU.wxl'),
    $generated
)

& $wix @wixArguments
if ($LASTEXITCODE -ne 0) {
    throw "wix build завершился с кодом $LASTEXITCODE."
}

if (-not (Test-Path -LiteralPath $msiPath)) {
    throw "MSI не создан: $msiPath"
}

# --- Подпись и манифест ----------------------------------------------------

if ($CertificatePath) {
    Write-Step 'Подпись MSI'
    Invoke-Sign -FilePath $msiPath
}
else {
    Write-Warning 'Сертификат не задан: подпись пропущена (заглушка), выпускается SHA256-манифест.'
    Write-Warning 'SmartScreen может предупреждать о неизвестном издателе; см. ./installer/README.md.'
}

Write-Step 'Манифест SHA256'
$manifest = @()
$manifestTargets = @(
    $msiPath,
    (Join-Path $serviceArtifacts 'WinShedulerSvc.exe'),
    (Join-Path $uiArtifacts 'WinSheduler.UI.exe')
)
foreach ($target in $manifestTargets) {
    if (-not (Test-Path -LiteralPath $target)) { continue }

    $hash = (Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash
    $relative = './' + ((Resolve-Path -LiteralPath $target).Path.Substring($root.Length + 1) -replace '\\', '/')
    $manifest += ('{0}  {1}' -f $hash, $relative)
}

$manifestPath = Join-Path $artifacts 'SHA256SUMS.txt'
$manifest | Set-Content -LiteralPath $manifestPath -Encoding UTF8

# --- Бюджет размера --------------------------------------------------------

$msiSize = (Get-Item -LiteralPath $msiPath).Length
$msiMegabytes = [math]::Round($msiSize / 1MB, 1)
if ($msiSize -gt $maxMsiBytes) {
    $budgetMessage = "Размер MSI $msiMegabytes МБ превышает бюджет 80 МБ."
    if ($EnforceBudgets) {
        throw $budgetMessage
    }
    Write-Warning $budgetMessage
}

Write-Host ''
Write-Host 'Готово.' -ForegroundColor Green
Write-Host "    MSI:        $msiPath ($msiMegabytes МБ)"
Write-Host "    Манифест:   $manifestPath"
Write-Host "    Бинарники:  $uiArtifacts, $serviceArtifacts"
exit 0
