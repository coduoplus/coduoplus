# CoDUO+ — Changelog

## v0.3.0 (unreleased)

### Netcode
- **Lag compensation rewinds poses.** A target that crouched, went prone, leaned or
  aimed down since the shooter saw it is now tested in that earlier pose, not just
  at the earlier position: the server records each player's computed skeleton every
  frame and swaps the old one in for the length of the shot. Bounding boxes are
  rewound too. New server cvar `g_antilagPose` (on; `0` = position only).
  [docs/NETCODE.md](docs/NETCODE.md#lag-compensation)

## v0.2.0 (2026-10-18) — upstream 1.6.9, lag compensation, Linux servers

Everything new in COD1.6X `release/1.6.8`..`release/1.6.9` except the
PunkBuster ruleset, ported onto 0.1.0. Upstream's 1.6.8/1.6.9 line starts from
1.6.5 (1.6.6 was withdrawn), so the 1.6.6 features CoDUO+ is built on stay. See
[docs/PORTING.md](docs/PORTING.md#upstream-168-and-169).

### Frame pacing and input
- **Continuous clock**: the limiter reports a QPC clock calibrated onto the
  engine's `Sys_Milliseconds` epoch; at 250 fps every frame reads exactly 4 ms
  (61 % with the stepped engine clock, measured).
- **Late mouse**: the mouse is read again once the frame wait is over, so
  movement made during the wait lands in this frame's command
  (`input_late_sampling`).
- **gpu_sync**: `glFinish` after every `SwapBuffers`, one frame in flight;
  switches itself off for the session when the GPU sets the fps (`gpu_sync`).
- The 1 ms timer is kept while the window is occluded (Windows 11).
- Working-set sizes computed in 64 bits (5000 MB no longer wraps to 904 MB).

### Netcode (40-tick)
- **CoDUO+ servers push their netcode spec.** `competitive.cfg` next to the
  server's `mss32.dll` (example shipped) is published to CoDUO+ clients, which lock
  or clamp the named cvars while connected. Until now nothing on a UO server
  published it, so clients never actually ran `snaps 40` / `cl_maxpackets 125`.
- An over-cap `cl_maxpackets` resets to 125 instead of vanilla's 100.
- **Lag compensation** on CoDUO+ servers (Windows and Linux): a shot is traced
  against where the other players were on the shooter's screen, up to
  `g_antilagMaxMs` (300) back. Server cvar `g_antilag` (on by default, in
  serverinfo; `2` logs every compensated shot). Nothing changes on clients, so
  vanilla players benefit too. Pose (stance/lean) is not rewound yet.
- [docs/NETCODE.md](docs/NETCODE.md): how a shot is registered, server/client
  setup, why `sv_fps 40`, and how lag compensation works.

### Linux servers
- **`coduoplus_sv.so`** (LD_PRELOAD) for the CoD:UO 1.51b Linux dedicated server:
  snaps cap 40, `CoDUO+` shortversion, swing-fix mirror + `sv_coduoplus_swing`,
  version gate, `competitive.cfg` publisher, re-applied on every map change.
  Shares its values and spec code with the Windows mod. [docs/LINUX.md](docs/LINUX.md)
- **Docker image** `coduoplus-server` (no game files inside: mounted read-only) with
  an example `docker-compose.yml`, health check, tini, unprivileged user; published
  to `ghcr.io/coduoplus/coduoplus-server` by the release workflow. [docs/DOCKER.md](docs/DOCKER.md)
- The Linux engine's 32-bit `stat`/`readdir` are wrapped: on a Windows drive under
  WSL/Docker Desktop or a big XFS/btrfs disk the stock server finds no pk3s at all.
- `competitive.cfg.example` trimmed to the four 40-tick lines: the systeminfo
  string is capped at 1024 chars and stock UO servers already overflow it.

### Display
- A `r_fullscreen 1` in `uoconfig_mp.cfg` is the player's choice and is never
  rewritten to 0; a minimized window is never restyled.

### Settings menu (Ctrl+M)
- Branded **CoDUO+** (it still said "1.6X / COD1 RELOADED"); the version shown
  comes from the build.
- The cursor moves with raw mouse input off (it was frozen: the engine re-centres
  the cursor before Windows reports the move).
- Files tab lists UO's own demos (`*.dm_22`, not CoD1's `Main` demos) and the
  configs in `uo/` as well as `uo/configs/`.
- `zz_coduoplus_ui.pk3` (the main-menu button) is optional and attached to the
  release separately; Ctrl+M opens the settings without it.

### Imposed settings (ini lock)
- `fullscreen`, `refresh_rate`, `frame_limiter_enable`, `force_1ms_timer`,
  `gpu_sync`, `input_late_sampling`, `smoothness_cpu_cores` are rewritten to
  fixed values at every launch; `r_fullscreen` is read-only at 1 and
  `r_displayRefresh` held at the display's maximum. The borderless window is
  therefore not available in release builds (as upstream 1.6.10).
  `-DCOD1RELOADED_DEV_UNLOCK=ON` builds a test DLL without the lock.

### Log
- One `session` line a minute and at exit: fps, late wakes, long frames, GPU
  wait, display mode, com_maxfps, raw input.

### CI and releases
- GitHub-hosted runners only: no self-hosted runner is needed for pull requests,
  `main` or releases. The tests against the real game run locally
  ([docs/CI.md](docs/CI.md)).

### Tests
- `antilag`: the lag-compensation core on a fake entity array (portable).
- `frame_limiter`: the real wait in a copy of the patched `Com_Frame` loop.
- `ini_lock`: the rewrite over the shipped `coduoplus.ini`.
- `site_check`: 264 checks (11 new for the limiter's clock, mouse and
  dedicated-server path), plus 12 for lag compensation per `uo_game` build;
  `site_check_linux` anchors every Linux address and struct field.

## v0.1.0 (2026-10-17) — first CoD:UO 1.51 build

Port of COD1.6X 1.6.6 (`cod1plus/client` `6f8fec0`) to Call of Duty: United
Offensive 1.51. How and why: [docs/PORTING.md](docs/PORTING.md).

### Ported
- Every `CoDUOMP.exe` patch, re-found and checked against the 1.51 binaries:
  version string, protocol/master/browser sites (left at UO's own values),
  snaps 40 / cl_maxpackets 125 caps, server-pushed competitive spec,
  r_displayRefresh 1000 Hz cap, r_fullscreen default,
  raw mouse input, live systeminfo lookup.
- cgame: cg_fov unlock (80–95), Hor+ / stretched FOV, swing fix, stance blend
  hold (off, as upstream).
- Everything Win32-only: 1 ms timer, CPU affinity, priorities, working set,
  FSO opt-out, per-monitor gamma, display probe, mode guard, borderless window,
  single-instance guard, Ctrl+M overlay, Discord, updater, PAM installer.

### Different from COD1.6X, on purpose
- **Swing fix** follows the server: a CoDUO+ server mirrors it into its own
  `uo_game_mp_x86.dll` and publishes `sv_coduoplus_swing 1`; the client only
  applies it there. On vanilla servers the drawn pose stays the tested one.
- **Frame limiter** still runs UO's `Com_EventLoop` every frame (in UO the
  patched call *is* the event loop) and removes the engine's integer-ms re-wait.
- **Protocol 22 and the stock master**: no separate ecosystem, so CoDUO+
  players use the normal browser and servers.
- **Version gate** works on both known `uo_game` builds and survives map
  changes (upstream's was lost at the first rotation). Vanilla players are
  always let in.
- **Window patch** only restyles the game window (upstream's turned a
  dedicated server's console into a borderless full-screen window).
- **Main-menu entry** is generated from the player's own `pakuo03.pk3`; no
  CoD1 menus are shipped. Upstream's 1.6/1.5 server-list buttons are dropped.
- **Updater / PAM** have no default feed (upstream's ship CoD1 files).
- `fullscreen = on` by default (as upstream's own 1.6.10 ini).

### Removed
- All PunkBuster code: the PB-format cvar ruleset (parser, fetcher, embedded
  CoDBase PunkBuster list, `ruleset_*` ini keys, userinfo `cod1x_rs`) and the
  cheat-cvar scan (`cod1x_ac`).

### Not ported
- Lean fix and controller smoothing, antilag, viewheight lerp speed, the
  CG_Draw2D hook (avatar overlay, Discord in-match heartbeat). Reasons in
  [docs/PORTING.md](docs/PORTING.md#feature-status).

### Tests
- `site_check`: 253 offline checks of every patch site against the stock
  binaries (and the 2005 `uo_game` build).
- `smoke_dedicated`: the real game with the mod as a dedicated server: UDP
  queries, real (Huffman-compressed) connect handshakes against the version
  gate, and the mod's log.

---

# COD1.6X (cod1reloaded) — upstream changelog

The history below is upstream's, for the CoD1 mod this port is based on.

## v1.6.6 (2026-09-18)

### 📦 Bouton « INSTALL / UPDATE PAM » (menu 1.6X, onglet Files)
- Télécharge les pk3 du mod compétitif (maps comprises) sans rejoindre de serveur, depuis le manifeste
  publié par https://github.com/cod1plus/cod1pluspam : vérification SHA-256, fichiers déjà bons sautés
  (le même bouton met à jour), pk3 en cours d'utilisation posé en `.new` et échangé au lancement suivant.
  Clés ini `pam_download_enable`, `pam_manifest_url`.

### 🖥️ Mode Stretched 4:3 (la façon cod2x)
- Display → View mode → **Stretched 4:3** : le look 1440×1080 étiré, rendu par le mod en résolution native
  (FOV vertical 4:3 sur le buffer 16:9, HUD étiré comme avant). Le menu 1.6X écrivait une cvar morte
  (`cod1x_widescreen`) et n'offrait pas ce mode ; un changement de mode s'applique désormais en direct.
- Le plafond moteur de `r_displayRefresh` (200 Hz, `Cvar_CheckRange` dans R_Register) est levé : 320 Hz
  et plus passent, et un mode refusé retombe sur le plus haut listé, plus sur « le plus haut en dessous ».

### 🖥️ Hz maximum quelle que soit la résolution
- Une résolution custom n'existe qu'avec les fréquences que le pilote lui a données (1440×1080 listée
  à 180 Hz max sur un écran 320 Hz) : avec `refresh_rate = max`, le mod passe le jeu à la résolution
  qui a le maximum et fait lui-même l'étirement 4:3 (`view_mode = stretched`). `max_hz_native_res = off`
  pour garder sa résolution custom.

### ⌨️ Menu 1.6X : Ctrl+M uniquement
- INSERT n'ouvre plus le menu (touche que les joueurs bindent, et qui partait en pleine visée) ;
  Ctrl+M ou le bouton « 1.6X SETTINGS » du menu principal. INSERT redevient bindable.

### 👁️ FOV plafonné à 95
- Le slider du menu 1.6X va de 80 à 95, et `cg_fov` est ramené dans cette plage en direct quelle que
  soit la source (console, config_mp.cfg). Les serveurs (`competitive.cfg`) appliquent la même plage.

### 🛡️ Ruleset PunkBuster (remplaçant de PB)
- Le client applique lui-même la liste CoDBase complète (~430 cvars) demandée par le serveur
  (`sv_competitive_ruleset`) : exact → forcé + verrouillé, plage → ramené dans la plage, `wait` neutralisé.
- La liste est téléchargée depuis https://github.com/cod1plus/rulesets au lancement puis toutes les
  10 min, et immédiatement quand un serveur demande une version plus récente (`<id>@<version>`) ;
  cache dans `rulesets\`, copie compilée en secours hors-ligne (`ruleset_fetch_enable`, `ruleset_url`).
- Aucune mise à jour serveur requise : un serveur qui pousse un `competitive.cfg` (tous les `.so`
  1.6.x) déclenche la liste par défaut `codbase-2023-05`. Le `.so` 1.6.6 ajoute le choix de l'id par
  serveur, la poussée immédiate d'une version et le kick des clients qui combattent l'enforcement.
- Le `competitive.cfg` du serveur garde la priorité pour les cvars qu'il nomme, y compris à chaud.
- La commande `wait` reste **autorisée** : les nade binds de CoD1 sont construits dessus (switch grenade,
  cook, throw, switch back). Le ruleset encadre les valeurs de cvars, pas la console.
- Corrigé : en 1.6.5 aucune règle à 0, négative ou flottante du `competitive.cfg` n'était appliquée
  (`r_fullbright 0`, `m_yaw 0.022`, `cl_timenudge -20 0`…).
- Corrigé : les cvars systeminfo gardaient la valeur du serveur précédent (spec et ruleset lus dans
  la configstring courante, plus dans la cvar).

## v1.6.4 (2026-08-02)

### 🧍 Up / down
- **Retour au comportement de la 1.6.0**, celui qui avait été validé au lancement.
  En 1.6.3 le modèle suivait de trop près un joueur qui spamme accroupi, et ça se
  lisait comme du clignotement (« on voit le joueur briller »). Un modèle lent et
  lisible est justement ce qui rend l'abus d'up-down punissable.
- **Correction du limiteur de FPS** : il renvoyait au moteur une horloge théorique
  au lieu du temps réel. C'est l'horloge qui sert à l'interpolation des snapshots,
  au blending d'animation et aux transitions de posture — elle dérivait de la
  timeline réseau et faisait scintiller les modèles adverses, y compris ceux qui
  ne spammaient rien.

### ⌨️ Menu 1.6X
- **La touche INSERT est totalement supprimée.** Le sondage clavier global se
  déclenchait pendant que tu vises, tires ou écris. Le menu s'ouvre désormais
  uniquement depuis le MENU PRINCIPAL.

### 🖱️ Raw input souris (`m_rinput`)
- **Portage du `m_rinput` de cod2x.** La souris est lue directement sur le
  périphérique au lieu de passer par le curseur Windows : plus d'accélération
  pointeur, plus de dépendance au curseur du bureau, plus d'arrondi au pixel. Ta
  sensibilité en jeu ne change pas, c'est la couche Windows qui disparaît.
- `m_rinput 0/1` (sauvegardé dans ta config) ou `raw_mouse_input` dans
  `cod1reloaded.ini`. **Désactivé par défaut** : ça modifie la visée, personne ne
  doit se le prendre sans l'avoir choisi.
- `m_rinput_hz` affiche le **taux de sondage réel** de ta souris (500, 1000…), et
  `m_rinput_hz_max` le maximum vu — de quoi vérifier qu'une souris annoncée à
  1000 Hz tient vraiment 1000 Hz.

### 🖥️ Résolutions personnalisées / stretch
- **Correction** : une résolution personnalisée plus grande que ton bureau
  (2128x1330, 1776x1332… sur un écran 1080p) était tout simplement ignorée. Seul
  le plein écran exclusif peut changer le mode d'affichage ; le mod démarrait en
  fenêtre sans bordure, où une telle résolution ne peut pas tenir. Le mod détecte
  maintenant le cas au lancement et **repasse en plein écran tout seul**.
- Une résolution plus **petite** que le bureau (1440x1080 par exemple) fonctionne
  en fenêtre mais n'est **pas étirée par le GPU** : le mod l'écrit désormais dans
  `cod1reloaded.log` au lieu de laisser chercher.
- `cod1reloaded.ini` explique la règle en clair dans la section DISPLAY.

### 🔒 Règles de jeu équitable
- Le serveur peut désormais pousser une **liste de réglages beaucoup plus longue**
  (découpée en plusieurs cvars). Au-delà de ~250 caractères l'ancienne version
  arrêtait le serveur au changement de map.

## v1.6.3 (2026-07-31)

### 🎯 Hitbox des joueurs penchés
- **Refonte complète du calcul des impacts sur un joueur qui lean.** Le squelette
  testé par le serveur est désormais posé à chaque tir et aligné sur le modèle que
  tu vois à l'écran : tirer sur la tête visible d'un joueur penché compte enfin
  comme un headshot, debout comme accroupi.
- **Fini les impacts fantômes** : plus de dégâts à côté du corps, plus de tir qui
  compte sur la silhouette « debout » d'un joueur penché, plus de balle qui touche
  à côté des jambes.
- **Plus de balles avalées** : un tir qui touche vraiment le corps ne peut plus
  être annulé en silence.
- Le tireur ne peut plus se toucher lui-même en leanant ; les joueurs à plat ventre
  et les morts ne bloquent plus les balles.

### ⏱️ Limiteur de FPS
- **Cadence réellement stable** à la valeur choisie (250 ou 125) : fini le
  248-250 qui oscillait, et le scénario de jeu ne « scintille » plus.

### 🔒 Client ↔ serveur
- **Correction majeure** : les informations envoyées par le client au serveur
  (version du mod, rapport anti-triche) n'arrivaient jamais à destination — un
  drapeau interne erroné les publiait dans le mauvais canal. La vérification de
  version et le contrôle anti-triche côté serveur sont donc **opérationnels pour
  la première fois**.
- Le serveur peut suivre automatiquement la dernière version publiée sur GitHub et
  refuser les clients périmés, sans intervention manuelle.
- **Règles de jeu équitable** poussées par le serveur à tous les clients du mod
  (fps, snaps, cl_maxpackets, rate, réglages graphiques avantageux…).

## v1.6 — BÊTA DE TEST (2026-06-30)

> ⚠️ **Build BÊTA destinée aux tests communautaires.** Merci de remonter tout bug
> (glitch de modèle, souci de connexion, crash) sur le Discord. Certaines fonctions
> sont expérimentales — voir « Limitations connues » plus bas.

### 🌐 Nouvel écosystème réseau
- **Protocole réseau 6 → 10** : cod1reloaded devient un écosystème **séparé** de CoD1
  vanilla. Les clients et serveurs proto-10 ne communiquent qu'entre eux.
- **Nouveau master server** (le master Activision officiel est mort) → la **liste de
  serveurs Internet refonctionne**.
- **Vérification de version client** : un serveur cod1reloaded rejette les clients
  trop anciens (cvar userinfo `cod1reloaded`).
- Version affichée dans le menu → **1.6**.

### 🎮 Modèle joueur (portage CoD2x)
- **Buste droit** en marche/visée + lean latéral propre — fini le « piqué » avant
  quand on avance en leanant.
- **Arme et torse verrouillés sur la vue** — plus de lag jambes/torse (swing fix).
- **Lissage synchronisé du contrôleur** (`ctrl_smooth`) — plus de saccade du modèle
  quand la direction de mouvement change.
- **Fix anti lean-spam / « clip »** : le lean du modèle est limité en vitesse
  (~1,4°/frame, velocity-clamp porté de CoD2x) → spammer la touche lean ne fait
  **plus clignoter ton modèle** pour les autres joueurs.
- **Persistance au changement de map** : les fix de modèle ne disparaissent plus
  après une rotation de map ou un `/devmap`.

### 🖥️ Affichage
- **FOV Hor+ widescreen** : vrai FOV sur écran large (fini l'étirement vertical).
- **Fenêtre borderless** : alt-tab instantané, sans freeze d'écran noir.

### ⚡ Fluidité / performance (anti-microstutter)
- Timer **1 ms** (com_maxfps précis) + **frame limiter** à la microseconde (FPS cap exact).
- Affinité CPU, priorité process, lock du working set, désactivation du
  Fullscreen Optimization Windows.

### 🔌 Intégrations
- **Auto-updater** (récupère les nouvelles versions automatiquement).
- **Discord Rich Presence** (optionnel).
- Upload auto de démos + overlay avatar (POC, désactivés par défaut).

### ⚠️ Limitations connues (bêta)
- **Antilag** (lag compensation) : expérimental, côté serveur, **désactivé** dans
  cette build.
- **Crouch-lean corner peek (headclip)** : pas encore empêché — prévu pour une
  prochaine version. Le fix de cette build couvre le *lean-spam*, **pas** le peek
  de coin accroupi.
- C'est une **bêta** : merci de tester et de remonter tout comportement anormal.

### 🎯 À tester en priorité
- Le **modèle des autres joueurs** : lean gauche/droite, strafe + lean, et surtout
  le **spam de lean** → tout doit rester lisse et naturel (pas de clignotement).
- La **liste Internet** : ton serveur apparaît bien, tu peux t'y connecter.
- **Stabilité** : changements de map, longues sessions, alt-tab.
