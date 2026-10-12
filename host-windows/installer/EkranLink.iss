; EkranLink kurulum programı (Inno Setup 6.3+). tools\paketle.ps1 derler:
;   ISCC /DSurum=0.4.3 /DKaynak=<host-windows\build> /DAdb=<platform-tools> /O<dist> EkranLink.iss
;
; Program Files\EkranLink'e kurar (hizmet bu yolu bekler: service.cpp installDir). Güncellemede çalışan
; EkranLink'i düzgünce kapatır (WM_CLOSE: yayını durdurur, sanal monitörü kaldırır), hizmet kuruluysa
; durdurup yeni sürümle yeniden kurar. Kaldırırken hizmeti kaldırır; ayarları ve eşleşmeleri silmeyi sorar.
; Sanal ekran sürücüsü henüz pakette değil (dağıtım imzası gerekiyor, driver/README.md).

#ifndef Surum
  #error "Surum tanımlı değil: tools\paketle.ps1 ile derleyin"
#endif

[Setup]
AppId={{FE85B2AA-7945-4E39-959A-3D9CC4C2FD2F}
AppName=EkranLink
AppVersion={#Surum}
AppVerName=EkranLink {#Surum}
AppPublisher=EkranLink
AppPublisherURL=https://github.com/meg55/EkranLink
AppSupportURL=https://github.com/meg55/EkranLink/issues
AppUpdatesURL=https://github.com/meg55/EkranLink/releases
VersionInfoVersion={#Surum}
DefaultDirName={autopf}\EkranLink
DisableDirPage=yes
DisableProgramGroupPage=yes
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.17763
OutputBaseFilename=EkranLink-{#Surum}-windows-setup
SetupIconFile=..\res\ekranlink.ico
UninstallDisplayIcon={app}\EkranLink.exe
UninstallDisplayName=EkranLink
WizardStyle=modern
Compression=lzma2/max
SolidCompression=yes
; adb sunucusu gibi {app}'teki dosyaları kullanan süreçler (Restart Manager)
CloseApplications=force
RestartApplications=no
SetupMutex=EkranLink-kurulum

[Languages]
Name: en; MessagesFile: compiler:Default.isl
Name: tr; MessagesFile: compiler:Languages\Turkish.isl

[CustomMessages]
en.Autostart=Start EkranLink when Windows starts (in the background)
tr.Autostart=Windows başlarken EkranLink'i başlat (arka planda)
en.ServiceUpdate=Updating the EkranLink service...
tr.ServiceUpdate=EkranLink hizmeti güncelleniyor...
en.DeleteData=Also delete EkranLink settings and device pairings?%n%nChoose "No" if you plan to install EkranLink again.
tr.DeleteData=EkranLink ayarları ve cihaz eşleşmeleri de silinsin mi?%n%nEkranLink'i yeniden kurmayı düşünüyorsanız "Hayır" deyin.

[Tasks]
Name: autostart; Description: {cm:Autostart}
Name: desktopicon; Description: {cm:CreateDesktopIcon}; GroupDescription: {cm:AdditionalIcons}; Flags: unchecked

[Files]
Source: "{#Kaynak}\EkranLink.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#Kaynak}\ekranlink-host.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#Adb}\adb.exe"; DestDir: "{app}\adb"; Flags: ignoreversion
Source: "{#Adb}\AdbWinApi.dll"; DestDir: "{app}\adb"; Flags: ignoreversion
Source: "{#Adb}\AdbWinUsbApi.dll"; DestDir: "{app}\adb"; Flags: ignoreversion
Source: "{#Adb}\NOTICE.txt"; DestDir: "{app}\adb"; Flags: ignoreversion
Source: "..\..\LICENSE"; DestDir: "{app}"; DestName: "LICENSE.txt"; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\EkranLink"; Filename: "{app}\EkranLink.exe"
Name: "{autodesktop}\EkranLink"; Filename: "{app}\EkranLink.exe"; Tasks: desktopicon

[Run]
; Hizmet kuruluydu: yeni sürümle yeniden kur (sürüm kaydı) ve başlat
Filename: "{app}\EkranLink.exe"; Parameters: "--hizmet-kur"; StatusMsg: "{cm:ServiceUpdate}"; Flags: runhidden waituntilterminated; Check: HizmetVardi
; Otomatik başlatma kullanıcının kendi kaydına (HKCU) yazılır: kurulumu başlatan kullanıcı adına
Filename: "{app}\EkranLink.exe"; Parameters: "--windows-ile-baslat"; Flags: runasoriginaluser runhidden waituntilterminated; Tasks: autostart
; Sessiz güncelleme (winget vb.): önceden çalışıyorduysa arka planda yeniden başlat
Filename: "{app}\EkranLink.exe"; Parameters: "--arka-plan"; Flags: runasoriginaluser nowait; Check: SessizVeCalisiyordu
Filename: "{app}\EkranLink.exe"; Description: "{cm:LaunchProgram,EkranLink}"; Flags: runasoriginaluser nowait postinstall skipifsilent

[UninstallRun]
; Hizmet yoksa da zararsız: ayarları kullanıcıya geri taşır, makine kaydını siler
Filename: "{app}\EkranLink.exe"; Parameters: "--hizmet-kaldir"; Flags: runhidden waituntilterminated; RunOnceId: "HizmetKaldir"

[UninstallDelete]
; Hizmet günlükleri; kilitli exe'lerin kenara alınmış eski kopyaları (service.cpp copyFile)
Type: filesandordirs; Name: "{commonappdata}\EkranLink"
Type: files; Name: "{app}\*.eski"
Type: files; Name: "{app}\adb\*.eski"

[Code]
const
  WM_CLOSE = $0010;
  RunKey = 'Software\Microsoft\Windows\CurrentVersion\Run';

var
  ServiceWasInstalled: Boolean;
  TrayWasRunning: Boolean;

function FindWindowEx(Parent, ChildAfter: HWND; ClassName, WindowName: String): HWND;
  external 'FindWindowExW@user32.dll stdcall';

// Tepsi uygulaması mesaj penceresi (tray.cpp: sınıf EkranLinkTray, ad EkranLink). WM_CLOSE ile düzgünce kapanır.
function CloseTray: Boolean;
var
  MessageOnly, W: HWND;
  I: Integer;
begin
  Result := False;
  MessageOnly := -3;  // HWND_MESSAGE
  for I := 1 to 100 do
  begin
    W := FindWindowEx(MessageOnly, 0, 'EkranLinkTray', 'EkranLink');
    if W = 0 then Break;
    Result := True;
    if (I mod 20) = 1 then PostMessage(W, WM_CLOSE, 0, 0);
    Sleep(100);
  end;
end;

function ServiceExists: Boolean;
begin
  Result := RegKeyExists(HKLM, 'SYSTEM\CurrentControlSet\Services\EkranLink');
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
var
  Code: Integer;
begin
  TrayWasRunning := CloseTray;
  ServiceWasInstalled := ServiceExists;
  if ServiceWasInstalled then
    Exec(ExpandConstant('{sys}\net.exe'), 'stop EkranLink', '', SW_HIDE, ewWaitUntilTerminated, Code);
  Result := '';
end;

function HizmetVardi: Boolean;
begin
  Result := ServiceWasInstalled;
end;

function SessizVeCalisiyordu: Boolean;
begin
  Result := WizardSilent and TrayWasRunning;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  Value: String;
begin
  if CurUninstallStep = usUninstall then
    CloseTray
  else if CurUninstallStep = usPostUninstall then
  begin
    // Otomatik başlatma kaydı yalnızca bu kuruluma aitse (taşınabilir zip sürümüne dokunma)
    if RegQueryStringValue(HKCU, RunKey, 'EkranLink', Value) and
       (Pos(Lowercase(ExpandConstant('{app}\')), Lowercase(Value)) > 0) then
      RegDeleteValue(HKCU, RunKey, 'EkranLink');
    if not UninstallSilent and
       (MsgBox(CustomMessage('DeleteData'), mbConfirmation, MB_YESNO or MB_DEFBUTTON2) = IDYES) then
    begin
      RegDeleteKeyIncludingSubkeys(HKCU, 'Software\EkranLink');
      DelTree(ExpandConstant('{localappdata}\EkranLink'), True, True, True);
    end;
  end;
end;
