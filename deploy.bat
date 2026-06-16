@echo off
chcp 1251 >nul
set "DIR=%~dp0"

echo Installing WinSheduler service...
sc create WinSheduler binPath="%DIR%published\service\WinShedulerSvc.exe" start=auto DisplayName="WinSheduler Task Scheduler"

if %ERRORLEVEL% equ 0 (
    echo Service created successfully.
    echo Starting service...
    sc start WinSheduler
) else (
    echo Failed to create service. Try running as Administrator.
)

echo.
echo To launch UI: %DIR%published\ui\WinSheduler.UI.exe
echo To uninstall: sc stop WinSheduler ^&^& sc delete WinSheduler
pause
