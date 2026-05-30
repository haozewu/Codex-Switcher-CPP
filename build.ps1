# build.ps1 - Build script for CodexSwitcher
# Finds qmake, runs qmake + make/nmake, then windeployqt.

param(
    [switch]$Release = $false,
    [switch]$Clean = $false,
    [switch]$NoDeploy = $false
)

$ErrorActionPreference = "Stop"
$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location -LiteralPath $scriptDir

$buildType = if ($Release) { "release" } else { "debug" }
Write-Host "=== Build config: $buildType ===" -ForegroundColor Cyan

# --- Find Qt tools ---
function Find-QtTool {
    param([string]$toolName)
    $candidates = @()
    $qtRoots = @(
        "C:\Qt",
        "D:\Qt",
        "C:\Users\haoze\AppData\Local\Programs\Qt",
        "D:\Users\haoze\AppData\Local\Programs\Qt"
    )
    foreach ($root in $qtRoots) {
        $candidates += Get-ChildItem -Path $root -Directory -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -match "^5\." -or $_.Name -match "^6\." } |
        Sort-Object Name -Descending |
        ForEach-Object { Get-ChildItem -Path $_.FullName -Directory -Filter "mingw*_64" -ErrorAction SilentlyContinue } |
        ForEach-Object { Join-Path $_.FullName "bin\$toolName.exe" } |
        Where-Object { Test-Path -LiteralPath $_ }
        $candidates += Get-ChildItem -Path $root -Directory -ErrorAction SilentlyContinue |
            Where-Object { $_.Name -match "^6\." -or $_.Name -match "^5\." } |
            Sort-Object Name -Descending |
            ForEach-Object { Get-ChildItem -Path $_.FullName -Directory -Filter "msvc*_64" -ErrorAction SilentlyContinue } |
            ForEach-Object { Join-Path $_.FullName "bin\$toolName.exe" } |
            Where-Object { Test-Path -LiteralPath $_ }
    }
    # Also check PATH
    $candidates += (Get-Command "${toolName}.exe" -ErrorAction SilentlyContinue).Source
    return ($candidates | Where-Object { $_ } | Select-Object -First 1)
}

$qmake = Find-QtTool "qmake"
if (-not $qmake) {
    Write-Host "ERROR: qmake not found. Install Qt (e.g. 5.15.x or 6.x) and retry." -ForegroundColor Red
    Write-Host "Looked in common Qt roots and PATH." -ForegroundColor Yellow
    exit 1
}
Write-Host "qmake: $qmake" -ForegroundColor Green

$windeployqt = Find-QtTool "windeployqt"
if (-not $windeployqt) {
    Write-Host "WARNING: windeployqt not found. Build will skip deployment." -ForegroundColor Yellow
}

# --- Detect make tool ---
$makeTool = $null
if ($qmake -match "mingw") {
    $mingwDir = Split-Path -Parent $qmake
    $makeTest = Join-Path $mingwDir "mingw32-make.exe"
    if (Test-Path $makeTest) {
        $makeTool = $makeTest
    }
    if (-not $makeTool) {
        $qtRoot = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $qmake)))
        $makeTool = Get-ChildItem -Path (Join-Path $qtRoot "Tools") -Recurse -Filter mingw32-make.exe -ErrorAction SilentlyContinue |
            Select-Object -First 1 -ExpandProperty FullName
    }
}
if (-not $makeTool) {
    $makeTool = (Get-Command "nmake.exe" -ErrorAction SilentlyContinue).Source
}
if (-not $makeTool) {
    $makeTool = (Get-Command "mingw32-make.exe" -ErrorAction SilentlyContinue).Source
}
if (-not $makeTool) {
    $makeTool = (Get-Command "make.exe" -ErrorAction SilentlyContinue).Source
}
if (-not $makeTool) {
    Write-Host "ERROR: No make tool found (mingw32-make / nmake / make)." -ForegroundColor Red
    exit 1
}
Write-Host "make:  $makeTool" -ForegroundColor Green
if ($makeTool -match "mingw") {
    $mingwBin = Split-Path -Parent $makeTool
    $env:PATH = "$mingwBin;$env:PATH"
}

# --- Build directory ---
$buildDir = Join-Path $scriptDir "build-$buildType"
if ($Clean -and (Test-Path $buildDir)) {
    Write-Host "Cleaning build directory..." -ForegroundColor Yellow
    Remove-Item -Recurse -Force $buildDir
}
New-Item -ItemType Directory -Force -Path $buildDir | Out-Null

# --- qmake ---
Write-Host "Running qmake..." -ForegroundColor Cyan
$qmakeArgs = @((Join-Path $scriptDir "CodexSwitcher.pro"), "CONFIG+=$buildType")
if ($qmake -match "mingw") {
    $qmakeArgs += @("-spec", "win32-g++")
}
if ($Release) {
    $qmakeArgs += "CONFIG-=debug"
    $qmakeArgs += "CONFIG+=release"
}
Push-Location $buildDir
try {
    & $qmake $qmakeArgs
    if ($LASTEXITCODE -ne 0) { throw "qmake failed" }
} finally {
    Pop-Location
}
Write-Host "qmake OK" -ForegroundColor Green

# --- make ---
Write-Host "Building..." -ForegroundColor Cyan
Push-Location $buildDir
try {
    & $makeTool
    if ($LASTEXITCODE -ne 0) { throw "make failed" }
} finally {
    Pop-Location
}
Write-Host "Build OK" -ForegroundColor Green

# --- windeployqt ---
if (-not $NoDeploy -and $windeployqt) {
    Write-Host "Deploying Qt DLLs..." -ForegroundColor Cyan
    $exeName = "CodexSwitcher.exe"
    $exePath = Get-ChildItem -Path $buildDir -Filter $exeName -Recurse |
        Select-Object -First 1 -ExpandProperty FullName
    if ($exePath) {
        $deployArgs = @($exePath)
        if ($Release) { $deployArgs += "--release" }
        $deployArgs += "--no-translations"
        & $windeployqt $deployArgs
        Write-Host "windeployqt OK" -ForegroundColor Green
        $qtRoot = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $qmake)))
        $openssl = Get-ChildItem -Path $qtRoot -Recurse -Include "libssl-1_1-x64.dll","libcrypto-1_1-x64.dll" -ErrorAction SilentlyContinue |
            Sort-Object FullName |
            Select-Object -Unique
        foreach ($dll in $openssl) {
            Copy-Item -LiteralPath $dll.FullName -Destination (Split-Path -Parent $exePath) -Force
            Write-Host "Copied OpenSSL DLL: $($dll.Name)" -ForegroundColor Green
        }
        Write-Host "Output: $exePath" -ForegroundColor Green
    } else {
        Write-Host "WARNING: $exeName not found, skipping deploy." -ForegroundColor Yellow
    }
} elseif ($NoDeploy) {
    Write-Host "Skipping deploy (--NoDeploy)." -ForegroundColor Yellow
    $exePath = Get-ChildItem -Path $buildDir -Filter "CodexSwitcher.exe" -Recurse |
        Select-Object -First 1 -ExpandProperty FullName
    if ($exePath) { Write-Host "Output: $exePath" -ForegroundColor Green }
}
