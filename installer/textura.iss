; Windows installer: the VST3 into the shared VST3 folder, the standalone into Program Files.
[Setup]
AppName=Textura
AppVersion=1.0.1
AppPublisher=Liberatus
DefaultDirName={autopf}\Textura
DefaultGroupName=Textura
DisableProgramGroupPage=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir=..\dist
OutputBaseFilename=Textura-1.0.1-windows
Compression=lzma2
SolidCompression=yes
LicenseFile=..\LICENSE

[Files]
Source: "..\build\Textura_artefacts\Release\VST3\Textura.vst3\*"; DestDir: "{commoncf64}\VST3\Textura.vst3"; Flags: recursesubdirs ignoreversion
Source: "..\build\Textura_artefacts\Release\Standalone\Textura.exe"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\Textura"; Filename: "{app}\Textura.exe"
