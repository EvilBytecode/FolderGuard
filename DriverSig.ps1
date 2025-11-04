# Simple PowerShell script to sign NoMoreStealer driver using LazySign
# Usage: Run from anywhere - uses absolute paths
# Use LazySign on github
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
