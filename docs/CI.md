# CI, branch protection and releases

How CoDUO+ is built and tested on GitHub, why part of it runs on a self-hosted
machine, how `main` is protected, and how a release is cut.

## Workflows

| Workflow | Trigger | Jobs |
| --- | --- | --- |
| `.github/workflows/ci.yml` | push to `main`, pull request into `main`, manual | **`portable`** (hosted Windows), **`linux`** (hosted Ubuntu: the Linux library) and **`game`** (self-hosted) |
| `.github/workflows/release.yml` | tag `v*` pushed, manual on a tag | `build` (hosted), `linux` (hosted), `game` (self-hosted), then `image` (server image to GHCR) and `publish` |

### Why two kinds of runner

The test suite ([TESTING.md](TESTING.md)) has three kinds of test, by CTest label:

| Label | Tests | Needs | Runs on |
| --- | --- | --- | --- |
| `portable` | `doc_links`, `ini_lock`, `competitive_sv` | only the build | hosted Windows runner |
| `game` | `fetch_binaries`, `site_check`, `site_check_2005`, `site_check_linux`, `ui_pk3`, `smoke_dedicated`, `smoke_linux` | a CoD:UO 1.51 install; the smoke test **launches the game** | self-hosted |
| `timing` | `frame_limiter` | a quiet machine (wall-clock measurement) | self-hosted |

The `game` tests read Activision's binaries (`CoDUOMP.exe`, the `uo_*_mp_x86.dll`
modules from `pakuo07.pk3`) and run the real game. Those files can't be put in
the repository or on a hosted runner without redistributing them. So they run
on a machine that has the game: a self-hosted runner with the labels
`self-hosted, windows, coduo-game`.

The hosted `portable` job still compiles *everything* (both DLLs and every test
program) with the pinned toolchain, so a compile error is caught even when the
self-hosted machine is off.

### Toolchain

Both runners use the same compiler, WinLibs GCC 16.2.0 i686 (posix, dwarf,
MSVCRT), from
`winlibs-i686-posix-dwarf-gcc-16.2.0-mingw-w64msvcrt-14.0.0-r2.zip`. On the hosted
runner it is downloaded once and cached; on the self-hosted machine it is
expected in `C:\tools\mingw32`.

## Branch protection on `main`

- **Required status checks:** `portable`, `linux` and `game`, with "require branches to be
  up to date" on, so a change is tested against the current `main`.
- **Pull requests required:** no direct pushes; admins included.
- No force-pushes, no deletion.

So nothing reaches `main` unless the whole suite passed on that exact merge
result, including the live dedicated-server run.

## Security of the self-hosted runner

A self-hosted runner executes whatever the workflow tells it to, on that
machine, with that user's rights. A pull-request workflow runs the workflow
file *from the pull request*, so anyone who can open a PR can change what runs.

- The `game` job is skipped for pull requests from forks (`if:` in `ci.yml`).
  That alone isn't enough, because a fork can edit that `if:`. The real controls are:
- **Keep the repository private**, or, if public, set *Settings → Actions → General →
  Fork pull request workflows → Require approval for all outside collaborators*.
  Never approve a fork run without reading its workflow changes.
- A skipped job counts as passing for branch protection. To merge an outside
  contribution, a maintainer pushes it to a branch of this repo and opens the
  PR from there, so `game` actually runs.
- Run the runner as a normal user (not an administrator), and give it its own
  copy of the game install: `deploy.ps1` overwrites `mss32.dll` there on every
  run.

## Setting up the self-hosted runner

On the machine with the CoD:UO install and `C:\tools\mingw32`:

1. *Repo → Settings → Actions → Runners → New self-hosted runner → Windows x64*,
   and follow the download/configure commands. When asked for labels, add
   **`coduo-game`** (`self-hosted` and `windows` are added automatically).
2. Run it (`run.cmd`) or install it as a service.
3. If the install isn't at `D:/Call of Duty - Copy`, set the repository
   variable **`CODUO_GAME_DIR`** (*Settings → Secrets and variables → Actions →
   Variables*).
4. Python 3 must be on that user's `PATH` (the tests use it).

## Cutting a release

1. Bump `COD1RELOADED_VERSION` in `src/features/updater.h`, add the
   `## vX.Y.Z` section to `CHANGELOG.md`, and merge through a PR.
2. Tag and push: `git tag v0.2.0 && git push origin v0.2.0`.
3. `release.yml`:
   - **build:** fails unless the tag equals `COD1RELOADED_VERSION`; builds the
     standard and no-rinput DLLs, proves they differ, and runs the portable tests;
   - **game:** runs the full suite against the install, including the smoke test,
     and generates `zz_coduoplus_ui.pk3` from the install's `pakuo03.pk3`;
   - **publish** (only if both passed): creates the release with the CHANGELOG
     section as notes, and attaches `mss32.dll`, `mss32-norinput.dll`,
     `coduoplus.ini`, `competitive.cfg.example`, `zz_coduoplus_ui.pk3`,
     `manifest.json`, and `coduoplus-X.Y.Z.zip` / `-norinput.zip` (laid out like the
     game folder).
4. To point installed copies at the release feed, set `updater_manifest_url` to
   `https://github.com/<org>/coduoplus/releases/latest/download/manifest.json`.
   CoDUO+ ships with an empty URL, see [PORTING.md](PORTING.md#updater-and-pam-installer-point-nowhere-by-default).
