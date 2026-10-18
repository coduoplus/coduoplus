param(
    [ValidateSet("Release", "Debug")]
    [string]$Configuration = "Release",
    [switch]$Clean,
    [switch]$Test     # run ctest afterwards (needs the CoD:UO install, see docs/TESTING.md)
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$buildDir = Join-Path $repoRoot "build"

function Find-Tool {
    param(
        [string]$Name,
        [string[]]$Candidates
    )

    foreach ($candidate in $Candidates) {
        if ($candidate -and (Test-Path $candidate)) {
            return (Resolve-Path $candidate).Path
        }
    }

    return $null
}

$binCandidates = @(
    "C:\tools\mingw32\bin",
    (Join-Path $repoRoot "tools\mingw\bin"),
    (Join-Path $repoRoot "cod2x\tools\mingw\bin"),
    "C:\msys64\mingw32\bin",
    "C:\mingw32\bin"
)

$cmakeCandidates = @(
    "C:\tools\mingw32\bin\cmake.exe",
    (Join-Path $repoRoot "tools\mingw\bin\cmake.exe"),
    (Join-Path $repoRoot "cod2x\tools\mingw\bin\cmake.exe"),
    "C:\msys64\mingw32\bin\cmake.exe",
    "C:\Program Files\CMake\bin\cmake.exe",
    "C:\Program Files (x86)\CMake\bin\cmake.exe"
)

$makeCandidates = @(
    "C:\tools\mingw32\bin\mingw32-make.exe",
    (Join-Path $repoRoot "tools\mingw\bin\mingw32-make.exe"),
    (Join-Path $repoRoot "cod2x\tools\mingw\bin\mingw32-make.exe"),
    "C:\msys64\mingw32\bin\mingw32-make.exe",
    "C:\mingw32\bin\mingw32-make.exe"
)

$mingwBin = Find-Tool -Name "MinGW bin" -Candidates $binCandidates
$cmakeExe = Find-Tool -Name "cmake.exe" -Candidates $cmakeCandidates
$makeExe = Find-Tool -Name "mingw32-make.exe" -Candidates $makeCandidates

if (-not $mingwBin) {
    throw @"
Impossible de trouver un toolchain MinGW 32-bit.

Emplacements cherches :
 - .\tools\mingw\bin
 - .\cod2x\tools\mingw\bin
 - C:\msys64\mingw32\bin
 - C:\mingw32\bin

Installe un MinGW-w64 i686 (WinLibs ou MSYS2) puis relance ce script.
"@
}

if (-not $cmakeExe) {
    throw @"
Impossible de trouver cmake.exe.

Installe CMake, ou utilise un package MSYS2/WinLibs qui fournit cmake.exe.
"@
}

if (-not $makeExe) {
    throw @"
Impossible de trouver mingw32-make.exe.

Installe le package make de MinGW 32-bit, puis relance ce script.
"@
}

$gccExe = Join-Path $mingwBin "i686-w64-mingw32-gcc.exe"
$gxxExe = Join-Path $mingwBin "i686-w64-mingw32-g++.exe"

if (-not (Test-Path $gccExe)) {
    $gccExe = Join-Path $mingwBin "gcc.exe"
}

if (-not (Test-Path $gxxExe)) {
    $gxxExe = Join-Path $mingwBin "g++.exe"
}

if (-not (Test-Path $gccExe) -or -not (Test-Path $gxxExe)) {
    throw "Impossible de trouver un compilateur C/C++ 32-bit dans '$mingwBin'."
}

$env:PATH = "$mingwBin;$env:PATH"

if ($Clean -and (Test-Path $buildDir)) {
    Remove-Item -Recurse -Force $buildDir
}

# Generator: keep whatever an existing build dir was configured with (CMake refuses
# to switch), else Ninja when the toolchain ships it (WinLibs does), else make.
$generator = "MinGW Makefiles"
$genArgs = @("-DCMAKE_MAKE_PROGRAM=$makeExe")
$cache = Join-Path $buildDir "CMakeCache.txt"
$ninjaExe = Join-Path $mingwBin "ninja.exe"
if ((Test-Path $cache) -and ((Get-Content $cache -Raw) -match 'CMAKE_GENERATOR:INTERNAL=(.+)')) {
    $generator = $Matches[1].Trim()
} elseif (Test-Path $ninjaExe) {
    $generator = "Ninja"
}
if ($generator -eq "Ninja") { $genArgs = @("-DCMAKE_MAKE_PROGRAM=$ninjaExe") }

& $cmakeExe -S $repoRoot -B $buildDir `
    -G $generator `
    -DCMAKE_BUILD_TYPE=$Configuration `
    @genArgs `
    -DCMAKE_C_COMPILER="$gccExe" `
    -DCMAKE_CXX_COMPILER="$gxxExe"

if ($LASTEXITCODE -ne 0) {
    throw "La configuration CMake a echoue (code $LASTEXITCODE)."
}

& $cmakeExe --build $buildDir --config $Configuration

if ($LASTEXITCODE -ne 0) {
    throw "La compilation a echoue (code $LASTEXITCODE)."
}

if ($Test) {
    $ctestExe = Join-Path (Split-Path -Parent $cmakeExe) "ctest.exe"
    & $ctestExe --test-dir $buildDir --output-on-failure
    if ($LASTEXITCODE -ne 0) { throw "Tests failed (code $LASTEXITCODE)." }
}

$dllPath = Join-Path $buildDir "coduoplus.dll"
$proxyPath = Join-Path $buildDir "mss32.dll"

if (-not (Test-Path $dllPath)) {
    throw "Build terminee, mais '$dllPath' est introuvable."
}

Write-Host ""
Write-Host "Build OK:" $dllPath -ForegroundColor Green
if (Test-Path $proxyPath) {
    Write-Host "Proxy OK:" $proxyPath -ForegroundColor Green
}
