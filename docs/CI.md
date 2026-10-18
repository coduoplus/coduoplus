# CI, branch protection and releases

How CoDUO+ is built and tested on GitHub, why part of it runs on a self-hosted
machine, how `main` is protected, and how a release is cut.

## Workflows

| Workflow | Trigger | Jobs |
| --- | --- | --- |
| `.github/workflows/ci.yml` | push to `main`, pull request into `main`, manual | **`portable`** (hosted Windows) and **`linux`** (hosted Ubuntu: the Linux library), the required checks |
| `.github/workflows/game.yml` | manual only (any branch) | `game` (self-hosted): the full suite against the real game, **not** required |
| `.github/workflows/release.yml` | tag `v*` pushed, manual on a tag | `build` and `linux` (hosted), `publish` |

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

### Why `game` is not a required check

The self-hosted machine is someone's own PC, and it is not always on. When `game`
was required, every pull request sat at "Waiting for a runner to pick up this job"
until that PC came online, and nothing could merge. So:

- **Pull requests are gated by the hosted jobs only** (`portable`, `linux`). They
  never wait for a particular machine.
- **`game.yml` only runs when started by hand:** `gh workflow run game.yml` (add
  `--ref <branch>` to test a branch before merging it). Nothing queues up waiting
  for the machine.
- **Releases don't need it either.** `release.yml` runs on hosted runners only.
  Run `game.yml` (or the suite locally) on the commit you are about to tag.
- The game tests are also run locally, against the test install, before a branch
  is merged (see [TESTING.md](TESTING.md)).

What's given up: a change that compiles and passes the portable tests, but breaks
against the real binaries, can reach `main` and a release unless someone runs the
game suite first. The UI pk3 is also no longer built by the release workflow,
because it is generated from the install's `pakuo03.pk3`; see
[Cutting a release](#cutting-a-release).

### Toolchain

Both runners use the same compiler, WinLibs GCC 16.2.0 i686 (posix, dwarf,
MSVCRT), from
`winlibs-i686-posix-dwarf-gcc-16.2.0-mingw-w64msvcrt-14.0.0-r2.zip`. On the hosted
runner it is downloaded once and cached; on the self-hosted machine it is
expected in `C:\tools\mingw32`.

## Branch protection on `main`

- **Required status checks:** `portable` and `linux` (hosted), with "require branches to
  be up to date" on, so a change is tested against the current `main`. `game` is not
  required, [see above](#why-game-is-not-a-required-check).
- **Pull requests required:** no direct pushes; admins included.
- No force-pushes, no deletion.

So nothing reaches `main` unless the hosted jobs passed on that exact merge
result, and nothing is released unless the hosted build and portable tests
passed on the tagged commit.

## Security of the self-hosted runner

A self-hosted runner executes whatever the workflow tells it to, on that
machine, with that user's rights. A pull-request workflow runs the workflow
file *from the pull request*, so anyone who can open a PR can change what runs.

- `game.yml` has **no `pull_request` trigger**: it runs only commits already on
  `main`, or a branch a maintainer started by hand. A pull request's own code never
  reaches the runner through it.
- That alone isn't enough, because a pull request can *add* a workflow that asks for
  the self-hosted labels. The real control is *Settings → Actions → General → Fork
  pull request workflows → **Require approval for all outside collaborators***, set
  on this repository. Never approve a fork run without reading its workflow changes.
- To test an outside contribution against the game, a maintainer reads it, pushes
  it to a branch of this repo and runs `gh workflow run game.yml --ref <branch>`.
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
   `## vX.Y.Z` section to `CHANGELOG.md`, and merge through a PR. If the game
   machine is available, run `gh workflow run game.yml` on that commit first.
2. Tag and push: `git tag v0.2.0 && git push origin v0.2.0`.
3. `release.yml`:
   - **build:** fails unless the tag equals `COD1RELOADED_VERSION`; builds the
     standard and no-rinput DLLs, proves they differ, and runs the portable tests;
   - **linux:** builds `coduoplus_sv.so`;
   - **publish** (only if both passed): creates the release with the CHANGELOG
     section as notes, and attaches `mss32.dll`, `mss32-norinput.dll`,
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
