@echo off
set "DIR=%~dp0"
set "SQLITE_URL=https://www.sqlite.org/2026/sqlite-amalgamation-3530200.zip"
set "ZIP_FILE=%TEMP%\sqlite.zip"
set "EXTRACT_DIR=%TEMP%\sqlite_amalg"

echo Downloading SQLite amalgamation...
powershell -Command "& { Invoke-WebRequest -Uri '%SQLITE_URL%' -OutFile '%ZIP_FILE%' }"

if not exist "%ZIP_FILE%" (
    echo Failed to download. Please download manually from:
    echo %SQLITE_URL%
    echo and extract sqlite3.c + sqlite3.h into %DIR%
    pause
    exit /b 1
)

echo Extracting...
powershell -Command "& { Expand-Archive -Path '%ZIP_FILE%' -DestinationPath '%EXTRACT_DIR%' -Force }"

copy /Y "%EXTRACT_DIR%\sqlite-amalgamation-3470200\sqlite3.h" "%DIR%"
copy /Y "%EXTRACT_DIR%\sqlite-amalgamation-3470200\sqlite3.c" "%DIR%"

echo Done! sqlite3.h and sqlite3.c extracted to %DIR%
pause
