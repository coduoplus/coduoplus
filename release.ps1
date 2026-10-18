# Build + package a CoDUO+ release, ready to upload to GitHub.
#
# Usage:
#   .\release.ps1                                   (version read from src/features/updater.h)
#   .\release.ps1 -Version 0.1.1                    (override)
#   .\release.ps1 -Repo owner/name                  (repo the manifest's download_url points at)
#   .\release.ps1 -Game "D:\Call of Duty - Copy"    (install the UI pk3 is generated from)
#
# Runs the tests first (ctest: site_check against the stock 1.51 binaries in -Game)
# and refuses to package a build that fails them.
#
# Produces in dist/:
#   - mss32.dll                   (upload to the Release)
#   - manifest.json               (upload to the Release; the updater reads it)
#   - zz_coduoplus_ui.pk3         (upload to the Release; generated, see ui_pk3/build_pk3.py)
#   - coduoplus-<ver>.zip         (player package: mss32.dll, coduoplus.ini, uo\zz_coduoplus_ui.pk3, README)
param(
    [string]$Version = "",
    [string]$Repo = "",
    [string]$Game = "D:\Call of Duty - Copy"
)

$ErrorActionPreference = "Stop"
$root = $PSScriptRoot

# --- Version: single source of truth = src/features/updater.h ---
if (-not $Version) {
    $h = Get-Content (Join-Path $root "src\features\updater.h") -Raw
    if ($h -match 'COD1RELOADED_VERSION\s*=\s*"([0-9.]+)"') { $Version = $Matches[1] }
    else { Write-Error "Version not found in src/features/updater.h"; exit 1 }
}
Write-Host "=== CoDUO+ release v$Version ===" -ForegroundColor Cyan

# --- 32-bit MinGW toolchain (WinLibs i686 in C:\tools\mingw32, or MSYS2 mingw32) ---
$cands = @("C:\tools\mingw32\bin", "$root\tools\mingw\bin", "C:\msys64\mingw32\bin", "C:\mingw32\bin")
$mingw = $cands | Where-Object { Test-Path (Join-Path $_ "g++.exe") } | Select-Object -First 1
if (-not $mingw) { Write-Error "32-bit MinGW not found (looked in: $($cands -join ', '))"; exit 1 }
$env:PATH = "$mingw;$env:PATH"

$build = Join-Path $root "build-release"
& cmake -S $root -B $build -G Ninja -DCMAKE_BUILD_TYPE=Release `
    -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++ "-DCODUO_GAME_DIR=$Game" | Out-Null
if ($LASTEXITCODE) { Write-Error "CMake configure failed"; exit 1 }

Write-Host "Building..."
& cmake --build $build
if ($LASTEXITCODE) { Write-Error "Build failed"; exit 1 }
$dll = Join-Path $build "mss32.dll"
if (-not (Test-Path $dll)) { $dll = Join-Path $root "build\mss32.dll" }  # COD1RELOADED_OUTPUT_DIR
if (-not (Test-Path $dll)) { Write-Error "mss32.dll not found after build"; exit 1 }

Write-Host "Testing..."
& ctest --test-dir $build --output-on-failure
if ($LASTEXITCODE) { Write-Error "Tests failed - not packaging this build"; exit 1 }

# --- dist/ ---
$dist = Join-Path $root "dist"
Remove-Item -Recurse -Force $dist -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force $dist | Out-Null
Copy-Item $dll (Join-Path $dist "mss32.dll")
Copy-Item (Join-Path $root "coduoplus.ini") (Join-Path $dist "coduoplus.ini")
& python -I (Join-Path $root "ui_pk3\build_pk3.py") --game $Game
if ($LASTEXITCODE) { Write-Error "UI pk3 build failed"; exit 1 }

$readme = @"
CoDUO+ $Version  -  Call of Duty: United Offensive 1.51 client mod

Install:
1. In your game folder (the one with CoDUOMP.exe), rename mss32.dll to
   mss32_original.dll. Do not skip this: the mod forwards all audio to it.
2. Copy mss32.dll and coduoplus.ini next to CoDUOMP.exe.
3. Copy uo\zz_coduoplus_ui.pk3 into the game's uo\ folder.
4. Start CoDUOMP.exe as usual. coduoplus.log appears next to the DLL.

Uninstall: delete mss32.dll, coduoplus.ini and uo\zz_coduoplus_ui.pk3, then
rename mss32_original.dll back to mss32.dll.

Full documentation: README.md and docs\ in the source repository.
"@
Set-Content (Join-Path $dist "README.txt") $readme -Encoding ASCII

# --- manifest.json (dist + repo root) ---
$url = if ($Repo) { "https://github.com/$Repo/releases/latest/download/mss32.dll" } else { "" }
$manifest = @"
{
  "version": "$Version",
  "download_url": "$url",
  "notes": "CoDUO+ $Version - see CHANGELOG"
}
"@
Set-Content (Join-Path $dist "manifest.json") $manifest -Encoding ASCII
Set-Content (Join-Path $root "manifest.json") $manifest -Encoding ASCII

# --- player zip, mirroring the game folder layout ---
$stage = Join-Path $dist "stage"
New-Item -ItemType Directory -Force (Join-Path $stage "uo") | Out-Null
Copy-Item (Join-Path $dist "mss32.dll"), (Join-Path $dist "coduoplus.ini"), (Join-Path $dist "README.txt") $stage
Copy-Item (Join-Path $root "competitive.cfg.example") $stage   # for server admins (docs/NETCODE.md)
Copy-Item (Join-Path $dist "zz_coduoplus_ui.pk3") (Join-Path $stage "uo")
$zip = Join-Path $dist "coduoplus-$Version.zip"
Compress-Archive -Path (Join-Path $stage "*") -DestinationPath $zip -Force
Remove-Item -Recurse -Force $stage

Write-Host ""
Write-Host "OK -> $dist" -ForegroundColor Green
Write-Host "Create the GitHub release 'v$Version' and attach:" -ForegroundColor Yellow
Write-Host "  - mss32.dll, manifest.json, zz_coduoplus_ui.pk3, coduoplus-$Version.zip"
if (-not $Repo) { Write-Host "  (manifest download_url is empty: pass -Repo owner/name to point the updater at it)" }
