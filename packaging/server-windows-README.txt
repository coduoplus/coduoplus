CoDUO+ {VERSION} - Windows dedicated server for Call of Duty: United Offensive 1.51
https://github.com/coduoplus/coduoplus

1. Unzip this into your CoD:UO 1.51 folder (the one with CoDUOMP.exe), so that
   start-coduoplus-server.bat sits next to CoDUOMP.exe.
2. Double-click start-coduoplus-server.bat. The first time it renames the game's
   mss32.dll to mss32_original.dll, installs CoDUO+'s, and creates
   competitive.cfg and uo\server.cfg from the examples here.
3. Edit uo\server.cfg (hostname, rotation, rconpassword ...) and restart.

Port, internet/LAN and the config name are at the top of the .bat.

What the server gets (docs/NETCODE.md, docs/PORTING.md):
  - snaps cap 30 -> 40: real 40-tick with sv_fps 40 (set in the example cfg);
  - competitive.cfg is pushed to CoDUO+ clients (snaps 40, cl_maxpackets 125,
    rate 90000 ...); edits are picked up live, no restart;
  - the swing fix is mirrored, so CoDUO+ players and the server agree on the pose;
  - outdated CoDUO+ clients are asked to update; vanilla clients play as usual.

Update: unzip the newer release over this one and restart the server.
PunkBuster is not supported (sv_punkbuster is forced to 0).
