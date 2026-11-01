<#
.SYNOPSIS
    Заглушка бенчмарков WinSheduler.

.DESCRIPTION
    Замеры производительности (тик планировщика, обновление UI, память)
    появятся в Track G. Пока скрипт ничего не измеряет и завершается успешно.
#>
[CmdletBinding()]
param()

Write-Host 'Бенчмарки будут добавлены в Track G.' -ForegroundColor Yellow
exit 0
