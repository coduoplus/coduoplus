; CoDUO+ player installer - CoDUOPlus-Setup.exe. See docs/INSTALLER.md.
;
;   ISCC /DVersion=0.2.0 /DDllDir=..\build packaging\coduoplus.iss   -> dist\CoDUOPlus-Setup.exe
;
; What a player otherwise does by hand (README "Manual install"), in one wizard:
;   1. finds the CoD:UO folder (registry, Steam libraries) and checks it really is
;      CoD:UO 1.51 (CoDUOMP.exe's PE timestamp, the same test the DLL makes);
;   2. renames the game's Miles mss32.dll to mss32_original.dll, the step people miss,
;      and only when that file is really Miles (not an older CoDUO+ already in place);
;   3. installs mss32.dll, and coduoplus.ini only if the player has none (it holds
;      their settings). The DLL generates uo\zz_coduoplus_ui.pk3 itself on first launch.
; Uninstall (Settings -> Apps) removes all of it and puts the Miles DLL back.
; Updates after this one come through the mod's own updater; running a newer setup over
; an install also works.

#ifndef Version
  #define Version "0.0.0"
#endif
#ifndef DllDir
  #define DllDir "..\build"
#endif

[Setup]
AppId={{6C0D7F3A-2B1E-4C59-9E44-C0D0E0A0B151}
AppName=CoDUO+
AppVersion={#Version}
AppVerName=CoDUO+ {#Version}
AppPublisher=CoDUO+
AppPublisherURL=https://github.com/coduoplus/coduoplus
AppSupportURL=https://github.com/coduoplus/coduoplus/issues
AppUpdatesURL=https://github.com/coduoplus/coduoplus/releases
VersionInfoVersion={#Version}
; the game folder, not a folder of our own
DefaultDirName={code:DefaultGameDir}
AppendDefaultDirName=no
DirExistsWarning=no
UsePreviousAppDir=yes
DisableProgramGroupPage=yes
; most installs live under Program Files; /CURRENTUSER for a folder you own
PrivilegesRequired=admin
PrivilegesRequiredOverridesAllowed=commandline
; keep the game folder tidy: the uninstaller in a subfolder
UninstallFilesDir={app}\coduoplus-uninstall
UninstallDisplayName=CoDUO+ {#Version} (Call of Duty: United Offensive)
UninstallDisplayIcon={app}\CoDUOMP.exe
OutputDir=..\dist
OutputBaseFilename=CoDUOPlus-Setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x86compatible
SetupLogging=yes

[Messages]
SelectDirDesc=Where is Call of Duty: United Offensive installed?
SelectDirLabel3=CoDUO+ goes into your Call of Duty: United Offensive folder - the one with CoDUOMP.exe in it. It must be patched to 1.51.
SelectDirBrowseLabel=Setup found the folder below. If it is wrong, click Browse.

[Files]
Source: "{#DllDir}\mss32.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\coduoplus.ini"; DestDir: "{app}"; Flags: onlyifdoesntexist

[UninstallDelete]
; what the mod writes next to itself and into uo\ while it runs
Type: files; Name: "{app}\coduoplus.log"
Type: files; Name: "{app}\coduoplus.prev.log"
Type: files; Name: "{app}\mss32.dll.new"
Type: files; Name: "{app}\mss32.dll.part"
Type: files; Name: "{app}\mss32.dll.old"
Type: files; Name: "{app}\uo\zz_coduoplus_ui.pk3"
Type: files; Name: "{app}\uo\zz_coduoplus_ui.pk3.tmp"
Type: files; Name: "{app}\uo\z_coduoplus.pk3"
Type: files; Name: "{app}\uo\z_coduoplus.pk3.hash"
Type: files; Name: "{app}\uo\coduoplus_autoexec.cfg"
Type: files; Name: "{app}\main\coduoplus_autoexec.cfg"
Type: dirifempty; Name: "{app}\coduoplus-uninstall"

[Run]
Filename: "{app}\CoDUOMP.exe"; WorkingDir: "{app}"; Description: "Play Call of Duty: United Offensive multiplayer now"; Flags: postinstall nowait skipifsilent runasoriginaluser

[Code]
const
  GAME_EXE = 'CoDUOMP.exe';
  UO_151_PE_TIMESTAMP = $41B53340;   { patches::GAME_PE_TIMESTAMP, 2004-12-07 }
  OUR_MARKER = 'coduoplus DLL_PROCESS_ATTACH';   { a string only a CoDUO+ build carries }

function HasGame(const Dir: String): Boolean;
begin
  Result := (Dir <> '') and FileExists(AddBackslash(Dir) + GAME_EXE);
end;

{ Steam keeps its extra library folders in steamapps\libraryfolders.vdf as "path" lines. }
function SteamCandidate(const Steam: String): String;
var
  Lines: TArrayOfString;
  I: Integer;
  Lib, Line: String;
begin
  Result := '';
  if Steam = '' then exit;
  StringChangeEx(Steam, '/', '\', True);
  if HasGame(Steam + '\steamapps\common\Call of Duty United Offensive') then begin
    Result := Steam + '\steamapps\common\Call of Duty United Offensive'; exit; end;
  if HasGame(Steam + '\steamapps\common\Call of Duty') then begin
    Result := Steam + '\steamapps\common\Call of Duty'; exit; end;
  if LoadStringsFromFile(Steam + '\steamapps\libraryfolders.vdf', Lines) then
    for I := 0 to GetArrayLength(Lines) - 1 do begin
      Line := Trim(Lines[I]);
      if Pos('"path"', Line) = 1 then begin
        Lib := Trim(Copy(Line, 7, Length(Line)));
        StringChangeEx(Lib, '"', '', True);
        StringChangeEx(Lib, '\\', '\', True);
        if HasGame(Lib + '\steamapps\common\Call of Duty United Offensive') then begin
          Result := Lib + '\steamapps\common\Call of Duty United Offensive'; exit; end;
        if HasGame(Lib + '\steamapps\common\Call of Duty') then begin
          Result := Lib + '\steamapps\common\Call of Duty'; exit; end;
      end;
    end;
end;

function DefaultGameDir(Param: String): String;
var
  S: String;
begin
  { retail UO writes its own key; it installs into the CoD folder, so try that one too }
  if RegQueryStringValue(HKLM32, 'SOFTWARE\Activision\Call of Duty United Offensive', 'InstallPath', S) and HasGame(S) then begin
    Result := S; exit; end;
  if RegQueryStringValue(HKLM32, 'SOFTWARE\Activision\Call of Duty', 'InstallPath', S) and HasGame(S) then begin
    Result := S; exit; end;
  if RegQueryStringValue(HKCU, 'Software\Valve\Steam', 'SteamPath', S) then begin
    S := SteamCandidate(S);
    if S <> '' then begin Result := S; exit; end;
  end;
  Result := ExpandConstant('{commonpf32}\Call of Duty');
end;

function U32At(const S: AnsiString; Ofs: Integer): Cardinal;
begin
  { Ofs is 0-based, Pascal strings 1-based }
  Result := Cardinal(Ord(S[Ofs + 1])) or (Cardinal(Ord(S[Ofs + 2])) shl 8) or
            (Cardinal(Ord(S[Ofs + 3])) shl 16) or (Cardinal(Ord(S[Ofs + 4])) shl 24);
end;

{ The same test the DLL makes (core/game.cpp): CoD:UO 1.51 is the CoDUOMP.exe with this
  PE timestamp. On any other exe CoDUO+ patches nothing, so warn instead of pretending. }
function IsUO151(const Exe: String): Boolean;
var
  S: AnsiString;
  Lfanew: Integer;
begin
  Result := False;
  if not LoadStringFromFile(Exe, S) then exit;
  if Length(S) < $40 then exit;
  Lfanew := Integer(U32At(S, $3C));
  if (Lfanew < $40) or (Lfanew > $100000) then exit;
  if Length(S) < Lfanew + 12 then exit;
  Result := U32At(S, Lfanew + 8) = UO_151_PE_TIMESTAMP;
end;

function IsCoduoplusDll(const Path: String): Boolean;
var
  S: AnsiString;
begin
  Result := LoadStringFromFile(Path, S) and (Pos(OUR_MARKER, S) > 0);
end;

function NextButtonClick(CurPageID: Integer): Boolean;
var
  Dir: String;
begin
  Result := True;
  if CurPageID <> wpSelectDir then exit;
  Dir := AddBackslash(WizardDirValue);
  if not FileExists(Dir + GAME_EXE) then begin
    SuppressibleMsgBox(GAME_EXE + ' is not in' + #13#10 + WizardDirValue + #13#10#13#10 +
      'Pick your Call of Duty: United Offensive folder (it has ' + GAME_EXE + ' in it).',
      mbError, MB_OK, IDOK);
    Result := False; exit;
  end;
  if not FileExists(Dir + 'mss32.dll') and not FileExists(Dir + 'mss32_original.dll') then begin
    SuppressibleMsgBox('mss32.dll is missing from this folder, so the game itself cannot start.' + #13#10 +
      'Repair or reinstall the game first.', mbError, MB_OK, IDOK);
    Result := False; exit;
  end;
  if not FileExists(Dir + 'mss32_original.dll') and IsCoduoplusDll(Dir + 'mss32.dll') then begin
    SuppressibleMsgBox('The mss32.dll in this folder is CoDUO+, but the game''s own mss32.dll ' +
      '(mss32_original.dll) is gone, so the game cannot play sound or start.' + #13#10 +
      'Repair or reinstall the game first, then run this setup again.', mbError, MB_OK, IDOK);
    Result := False; exit;
  end;
  if not IsUO151(Dir + GAME_EXE) then
    if SuppressibleMsgBox('This ' + GAME_EXE + ' is not Call of Duty: United Offensive 1.51.' + #13#10#13#10 +
         'CoDUO+ only works on 1.51 and stays switched off on anything else. ' +
         'Patch the game to 1.51 first.' + #13#10#13#10 + 'Install anyway?',
         mbConfirmation, MB_YESNO or MB_DEFBUTTON2, IDYES) <> IDYES then
      Result := False;
end;

procedure CurStepChanged(CurStep: TSetupStep);
var
  Dir: String;
begin
  if CurStep <> ssInstall then exit;
  Dir := ExpandConstant('{app}\');
  { The game's Miles DLL becomes mss32_original.dll: CoDUO+'s mss32.dll forwards all
    sound to it. Only a Miles DLL is moved - an older CoDUO+ is simply replaced. }
  if not FileExists(Dir + 'mss32_original.dll') and FileExists(Dir + 'mss32.dll') and
     not IsCoduoplusDll(Dir + 'mss32.dll') then begin
    if RenameFile(Dir + 'mss32.dll', Dir + 'mss32_original.dll') then
      Log('renamed mss32.dll (Miles) -> mss32_original.dll')
    else
      RaiseException('Could not rename mss32.dll to mss32_original.dll in ' + Dir +
                     ' - is the game running?');
  end;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  Dir: String;
begin
  if CurUninstallStep <> usPostUninstall then exit;
  Dir := ExpandConstant('{app}\');
  { our mss32.dll is gone by now: put the game's own back under its real name }
  if FileExists(Dir + 'mss32_original.dll') and not FileExists(Dir + 'mss32.dll') then
    if RenameFile(Dir + 'mss32_original.dll', Dir + 'mss32.dll') then
      Log('restored mss32_original.dll -> mss32.dll');
end;
