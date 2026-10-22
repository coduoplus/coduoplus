@echo off
rem CoDUO+ dedicated server for Windows (CoDUOMP.exe 1.51 + CoDUO+'s mss32.dll).
rem
rem Unzip coduoplus-server-windows.zip into your CoD:UO 1.51 folder (the one with
rem CoDUOMP.exe) and run this file. Every start it:
rem   1. renames the game's own mss32.dll (Miles sound) to mss32_original.dll, once;
rem   2. puts CoDUO+'s mss32.dll from coduoplus-server\ in place (so unzipping a newer
rem      release over the old one is the whole update);
rem   3. creates competitive.cfg and uo\server.cfg from the examples if you have none;
rem   4. starts the server in its own console window.
rem Edit the settings below; server settings go in uo\server.cfg, the netcode spec that
rem CoDUO+ clients receive in competitive.cfg. See docs/NETCODE.md.

setlocal
rem UDP port
set PORT=28960
rem 2 = internet (listed on the master server), 1 = LAN only
set DEDICATED=2
rem the config in uo\ to exec, and what to do after it
set CONFIG=server.cfg
set AFTER=+map_rotate

cd /d "%~dp0"
if not exist CoDUOMP.exe (
  echo CoDUOMP.exe is not in %CD%
  echo Unzip coduoplus-server-windows.zip into your CoD:UO 1.51 folder and run this from there.
  pause & exit /b 1
)
if not exist coduoplus-server\mss32.dll (
  echo coduoplus-server\mss32.dll is missing - unzip the whole package again.
  pause & exit /b 1
)

if not exist mss32_original.dll (
  if not exist mss32.dll (
    echo mss32.dll is missing, so the game cannot start: repair or reinstall the game first.
    pause & exit /b 1
  )
  findstr /m /c:"coduoplus DLL_PROCESS_ATTACH" mss32.dll >nul && (
    echo mss32.dll is CoDUO+ but the game's own mss32_original.dll is gone:
    echo repair or reinstall the game first.
    pause & exit /b 1
  )
  ren mss32.dll mss32_original.dll || ( pause & exit /b 1 )
  echo renamed the game's mss32.dll to mss32_original.dll
)

fc /b coduoplus-server\mss32.dll mss32.dll >nul 2>&1 || (
  copy /y coduoplus-server\mss32.dll mss32.dll >nul || (
    echo Cannot replace mss32.dll - is a server or the game already running from this folder?
    pause & exit /b 1
  )
  echo installed CoDUO+ mss32.dll
)

if not exist competitive.cfg copy coduoplus-server\competitive.cfg.example competitive.cfg >nul
if not exist uo\%CONFIG% copy coduoplus-server\server.cfg.example uo\%CONFIG% >nul

echo Starting the CoDUO+ server on UDP port %PORT% (dedicated %DEDICATED%, exec %CONFIG%)
start "CoDUO+ server :%PORT%" /d "%CD%" CoDUOMP.exe +set dedicated %DEDICATED% +set net_port %PORT% +set sv_punkbuster 0 +exec %CONFIG% %AFTER%
