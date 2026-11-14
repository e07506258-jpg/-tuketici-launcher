#define MyAppName "Tüketici Launcher"
#define MyAppVersion "1.0"
#define MyAppPublisher "SoldierGaming"
#define MyAppExeName "TitanLauncherCpp.exe"
#define BuildDir "C:\Users\e0750\Desktop\ModernLauncherSON\build\Desktop_Qt_6_12_0_MinGW_64_bit_Debug"

[Setup]
AppId={{A1B2C3D4-E5F6-7890-ABCD-EF1234567890}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
DefaultDirName={autopf}\TuketiciLauncher
DefaultGroupName={#MyAppName}
AllowNoIcons=yes
; MIT lisanslı açık kaynak proje — github.com üzerinden kaynak koduna ulaşabilirsiniz.
LicenseFile=C:\Users\e0750\Desktop\ModernLauncherSON\LICENSE
OutputDir=C:\Users\e0750\Desktop\ModernLauncherSON\installer_output
OutputBaseFilename=TuketiciLauncher-Setup-v1.0
SetupIconFile=C:\Users\e0750\Desktop\ModernLauncherSON\app.ico
Compression=lzma
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible

[Languages]
Name: "turkish"; MessagesFile: "compiler:Languages\Turkish.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "Masaüstü kısayolu oluştur"; GroupDescription: "Ek görevler:"; Flags: unchecked

[Files]
Source: "{#BuildDir}\TitanLauncherCpp.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#BuildDir}\Qt6Core.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#BuildDir}\Qt6Gui.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#BuildDir}\Qt6Network.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#BuildDir}\Qt6Svg.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#BuildDir}\Qt6Widgets.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#BuildDir}\libgcc_s_seh-1.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#BuildDir}\libstdc++-6.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#BuildDir}\libwinpthread-1.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#BuildDir}\D3Dcompiler_47.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#BuildDir}\opengl32sw.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#BuildDir}\platforms\*"; DestDir: "{app}\platforms"; Flags: ignoreversion recursesubdirs
Source: "{#BuildDir}\styles\*"; DestDir: "{app}\styles"; Flags: ignoreversion recursesubdirs
Source: "{#BuildDir}\imageformats\*"; DestDir: "{app}\imageformats"; Flags: ignoreversion recursesubdirs
Source: "{#BuildDir}\iconengines\*"; DestDir: "{app}\iconengines"; Flags: ignoreversion recursesubdirs
Source: "{#BuildDir}\networkinformation\*"; DestDir: "{app}\networkinformation"; Flags: ignoreversion recursesubdirs
Source: "{#BuildDir}\tls\*"; DestDir: "{app}\tls"; Flags: ignoreversion recursesubdirs

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{group}\{#MyAppName} Kaldır"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "Tüketici Launcher'ı başlat"; Flags: nowait postinstall skipifsilent

[UninstallDelete]
Type: filesandordirs; Name: "{app}"
