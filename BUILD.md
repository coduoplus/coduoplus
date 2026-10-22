# Building CoDUO+

## Toolchain

CoD:UO is a 32-bit game, so the mod must be a **32-bit** DLL: an `i686`
MinGW-w64, not `x86_64`. CMake refuses a 64-bit compiler.

- **WinLibs** (what this port was built and tested with): the
  `i686-posix-dwarf` MSVCRT release, e.g.
  `winlibs-i686-posix-dwarf-gcc-16.2.0-mingw-w64msvcrt-14.0.0-r2.zip` from
  <https://github.com/brechtsanders/winlibs_mingw/releases>, unzipped so that
  `C:\tools\mingw32\bin\g++.exe` exists. It ships `cmake`, `ninja`, `objdump`
  and `gdb`.
- or **MSYS2**: `pacman -S mingw-w64-i686-gcc mingw-w64-i686-cmake mingw-w64-i686-ninja`.
- or, on Linux/CI: `apt install g++-mingw-w64-i686 cmake` (cross-compile, see
  `.github/workflows/build.yml`).

Python 3 is needed for the tests and the UI pk3.

## Build

```powershell
.\build.ps1                    # Release; finds the toolchain in C:\tools\mingw32, .\tools\mingw, C:\msys64\mingw32, C:\mingw32
.\build.ps1 -Test              # + ctest
.\build.ps1 -Clean             # wipe build\ first
.\build.ps1 -Configuration Debug
```

or by hand:

```sh
export PATH=/c/tools/mingw32/bin:$PATH
cmake -B build -G Ninja -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++ \
      -DCODUO_GAME_DIR="D:/Call of Duty - Copy"
cmake --build build
ctest --test-dir build --output-on-failure
```

Outputs (always in `<repo>\build\`, whatever the build dir, see
`COD1RELOADED_OUTPUT_DIR` in `CMakeLists.txt`):

| File | |
| --- | --- |
| `mss32.dll` | the mod, as the Miles proxy the game loads |
| `coduoplus.dll` | the same code without the proxy exports, for manual injection while debugging |
| `site_check.exe` | the offline patch-site test |

`-DCOD1RELOADED_NO_RINPUT=ON` builds a variant without the raw-input
`GetCursorPos` hook into `build\norinput\`, for bisecting input problems.

## CMake options

| Option | Default | |
| --- | --- | --- |
| `CODUO_GAME_DIR` | `D:/Call of Duty - Copy` | CoD:UO 1.51 install the tests read binaries from (read-only), and the smoke test runs. |
| `CODUO_SMOKE` | `OFF` | Register `smoke_dedicated` (launches the game as a dedicated server). |
| `COD1RELOADED_NO_RINPUT` | `OFF` | Build without the raw-input hook. |

## Deploy to a test install

```powershell
.\deploy.ps1 [-Game "D:\Call of Duty - Copy"]
```

`deploy.ps1` refuses to run while the game is running from that folder. On the
first deploy it renames the game's Miles `mss32.dll` to `mss32_original.dll`
(keeping `mss32.dll.miles-backup`), and installs `coduoplus.ini` only if none
exists. The DLL generates `uo\zz_coduoplus_ui.pk3` itself at the next start.

## Release

Releases are built by CI, not locally: push a tag `vX.Y.Z` and
`.github/workflows/release.yml` builds the DLLs, the installer and the Linux
library on hosted runners, runs the portable tests, and publishes every
download. Every push to `main` publishes the same set as the `nightly`
pre-release. Run the game tests locally before tagging. See
[docs/CI.md](docs/CI.md#cutting-a-release) and [docs/RELEASES.md](docs/RELEASES.md).

To build the downloads locally (needs Inno Setup 6, e.g. in `C:\tools\innosetup`):

```powershell
C:\tools\innosetup\ISCC.exe /DVersion=0.3.0 /DDllDir=$PWD\build /O$PWD\build packaging\coduoplus.iss
python -I packaging\package.py --version 0.3.0 --tag v0.3.0 --repo coduoplus/coduoplus `
    --windows build --linux build\linux --out dist
```

(`--windows` needs `mss32.dll`, `norinput\mss32.dll` and `CoDUOPlus-Setup.exe`;
`--linux` needs `coduoplus_sv.so`, see [docs/LINUX.md](docs/LINUX.md).)

The version lives in one place: `COD1RELOADED_VERSION` in
`src/features/updater.h`.

## Reverse-engineering helpers

`tools/re/` holds the scripts used to find and check addresses. See
[docs/PORTING.md](docs/PORTING.md#method):

```sh
python -I tools/re/find.py <pe> str "r_fullscreen"     # string + code refs
python -I tools/re/find.py <pe> call 43abc0            # E8 callers of a VA
bash tools/re/dis.sh <pe> 0x43c6a7 0x30                # objdump, Intel syntax
python -I tools/re/q3query.py 127.0.0.1 28960 getstatus
python -I tools/re/q3connect.py 127.0.0.1 28960 coduoplus=5
```
