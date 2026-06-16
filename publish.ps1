$publishDir = Join-Path $PSScriptRoot "published"
$rid = "win-x64"
$cppProject = Join-Path $PSScriptRoot "src\WinShedulerSvc\WinShedulerSvc.vcxproj"

# Clean
if (Test-Path $publishDir) { Remove-Item $publishDir -Recurse -Force }
New-Item $publishDir -ItemType Directory -Force -ErrorAction SilentlyContinue | Out-Null

Write-Host "Building C++ service..." -ForegroundColor Green
# First, check if sqlite3.c exists
$sqlite3c = Join-Path $PSScriptRoot "src\WinShedulerSvc\sqlite3.c"
$sqlite3h = Join-Path $PSScriptRoot "src\WinShedulerSvc\sqlite3.h"
if (-not (Test-Path $sqlite3c) -or -not (Test-Path $sqlite3h)) {
    Write-Host "sqlite3 amalgamation not found. Run get_sqlite3.bat first." -ForegroundColor Yellow
    Write-Host "Downloading..." -ForegroundColor Yellow
    & "$env:COMSPEC" /c "cd /d `"$PSScriptRoot\src\WinShedulerSvc`" && get_sqlite3.bat"
}

# Build C++ with MSBuild
$msbuild = &"${env:ProgramFiles}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.Component.MSBuild -property installationPath
if (-not $msbuild) {
    $msbuild = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\2019\BuildTools\MSBuild\Current\Bin\MSBuild.exe"
    if (-not (Test-Path $msbuild)) { $msbuild = "MSBuild.exe" }
}
$msbuildExe = Join-Path $msbuild "MSBuild.exe"
if (-not (Test-Path $msbuildExe)) { $msbuildExe = "MSBuild.exe" }

& $msbuildExe $cppProject /p:Configuration=Release /p:Platform=x64
if ($LASTEXITCODE -ne 0) { throw "C++ build failed" }

# Copy service
$cppOut = Join-Path $PSScriptRoot "src\WinShedulerSvc\bin\Release\WinShedulerSvc.exe"
$serviceDir = Join-Path $publishDir "service"
New-Item $serviceDir -ItemType Directory -Force | Out-Null
Copy-Item $cppOut $serviceDir

Write-Host "Publishing UI..." -ForegroundColor Green
dotnet publish (Join-Path $PSScriptRoot "src\WinSheduler.UI\WinSheduler.UI.csproj") -c Release -r $rid --self-contained false -o (Join-Path $publishDir "ui")
if ($LASTEXITCODE -ne 0) { throw "UI publish failed" }

Write-Host "`nDone!" -ForegroundColor Green
Write-Host "Structure:" -ForegroundColor Cyan
Get-ChildItem $publishDir -Directory | ForEach-Object {
    Write-Host "  $($_.Name)/"
    Get-ChildItem $_.FullName -Name | ForEach-Object { Write-Host "    $_" }
}
