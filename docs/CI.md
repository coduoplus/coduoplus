# CI, branch protection and releases

How CoDUO+ is built and tested on GitHub, how `main` is protected, and how a
release is cut. Everything runs on GitHub-hosted runners: there is no
self-hosted runner to keep online.

## Workflows

| Workflow | Trigger | Jobs |
| --- | --- | --- |
| `.github/workflows/ci.yml` | push to `main`, pull request into `main`, manual | **`portable`** (hosted Windows) and **`linux`** (hosted Ubuntu: the Linux library), the required checks |
| `.github/workflows/release.yml` | tag `v*` pushed, manual on a tag | `build` and `linux` (hosted), then `image` (server image to GHCR) and `publish` |

### What CI does and doesn't test

The test suite ([TESTING.md](TESTING.md)) has three kinds of test, by CTest label:

| Label | Tests | Needs | Runs |
| --- | --- | --- | --- |
| `portable` | `doc_links`, `ini_lock`, `competitive_sv` | only the build | in CI |
| `game` | `fetch_binaries`, `site_check`, `site_check_2005`, `site_check_linux`, `ui_pk3`, `smoke_dedicated`, `smoke_linux` | a CoD:UO 1.51 install; the smoke test **launches the game** | locally |
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
was run locally first. The UI pk3 is also not built by the release workflow,
because it is generated from the install's `pakuo03.pk3`; see
[Cutting a release](#cutting-a-release).

### Toolchain

CI and local builds use the same compiler, WinLibs GCC 16.2.0 i686 (posix,
dwarf, MSVCRT), from
`winlibs-i686-posix-dwarf-gcc-16.2.0-mingw-w64msvcrt-14.0.0-r2.zip`. In CI it is
downloaded once and cached; locally it is expected in `C:\tools\mingw32`.

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
     standard and no-rinput DLLs, proves they differ, and runs the portable tests;
   - **linux:** builds `coduoplus_sv.so`;
   - **image** (only if both passed): builds `server/linux/Dockerfile` and pushes
     `ghcr.io/<owner>/coduoplus-server:X.Y.Z` and `:latest` ([DOCKER.md](DOCKER.md));
   - **publish** (only if both passed): creates the release (or refreshes it on a
     re-run) with the notes: the version's whole CHANGELOG section, links pointing
     into the tag, then every PR merged since the previous tag and the
     "Full Changelog" compare link, and attaches `mss32.dll`, `mss32-norinput.dll`,
     `coduoplus.ini`, `competitive.cfg.example`, `coduoplus_sv.so`,
     `manifest.json`, and `coduoplus-X.Y.Z.zip` / `-norinput.zip` (laid out like the
     game folder).
   - The UI pk3 can't be generated on a hosted runner. On a machine with the game:
     `python -I ui_pk3/build_pk3.py --game "<install>"`, then
     `gh release upload vX.Y.Z dist/zz_coduoplus_ui.pk3`. It's optional for players:
     Ctrl+M opens the settings without it.
4. To point installed copies at the release feed, set `updater_manifest_url` to
   `https://github.com/<org>/coduoplus/releases/latest/download/manifest.json`.
   CoDUO+ ships with an empty URL, see [PORTING.md](PORTING.md#updater-and-pam-installer-point-nowhere-by-default).
