# CI, branch protection and releases

How CoDUO+ is built and tested on GitHub, how `main` is protected, and how a
release is cut. Everything runs on GitHub-hosted runners: there is no
self-hosted runner to keep online.

## Workflows

| Workflow | Trigger | Jobs |
| --- | --- | --- |
| `.github/workflows/ci.yml` | push to `main`, pull request into `main`, manual | **`portable`** (hosted Windows) and **`linux`** (hosted Ubuntu: the Linux library), the required checks |
| `.github/workflows/release.yml` | tag `v*` pushed → release `vX.Y.Z`; push to `main` → pre-release `nightly`; manual on a tag | `build` and `linux` (hosted), then `image` (server image to GHCR) and `publish` (every download, [RELEASES.md](RELEASES.md)) |

### What CI does and doesn't test

The test suite ([TESTING.md](TESTING.md)) has three kinds of test, by CTest label:

| Label | Tests | Needs | Runs |
| --- | --- | --- | --- |
| `portable` | `doc_links`, `ini_lock`, `competitive_sv`, `antilag`, `ui_pk3_gen`, `installer` | only the build (`installer`: Inno Setup, installed by CI) | in CI |
| `game` | `fetch_binaries`, `site_check`, `site_check_2005`, `site_check_linux`, `ui_pk3`, `ui_pk3_gen_game`, `smoke_dedicated`, `smoke_linux` | a CoD:UO 1.51 install; the smoke test **launches the game** | locally |
| `timing` | `frame_limiter` | a quiet machine (wall-clock measurement) | locally |

The `game` tests read Activision's binaries (`CoDUOMP.exe`, the `uo_*_mp_x86.dll`
modules from `pakuo07.pk3`) and run the real game. Those files can't be put in
the repository or on a hosted runner without redistributing them, so these tests
run on a developer's machine with the game, before merging and before tagging
(see [TESTING.md](TESTING.md)).

The hosted `portable` job still compiles *everything* (both DLLs and every test
program) with the pinned toolchain, so a compile error is always caught.

What's given up: a change that compiles and passes the portable tests, but breaks
against the real binaries, can reach `main` and a release unless the game suite
was run locally first. The UI pk3 needs no game on the runner: the DLL generates
it on each player's machine from their own `pakuo03.pk3`
([RELEASES.md](RELEASES.md#the-main-menu-entry-built-by-the-dll)).

### Toolchain

CI and local builds use the same compiler, WinLibs GCC 16.2.0 i686 (posix,
dwarf, MSVCRT), from
`winlibs-i686-posix-dwarf-gcc-16.2.0-mingw-w64msvcrt-14.0.0-r2.zip`. In CI it is
downloaded once and cached; locally it is expected in `C:\tools\mingw32`.

The player installer is compiled with Inno Setup 6.7.3, installed by
[`.github/actions/setup-innosetup`](../.github/actions/setup-innosetup/action.yml)
into `C:\tools\innosetup` after an SHA-256 check (locally: the same folder, or
`Program Files (x86)\Inno Setup 6`). Without it, CMake skips the `installer` test.

## Branch protection on `main`

- **Required status checks:** `portable` and `linux` (hosted), with "require branches to
  be up to date" on, so a change is tested against the current `main`.
- **Pull requests required:** no direct pushes; admins included.
- No force-pushes, no deletion.

So nothing reaches `main` unless the hosted jobs passed on that exact merge
result, and nothing is released unless the hosted build and portable tests
passed on the tagged commit.

## Cutting a release

1. Bump `COD1RELOADED_VERSION` in `src/features/updater.h`, add the
   `## vX.Y.Z` section to `CHANGELOG.md`, and merge through a PR. Run the full
   suite locally on that commit first (`ctest --test-dir build`, see
   [TESTING.md](TESTING.md)).
2. Tag and push: `git tag v0.2.0 && git push origin v0.2.0`.
3. `release.yml`:
   - **build:** fails unless the tag equals `COD1RELOADED_VERSION`; builds the
     standard and no-rinput DLLs, proves they differ, runs the portable tests
     (including the installer test), and compiles `CoDUOPlus-Setup.exe`;
   - **linux:** builds `coduoplus_sv.so`;
   - **image** (only if both passed): builds `server/linux/Dockerfile` and pushes
     `ghcr.io/<owner>/coduoplus-server:X.Y.Z` and `:latest` ([DOCKER.md](DOCKER.md));
   - **publish** (only if both passed): `packaging/package.py` builds every download
     ([RELEASES.md](RELEASES.md)), then the release is created (or refreshed on a
     re-run) and marked **latest**. Its notes are a download table, the version's
     whole CHANGELOG section with links pointing into the tag, then every PR merged
     since the previous release and the "Full Changelog" compare link.
4. That's all. Installed copies pick the release up by themselves: the updater's
   built-in feed is `releases/latest/download/manifest.json`
   ([RELEASES.md](RELEASES.md#updates)).

Every push to `main` runs the same workflow as the **`nightly`** pre-release: the
same downloads from that commit, the `nightly` tag moved to it, the image as
`:edge`. A newer push cancels a nightly still building. Pre-releases are never
"latest", so players' updaters never see it.
