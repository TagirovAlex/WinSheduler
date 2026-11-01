<#
.SYNOPSIS
    Дымовой E2E-тест UI: запускает приложение и проверяет окно через UI Automation.

.DESCRIPTION
    Требует интерактивного рабочего стола (не session 0). Проверяет, что окно
    создано и доступна кнопка «Добавить». Служба может быть не установлена:
    приложение должно показать офлайн-состояние, а не упасть.

.PARAMETER Exe
    Путь к WinSheduler.UI.exe. По умолчанию — Release-сборка.
#>
[CmdletBinding()]
param(
    [string]$Exe = './src/WinSheduler.UI/bin/Release/net8.0-windows/WinSheduler.UI.exe'
)

$ErrorActionPreference = 'Stop'

$exePath = (Resolve-Path -LiteralPath $Exe).Path

Get-Process -Name 'WinSheduler.UI' -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Milliseconds 500

$process = Start-Process -FilePath $exePath -PassThru

try {
    Add-Type -AssemblyName UIAutomationClient
    Add-Type -AssemblyName UIAutomationTypes

    $deadline = (Get-Date).AddSeconds(20)
    $window = $null

    while ((Get-Date) -lt $deadline) {
        $condition = New-Object System.Windows.Automation.PropertyCondition(
            [System.Windows.Automation.AutomationElement]::ProcessIdProperty,
            $process.Id)

        $window = [System.Windows.Automation.AutomationElement]::RootElement.FindFirst(
            [System.Windows.Automation.TreeScope]::Children,
            $condition)

        if ($null -ne $window) { break }
        Start-Sleep -Milliseconds 500
    }

    if ($null -eq $window) {
        throw 'Окно UI не найдено за 20 секунд.'
    }

    $buttonCondition = New-Object System.Windows.Automation.AndCondition(
        (New-Object System.Windows.Automation.PropertyCondition(
            [System.Windows.Automation.AutomationElement]::ControlTypeProperty,
            [System.Windows.Automation.ControlType]::Button)),
        (New-Object System.Windows.Automation.PropertyCondition(
            [System.Windows.Automation.AutomationElement]::NameProperty,
            'Добавить')))

    $button = $window.FindFirst(
        [System.Windows.Automation.TreeScope]::Descendants,
        $buttonCondition)

    if ($null -eq $button) {
        throw 'Кнопка «Добавить» не найдена.'
    }

    Write-Host 'UI smoke: окно и кнопка «Добавить» найдены.' -ForegroundColor Green
}
finally {
    if (-not $process.HasExited) {
        Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
    }
}
