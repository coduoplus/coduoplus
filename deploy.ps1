# Deploy the fresh build into a CoD:UO game folder.
# Usage:  .\deploy.ps1 [-Game "D:\Call of Duty - Copy"]   (close the game first)
#
# First run in a folder: the game's own Miles mss32.dll is renamed to
# mss32_original.dll (the proxy forwards every audio call to it) and a copy is
# kept as mss32.dll.miles-backup. Later runs only replace our mss32.dll.
# coduoplus.ini is copied only if the folder has none, so a tuned ini survives.
param(
    [string]$Game = "D:\Call of Duty - Copy"
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path (Join-Path $Game "CoDUOMP.exe"))) {
    Write-Host "Not a CoD:UO folder (no CoDUOMP.exe): $Game"
    exit 1
}

$build = Join-Path $PSScriptRoot "build\mss32.dll"
if (-not (Test-Path $build)) { Write-Host "build\mss32.dll not found - build first."; exit 1 }

$running = Get-Process CoDUOMP -ErrorAction SilentlyContinue |
    Where-Object { $_.Path -and $_.Path.StartsWith($Game, [StringComparison]::OrdinalIgnoreCase) }
if ($running) {
    Write-Host "CoDUOMP.exe is running from this folder -> close it, then re-run."
    exit 1
}

$dst  = Join-Path $Game "mss32.dll"
$orig = Join-Path $Game "mss32_original.dll"
if (-not (Test-Path $orig)) {
    # The Miles DLL exports _AIL_startup@0; ours forwards it. A file that is
    # already a proxy (forwarders to mss32_original) must not become the "original".
    $text = [System.Text.Encoding]::ASCII.GetString([System.IO.File]::ReadAllBytes($dst))
    if ($text.Contains("mss32_original.")) {
        Write-Host "mss32.dll is already a proxy but mss32_original.dll is missing - restore the Miles DLL first."
        exit 1
    }
    Copy-Item $dst (Join-Path $Game "mss32.dll.miles-backup")
    Move-Item $dst $orig
    Write-Host "First deploy: Miles mss32.dll -> mss32_original.dll (backup: mss32.dll.miles-backup)"
}

Copy-Item $build $dst -Force
$ini = Join-Path $Game "coduoplus.ini"
if (-not (Test-Path $ini)) {
    Copy-Item (Join-Path $PSScriptRoot "coduoplus.ini") $ini
    Write-Host "coduoplus.ini installed (defaults)"
}
$g = Get-Item $dst
Write-Host ("OK -> mss32.dll deployed ({0} bytes, {1})" -f $g.Length, $g.LastWriteTime)
