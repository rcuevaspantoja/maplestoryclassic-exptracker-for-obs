; Inno Setup script for the Windows installer.
;
; Build it after installing the plugin into a staging folder:
;   cmake --install build_x64 --config RelWithDebInfo --prefix release/RelWithDebInfo
;   iscc /DAppVersion=0.1.1 /DStagingDir=<repo>\release\RelWithDebInfo /DOutputDir=<repo>\release installer\exp-meter-for-obs.iss
;
; The CI does this from .github/scripts/Package-Windows.ps1.

#define PluginName "exp-meter-for-obs"
#define DisplayName "EXP Meter for MapleStory Classic"
#define Website "https://github.com/rcuevaspantoja/maplestoryclassic-exptracker-for-obs"
#define MinObsMajor 30

#ifndef AppVersion
  #error Pass the plugin version with /DAppVersion=x.y.z
#endif
#ifndef StagingDir
  #error Pass the cmake --install prefix with /DStagingDir=<path>
#endif
#ifndef Publisher
  #define Publisher "@rcuevaspantoja"
#endif
#ifndef OutputDir
  #define OutputDir "."
#endif

[Setup]
AppId={{0E9320D6-814F-4C6A-B38A-717CD4737CEB}
AppName={#DisplayName}
AppVersion={#AppVersion}
AppVerName={#DisplayName} {#AppVersion}
AppPublisher={#Publisher}
AppPublisherURL={#Website}
AppSupportURL={#Website}/issues
; OBS 30+ loads plugins from %ProgramData%\obs-studio\plugins. Users may create
; folders there, so no admin rights are needed.
DefaultDirName={commonappdata}\obs-studio\plugins\{#PluginName}
DisableDirPage=yes
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
; OBS holds this mutex while it runs (frontend/utility/platform-windows.cpp).
; The plugin only loads at OBS startup, so we ask the user to close it first.
AppMutex=OBSStudioCore
UninstallDisplayName={#DisplayName}
OutputDir={#OutputDir}
OutputBaseFilename={#PluginName}-{#AppVersion}-windows-x64-installer
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
ShowLanguageDialog=auto

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "spanish"; MessagesFile: "compiler:Languages\Spanish.isl"

[Messages]
english.SetupAppRunningError=OBS Studio is running.%n%nClose OBS, then click OK to continue, or Cancel to exit.
spanish.SetupAppRunningError=OBS Studio está abierto.%n%nCierra OBS y haz clic en Aceptar para continuar, o en Cancelar para salir.
english.UninstallAppRunningError=OBS Studio is running.%n%nClose OBS, then click OK to continue, or Cancel to exit.
spanish.UninstallAppRunningError=OBS Studio está abierto.%n%nCierra OBS y haz clic en Aceptar para continuar, o en Cancelar para salir.
english.FinishedLabel=EXP Meter is installed.%n%nIn OBS, open Docks > EXP Meter, pick your game capture and click Add to scene.
spanish.FinishedLabel=EXP Meter está instalado.%n%nEn OBS, abre Paneles > EXP Meter, elige tu captura del juego y haz clic en Agregar a la escena.

[CustomMessages]
english.ObsNotFound=OBS Studio was not found on this PC.%n%nEXP Meter is a plugin for OBS Studio {#MinObsMajor} or newer. Install anyway?
spanish.ObsNotFound=No se encontró OBS Studio en este PC.%n%nEXP Meter es un plugin para OBS Studio {#MinObsMajor} o superior. ¿Instalar de todos modos?
english.ObsTooOld=EXP Meter needs OBS Studio {#MinObsMajor} or newer, and this PC has OBS Studio %1.%n%nUpdate OBS first. Install anyway?
spanish.ObsTooOld=EXP Meter necesita OBS Studio {#MinObsMajor} o superior, y este PC tiene OBS Studio %1.%n%nActualiza OBS primero. ¿Instalar de todos modos?
english.LaunchObs=Open OBS Studio
spanish.LaunchObs=Abrir OBS Studio

[InstallDelete]
; Drop overlay/locale files that a previous version had but this one doesn't.
Type: filesandordirs; Name: "{app}\data"

[Files]
Source: "{#StagingDir}\{#PluginName}\*"; DestDir: "{app}"; Excludes: "*.pdb"; Flags: ignoreversion recursesubdirs createallsubdirs

[Run]
Filename: "{code:ObsExe}"; WorkingDir: "{code:ObsBinDir}"; Description: "{cm:LaunchObs}"; Flags: postinstall nowait skipifsilent; Check: ObsFound

[Code]
var
  ObsDir: String;

function ObsBinDir(Param: String): String;
begin
  Result := ObsDir + '\bin\64bit';
end;

function ObsExe(Param: String): String;
begin
  Result := ObsBinDir('') + '\obs64.exe';
end;

function ObsFound: Boolean;
begin
  Result := (ObsDir <> '') and FileExists(ObsExe(''));
end;

function InitializeSetup: Boolean;
var
  Version: String;
  Major: Integer;
begin
  Result := True;
  { The OBS installer stores its folder as the default value of this key. }
  if not RegQueryStringValue(HKLM64, 'SOFTWARE\OBS Studio', '', ObsDir) then
    ObsDir := '';

  if not ObsFound then
  begin
    Result := SuppressibleMsgBox(CustomMessage('ObsNotFound'), mbConfirmation, MB_YESNO, IDYES) = IDYES;
    Exit;
  end;

  if GetVersionNumbersString(ObsExe(''), Version) then
  begin
    Major := StrToIntDef(Copy(Version, 1, Pos('.', Version) - 1), 0);
    if Major < {#MinObsMajor} then
      Result := SuppressibleMsgBox(FmtMessage(CustomMessage('ObsTooOld'), [Version]), mbConfirmation, MB_YESNO, IDYES) = IDYES;
  end;
end;
