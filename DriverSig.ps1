# Simple PowerShell script to sign NoMoreStealer driver using LazySign
# Usage: Run from anywhere - uses absolute paths
# Use LazySign on github

# disable signing in project settings or create new cert
# the cert its autosigning with isnt trusted by default
# its only trusted locally automatically when you create the project
# not when you share the project, thats why we use signtool for now.
$LazySignPath = "C:\Users\YOURUSERNAME\Downloads\LazySign-main\LazySign-main\lazysign\signtool.exe"
$ProjectDir = "C:\Users\YOURUSERNAME\Downloads\NoMoreStealers-main"
$DriverDir = "$ProjectDir\x64\Release\NoMoreStealer"

Write-Host "Signing NoMoreStealer driver files..." -ForegroundColor Green
Write-Host "Driver directory: $DriverDir" -ForegroundColor Cyan

$SysFile = "$DriverDir\NoMoreStealer.sys"
$CatFile = "$DriverDir\nomorestealer.cat"

if (-not (Test-Path $SysFile)) {
    Write-Host "ERROR: NoMoreStealer.sys not found at $SysFile" -ForegroundColor Red
    exit 1
}

if (-not (Test-Path $CatFile)) {
    Write-Host "ERROR: nomorestealer.cat not found at $CatFile" -ForegroundColor Red
    exit 1
}

Write-Host "Signing NoMoreStealer.sys..." -ForegroundColor Yellow
Write-Host "Command: $LazySignPath sign `"$SysFile`"" -ForegroundColor Gray
& $LazySignPath sign "$SysFile"

Write-Host "Signing nomorestealer.cat..." -ForegroundColor Yellow
Write-Host "Command: $LazySignPath sign `"$CatFile`"" -ForegroundColor Gray
& $LazySignPath sign "$CatFile"

Write-Host "Driver signing completed!" -ForegroundColor Green
