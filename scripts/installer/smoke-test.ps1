<#
.SYNOPSIS
    Smoke-тест установки WinSheduler (чистая ВМ или текущая машина).

.DESCRIPTION
    По умолчанию выполняет только проверки уже установленного продукта:
    служба, аргументы, группа, ACL, файлы, реестр, маркер данных, бюджет MSI.
    Установка и удаление выполняются только при явных ключах -Install/-Uninstall.

    Чеклист контракта: ./docs/contracts/INSTALL.md, раздел 13.

.PARAMETER MsiPath
    Путь к MSI. По умолчанию ./artifacts/WinSheduler.msi.

.PARAMETER Install
    Выполнить silent install (требуются права администратора).

.PARAMETER Uninstall
    Выполнить silent uninstall (данные сохраняются, KEEPDATA=1).

.PARAMETER InstallDir
    Ожидаемый каталог программы.

.PARAMETER DataDir
    Ожидаемый каталог данных.

.PARAMETER MeasureUiStartup
    Замерить время запуска UI до появления главного окна.

.PARAMETER UiStartupBudgetSeconds
    Бюджет времени старта UI. По умолчанию 5 секунд.

.EXAMPLE
    ./scripts/installer/smoke-test.ps1

.EXAMPLE
    ./scripts/installer/smoke-test.ps1 -Install -MeasureUiStartup
#>
[CmdletBinding()]
param(
    [string]$MsiPath,
    [switch]$Install,
    [switch]$Uninstall,
    [string]$InstallDir = (Join-Path $env:ProgramFiles 'WinSheduler'),
    [string]$DataDir = (Join-Path $env:ProgramData 'WinSheduler'),
    [switch]$MeasureUiStartup,
    [double]$UiStartupBudgetSeconds = 5
)

$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
if (-not $MsiPath) {
    $MsiPath = Join-Path $root 'artifacts/WinSheduler.msi'
}

$serviceName = 'WinSheduler'
$groupName = 'WinSheduler Users'
$maxMsiBytes = 80MB

$results = [System.Collections.Generic.List[object]]::new()

function Add-Result {
    param(
        [string]$Name,
        [bool]$Passed,
        [string]$Details = ''
    )

    $results.Add([pscustomobject]@{ Name = $Name; Passed = $Passed; Details = $Details })
    if ($Passed) {
        Write-Host "    [OK]   $Name" -ForegroundColor Green
    }
    else {
        Write-Host "    [FAIL] $Name $Details" -ForegroundColor Red
    }
}

function Test-Administrator {
    $identity = [System.Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = [System.Security.Principal.WindowsPrincipal]::new($identity)
    return $principal.IsInRole([System.Security.Principal.WindowsBuiltInRole]::Administrator)
}

function Invoke-MsiExec {
    param([string[]]$Arguments)

    $process = Start-Process -FilePath 'msiexec.exe' -ArgumentList $Arguments -Wait -PassThru -NoNewWindow
    return $process.ExitCode
}

function Test-PathAcl {
    param(
        [string]$Path,
        [string[]]$ForbiddenSid,
        [string]$Name
    )

    if (-not (Test-Path -LiteralPath $Path)) {
        Add-Result -Name $Name -Passed $false -Details "(нет каталога $Path)"
        return
    }

    $writeMask = [System.Security.AccessControl.FileSystemRights]'Write, Modify, FullControl, WriteData, AppendData, CreateFiles, CreateDirectories, DeleteSubdirectoriesAndFiles, ChangePermissions, TakeOwnership'
    $violations = @()

    foreach ($ace in (Get-Acl -LiteralPath $Path).Access) {
        if ($ace.AccessControlType -ne 'Allow') { continue }

        $sid = $ace.IdentityReference.Translate([System.Security.Principal.SecurityIdentifier]).Value
        if ($ForbiddenSid -notcontains $sid) { continue }

        if (($ace.FileSystemRights -band $writeMask) -ne 0) {
            $violations += "${sid}:$($ace.FileSystemRights)"
        }
    }

    Add-Result -Name $Name -Passed ($violations.Count -eq 0) -Details ($violations -join '; ')
}

Write-Host "Smoke-тест WinSheduler" -ForegroundColor White
Write-Host "    MSI:       $MsiPath"
Write-Host "    INSTALLDIR: $InstallDir"
Write-Host "    DATADIR:    $DataDir"

# --- Установка/удаление (только по явному запросу) -------------------------

if ($Install -or $Uninstall) {
    if (-not (Test-Administrator)) {
        throw 'Для -Install/-Uninstall требуются права администратора.'
    }

    $logPath = Join-Path $root 'temp/installer/smoke-msiexec.log'
    $logDirectory = Split-Path -Parent $logPath
    if (-not (Test-Path -LiteralPath $logDirectory)) {
        New-Item -ItemType Directory -Path $logDirectory -Force | Out-Null
    }

    if ($Install) {
        Write-Host '==> Silent install' -ForegroundColor Cyan
        $code = Invoke-MsiExec -Arguments @('/i', $MsiPath, '/qn', '/l*v', $logPath)
        Add-Result -Name 'msiexec /i' -Passed ($code -eq 0) -Details "код $code, лог $logPath"
    }

    if ($Uninstall) {
        Write-Host '==> Silent uninstall (KEEPDATA=1)' -ForegroundColor Cyan
        $code = Invoke-MsiExec -Arguments @('/x', $MsiPath, '/qn', '/l*v', $logPath)
        Add-Result -Name 'msiexec /x' -Passed ($code -eq 0) -Details "код $code, лог $logPath"
    }
}

# --- MSI и манифест --------------------------------------------------------

if (Test-Path -LiteralPath $MsiPath) {
    $size = (Get-Item -LiteralPath $MsiPath).Length
    Add-Result -Name 'MSI существует' -Passed $true -Details ("{0:N1} МБ" -f ($size / 1MB))
    Add-Result -Name 'Бюджет MSI 80 МБ' -Passed ($size -le $maxMsiBytes)

    $manifestPath = Join-Path (Split-Path -Parent $MsiPath) 'SHA256SUMS.txt'
    if (Test-Path -LiteralPath $manifestPath) {
        $hash = (Get-FileHash -LiteralPath $MsiPath -Algorithm SHA256).Hash
        $line = Get-Content -LiteralPath $manifestPath |
            Where-Object { $_ -match 'WinSheduler\.msi$' } |
            Select-Object -First 1
        Add-Result -Name 'SHA256 совпадает с манифестом' -Passed ($line -and $line.StartsWith($hash))
    }
    else {
        Add-Result -Name 'SHA256-манифест' -Passed $false -Details '(нет SHA256SUMS.txt)'
    }
}
else {
    Add-Result -Name 'MSI существует' -Passed $false -Details $MsiPath
}

# --- Служба ----------------------------------------------------------------

$service = Get-Service -Name $serviceName -ErrorAction SilentlyContinue
Add-Result -Name 'Служба зарегистрирована' -Passed ($null -ne $service)

if ($service) {
    Add-Result -Name 'Служба запущена' -Passed ($service.Status -eq 'Running') -Details $service.Status

    $cim = Get-CimInstance -ClassName Win32_Service -Filter "Name='$serviceName'"
    Add-Result -Name 'Учётная запись LocalSystem' -Passed ($cim.StartName -eq 'LocalSystem') -Details $cim.StartName
    Add-Result -Name 'Тип запуска auto' -Passed ($cim.StartMode -eq 'Auto') -Details $cim.StartMode

    $delayed = (Get-ItemProperty -LiteralPath "HKLM:/SYSTEM/CurrentControlSet/Services/$serviceName" `
            -Name DelayedAutoStart -ErrorAction SilentlyContinue).DelayedAutoStart
    Add-Result -Name 'Delayed auto start' -Passed ($delayed -eq 1) -Details "DelayedAutoStart=$delayed"

    $normalizedData = ($DataDir -replace '\\', '/').TrimEnd('/')
    Add-Result -Name 'Аргументы --dbpath/--logdir' `
        -Passed ($cim.PathName -like "*--dbpath*$normalizedData*--logdir*") `
        -Details $cim.PathName
}

# --- Файлы -----------------------------------------------------------------

$expectedFiles = @(
    (Join-Path $InstallDir 'service/WinShedulerSvc.exe'),
    (Join-Path $InstallDir 'ui/WinSheduler.UI.exe'),
    (Join-Path $InstallDir 'LICENSE.txt'),
    (Join-Path $InstallDir 'THIRD-PARTY-NOTICES.txt')
)
foreach ($file in $expectedFiles) {
    Add-Result -Name "Файл $($file.Substring($InstallDir.Length + 1))" -Passed (Test-Path -LiteralPath $file)
}

# --- Группа и реестр -------------------------------------------------------

$group = Get-LocalGroup -Name $groupName -ErrorAction SilentlyContinue
Add-Result -Name 'Группа WinSheduler Users' -Passed ($null -ne $group)

$registryKey = 'HKLM:/SOFTWARE/WinSheduler'
$registry = Get-ItemProperty -LiteralPath $registryKey -ErrorAction SilentlyContinue
Add-Result -Name 'Реестр: InstallDir' -Passed ($registry.InstallDir -eq $InstallDir) -Details $registry.InstallDir
Add-Result -Name 'Реестр: DataDir' -Passed ($registry.DataDir -eq $DataDir) -Details $registry.DataDir
Add-Result -Name 'Реестр: GroupSid' -Passed (-not [string]::IsNullOrWhiteSpace($registry.GroupSid))

# --- ACL -------------------------------------------------------------------

$usersSid = @('S-1-5-32-545', 'S-1-5-11', 'S-1-1-0')
Test-PathAcl -Path $InstallDir -ForbiddenSid $usersSid -Name 'ACL INSTALLDIR: Users без записи'
Test-PathAcl -Path $DataDir -ForbiddenSid $usersSid -Name 'ACL DATADIR: Users без записи'

# --- Данные ----------------------------------------------------------------

Add-Result -Name 'Маркер данных' -Passed (Test-Path -LiteralPath (Join-Path $DataDir '.winsheduler-data'))
Add-Result -Name 'Каталог БД' -Passed (Test-Path -LiteralPath (Join-Path $DataDir 'data'))
Add-Result -Name 'Каталог логов' -Passed (Test-Path -LiteralPath (Join-Path $DataDir 'logs'))

# --- Старт UI --------------------------------------------------------------

if ($MeasureUiStartup) {
    $uiExe = Join-Path $InstallDir 'ui/WinSheduler.UI.exe'
    if (Test-Path -LiteralPath $uiExe) {
        $stopwatch = [System.Diagnostics.Stopwatch]::StartNew()
        $process = Start-Process -FilePath $uiExe -PassThru
        $deadline = (Get-Date).AddSeconds(30)
        while (-not $process.HasExited -and $process.MainWindowHandle -eq 0 -and (Get-Date) -lt $deadline) {
            Start-Sleep -Milliseconds 100
            $process.Refresh()
        }
        $stopwatch.Stop()

        $windowReady = $process.MainWindowHandle -ne 0
        Add-Result -Name 'UI: главное окно' -Passed $windowReady
        if ($windowReady) {
            $seconds = [math]::Round($stopwatch.Elapsed.TotalSeconds, 2)
            Add-Result -Name "UI: старт не дольше $UiStartupBudgetSeconds с" `
                -Passed ($seconds -le $UiStartupBudgetSeconds) -Details "$seconds с"
        }

        if (-not $process.HasExited) {
            Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
        }
    }
    else {
        Add-Result -Name 'UI: exe найден' -Passed $false -Details $uiExe
    }
}

# --- Итог ------------------------------------------------------------------

$failed = @($results | Where-Object { -not $_.Passed })
Write-Host ''
if ($failed.Count -gt 0) {
    Write-Host "Провалено проверок: $($failed.Count) из $($results.Count)" -ForegroundColor Red
    exit 1
}

Write-Host "Все проверки пройдены: $($results.Count)" -ForegroundColor Green
exit 0
