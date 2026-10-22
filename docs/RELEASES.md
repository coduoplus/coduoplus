# Downloads: what CI publishes, and how players and admins use it

Every release has **one download per kind of user**, and every push to `main`
publishes the same set as the `nightly` pre-release. This page covers what each
download is, how it works, and why it's built that way. How a release is cut is
in [CI.md](CI.md#cutting-a-release).

| You are | Download | What you do |
| --- | --- | --- |
| a **player** | `CoDUOPlus-Setup.exe` | run it, click Next until Finish |
| a player who'd rather copy files | `coduoplus-client.zip` | follow its `INSTALL.txt` (4 steps) |
| running a **Windows server** | `coduoplus-server-windows.zip` | unzip into the game folder, run `start-coduoplus-server.bat` |
| running a **Linux server** | `coduoplus-server-linux.tar.gz` | `docker compose up -d`, or `./coduoplus-server.sh /path/to/coduo` |

The links never change, because asset names carry no version:

- `https://github.com/coduoplus/coduoplus/releases/latest/download/CoDUOPlus-Setup.exe`
- `https://github.com/coduoplus/coduoplus/releases/latest/download/coduoplus-server-windows.zip`
- `https://github.com/coduoplus/coduoplus/releases/latest/download/coduoplus-server-linux.tar.gz`

A forum post or Discord pin with these links stays correct after every release.

Each release also carries the raw files: `mss32.dll`, `mss32-norinput.dll` (a
diagnostics build without the raw-mouse hook), `coduoplus_sv.so`,
`docker-compose.yml`, `manifest.json` (the updater feed) and `SHA256SUMS`.

## Releases and the nightly

| | Made by | Marked | The in-game updater | Server image |
| --- | --- | --- | --- | --- |
| `vX.Y.Z` | pushing the tag | **latest** | offers it | `:X.Y.Z`, `:latest` |
| `nightly` | every push to `main` | pre-release | **never** offers it | `:edge` |

GitHub never marks a pre-release as "latest", so `releases/latest/...` and the
updater only ever reach tagged releases. The nightly is for testing a fix before
it ships. Each push replaces its files, moves its tag to the new commit, and
says so at the top of its notes.

## The player installer (`CoDUOPlus-Setup.exe`)

Source: [`packaging/coduoplus.iss`](../packaging/coduoplus.iss), compiled with
**Inno Setup 6.7.3**. CI installs that exact version and checks its SHA-256
([`.github/actions/setup-innosetup`](../.github/actions/setup-innosetup/action.yml)),
the same way it pins the compiler.

Installing by hand has one step people get wrong: the game's own `mss32.dll`
(the Miles sound library) must be renamed to `mss32_original.dll` first, because
CoDUO+'s `mss32.dll` passes every sound call on to it. Skip it and the game
doesn't start. The installer does it, and also:

1. **Finds the game.** In order: retail UO's registry key
   (`HKLM\SOFTWARE\Activision\Call of Duty United Offensive`, `InstallPath`), the
   CoD key (UO installs into the CoD folder), then Steam (`SteamPath`) and every
   library in `steamapps\libraryfolders.vdf`. If none has `CoDUOMP.exe`, it suggests
   `Program Files (x86)\Call of Duty`. The player can always Browse.
2. **Checks the folder** before changing anything:
   - no `CoDUOMP.exe` → refused (wrong folder);
   - no `mss32.dll` at all → refused (the game itself is broken);
   - a CoDUO+ `mss32.dll` with no `mss32_original.dll` → refused: the Miles DLL
     is gone, so the game couldn't start. It says to repair the game first;
   - `CoDUOMP.exe` isn't 1.51 → warning, default **No**. The check is the PE
     timestamp `0x41B53340`, the same one the DLL uses (`core/game.cpp`), so the
     installer and the mod agree on what 1.51 is.
3. **Renames the Miles DLL**, but only if `mss32_original.dll` doesn't exist yet and
   the current `mss32.dll` isn't CoDUO+. It recognises CoDUO+ by a string only
   CoDUO+ builds contain. So installing over an older CoDUO+ simply replaces it,
   and the real Miles DLL is never overwritten.
4. **Installs `mss32.dll`**, and `coduoplus.ini` only if there's none. The ini
   holds the player's settings, so an update never resets them.
5. Puts its uninstaller in `coduoplus-uninstall\` inside the game folder, and
   registers it under *Settings → Apps* as "CoDUO+ (Call of Duty: United
   Offensive)".

**Uninstall** removes everything CoDUO+ added or wrote while running: the DLL,
the ini, the logs, the generated pk3s, any interrupted update files, and the
autoexec cfg. It then renames `mss32_original.dll` back to `mss32.dll`. The test
checks that the folder ends up byte-for-byte as it was before the install.

**Admin rights:** the installer asks for them, because most installs are under
`Program Files`. For a folder you own, `CoDUOPlus-Setup.exe /CURRENTUSER`
installs without them.

**"Unknown publisher":** the setup isn't code-signed, so Windows SmartScreen may
warn on first run (*More info* → *Run anyway*). The release notes say so. Signing
needs a paid certificate. `SHA256SUMS` lets anyone check the download.

**Silent install** for scripts and LAN parties:
`CoDUOPlus-Setup.exe /VERYSILENT /DIR="D:\Games\Call of Duty"`.

## Updates

**The auto-updater is off by default.** A fresh install checks nothing and
downloads nothing: the game never contacts GitHub unless the player opts in.
To turn it on,
remove the `;` in front of `updater_enable = on` in `coduoplus.ini`. The ini,
the client zip's `INSTALL.txt`, the README and every release's notes all say
so. With it off, players update by running the newer setup (it keeps their
ini) or by replacing `mss32.dll`.

Once turned on, on every start the DLL fetches the latest release's
`manifest.json`:

```
https://github.com/coduoplus/coduoplus/releases/latest/download/manifest.json
{"version": "0.4.0", "download_url": "https://github.com/coduoplus/coduoplus/releases/download/v0.4.0/mss32.dll", ...}
```

If that version is newer, the player sees one dialog. *Yes* downloads that
release's `mss32.dll` and installs it at the next start
(`features/updater.cpp`). The download URL names the tag, not `latest`, so a
release published mid-download can't hand out a mismatched file.

The feed is built into the DLL, so turning the updater on is the only step.
Before, the default URL was empty and a player would also have had to set
`updater_manifest_url`. Now:

- `updater_enable`: **off by default** (code default `false`, the line commented
  out in the shipped ini); `on` opts in;
- an **empty or missing** `updater_manifest_url` uses the built-in feed;
- a **set** URL overrides it (forks, mirrors).

**Upgrading from 0.2.0 or 0.3.0 with your old `coduoplus.ini`:** those releases
shipped `updater_enable = on`, but with no feed, so the updater never ran. With
this release, that old line now switches it on. Comment it out or set it to
`off` if you don't want it.

**On a dedicated server** the dialog is skipped: a server is often started by a
service or a script, and a dialog nobody answers would keep it down. The log
says an update exists and the server starts anyway. Update a server by
unzipping the new server package over the old one.

## The main-menu entry: built by the DLL

The **CoDUO+ SETTINGS** item in the main menu needs `uo\zz_coduoplus_ui.pk3`,
which holds UO's own `ui_mp/main.menu` plus one item. That file is
Activision's, so it can't ship in a release. It used to be made by
`ui_pk3/build_pk3.py` on a machine with the game and uploaded by hand, as an
optional extra file to copy.

Now the DLL makes it on the player's machine (`features/ui_pk3.cpp`), from the
player's own `uo\pakuo03.pk3`, at start-up, before the engine scans its pk3s:

- it reads only the zip directory and the one entry it needs, not the 146 MB
  pak;
- it accepts only the stock 1.51 `main.menu` (size `12560`, CRC-32 `0x08743fce`,
  the file `build_pk3.py` checks by SHA-256), and inserts the item in the same
  place, the empty y=400 slot above AUTO-UPDATE;
- it writes the pk3 only if it's missing or different, via a temp file and
  rename, so the engine never reads a half-written pk3. Normal starts only read;
- it does nothing on a dedicated server (no menus) or on any exe other than
  CoDUOMP 1.51.

The pak's entry is DEFLATE-compressed, so the DLL bundles zlib's `contrib/puff`
inflater (v1.3.1, zlib licence, `third_party/puff/`).

`build_pk3.py` stays as the reference. The `ui_pk3_gen_game` test checks that
the DLL's `main.menu` equals the script's, byte for byte, on the real
`pakuo03.pk3`.

## The Windows server package

`coduoplus-server-windows.zip` is meant to be unzipped into the game folder:

```
start-coduoplus-server.bat
coduoplus-server\mss32.dll
coduoplus-server\competitive.cfg.example
coduoplus-server\server.cfg.example
coduoplus-server\README.txt
```

On every run, [`start-coduoplus-server.bat`](../server/windows/start-coduoplus-server.bat):

1. renames the Miles `mss32.dll` to `mss32_original.dll` (once, with the same
   safety checks as the installer);
2. copies `coduoplus-server\mss32.dll` over `mss32.dll` if they differ. So
   **updating is unzipping the new release over the old one**: the DLL in use is
   never the one inside the zip, so the unzip can't fail on a locked file;
3. creates `competitive.cfg` and `uo\server.cfg` from the examples if they're
   missing (never overwrites yours);
4. starts `CoDUOMP.exe +set dedicated 2 +set net_port 28960 +set sv_punkbuster 0
   +exec server.cfg +map_rotate` in its own console window.

The port, internet/LAN and the config name are variables at the top of the
`.bat`. `server.cfg.example` is the same file the Docker image uses: 40-tick
netcode, logging, and a TDM rotation over the 11 UO maps.

## The Linux server package

`coduoplus-server-linux.tar.gz` unpacks to `coduoplus-server-linux/`:

| File | |
| --- | --- |
| `coduoplus_sv.so` | the server library ([LINUX.md](LINUX.md)) |
| `coduoplus-server.sh` | runs `coduo_lnxded` with the library preloaded, no Docker |
| `docker-compose.yml` | the [Docker](DOCKER.md) setup, **pinned to this release's image** (`:X.Y.Z`) |
| `.env.example` | where the game files are, port, LAN/internet |
| `competitive.cfg.example`, `server.cfg.example` | the configs |
| `README.txt` | both ways, short |

In the release, `docker-compose.yml` has the repo's `build:` line removed (a
download has no source tree to build from) and its image pinned to the
release's version. Upgrading is then a deliberate edit, not whatever `:latest`
is when the host restarts.

[`coduoplus-server.sh`](../server/linux/coduoplus-server.sh)` /path/to/coduo
[engine args]` checks for `coduo_lnxded` and `uo/game.mp.uo.i386.so`, creates
the two configs from the examples on first run, and execs the server with
`LD_PRELOAD`, `dedicated`, `net_port` and `sv_punkbuster 0`. `PORT` and
`DEDICATED` come from the environment, as in the image.

## How it's built and tested

[`packaging/package.py`](../packaging/package.py) assembles the release from the
CI build outputs (Python stdlib only). It refuses a `mss32.dll` that isn't a
CoDUO+ build, a `coduoplus_sv.so` that isn't 32-bit ELF, and a tagged release
with no CHANGELOG section. It writes the notes: a download table at the top,
then the version's CHANGELOG section with links pointing into the tag. The
workflow then appends GitHub's list of merged PRs.

| Test | Label | What it proves |
| --- | --- | --- |
| `installer` | portable (CI) | builds the setup from this build's DLL and runs it silently against stand-in game folders: fresh install, install over it, uninstall back to the original bytes, both refusals |
| `ui_pk3_gen` | portable (CI) | the zip writer/reader round trip, CRC and garbage rejection, non-stock menu refused, the dedicated-server command-line parser |
| `ui_pk3_gen_game` | game (local) | on the real `pakuo03.pk3`: DLL output == `build_pk3.py` output byte for byte; the on-disk path writes once, then reports up to date |

The stand-in game folder in `installer` is a `CoDUOMP.exe` with just a PE header
carrying the 1.51 timestamp, plus a fake Miles DLL. That's all the installer
reads, so the test needs no Activision files and runs on a hosted runner.

The Windows server package was also run by hand on a test install: it renamed
nothing (the original was already in place), installed its DLL, created both
configs, and the server answered `getstatus` within 5 s as `CoDUO+ 0.3.0` on
mp_arnhem, with the hostname from `server.cfg.example`. The log showed the menu
pk3 skipped and the updater, against the live feed, `up to date (local=0.3.0
remote=0.3.0)`.
