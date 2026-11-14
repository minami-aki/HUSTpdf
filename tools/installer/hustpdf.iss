; HUSTpdf - Inno Setup script (Inno Setup 6.7+). Build with tools\make_installer.ps1, which stages
; the files and passes:
;   /DAppVersion=0.1.0  /DStageMain=<main stage folder>  /DStageCuda=<CUDA add-on folder>  /DOutDir=<dist>
;
; One setup.exe with everything except the translation model (downloaded from inside the app):
; CPU (every ggml-cpu-* variant), Vulkan, and the CUDA backend + NVIDIA runtime (optional
; component, on by default). The installer asks "only for me" (no admin, %LOCALAPPDATA%\Programs)
; or "all users" (admin, Program Files). User data (%LOCALAPPDATA%\HUSTpdf: model, cache,
; settings) is kept on uninstall.

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef StageMain
  #error StageMain is not defined (use tools\make_installer.ps1)
#endif
#ifndef OutDir
  #define OutDir "."
#endif

#define AppName "HUSTpdf"
#define AppExe "HUSTpdf.exe"

[Setup]
; Fixed AppId: upgrades replace the previous installation instead of installing side by side.
AppId={{B3FC8EF8-274D-4B69-BC06-C23EA995111A}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher={#AppName}
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
LicenseFile={#SourcePath}\..\..\LICENSE
SetupIconFile={#SourcePath}\..\..\res\HUSTpdf.ico
UninstallDisplayIcon={app}\{#AppExe}
UninstallDisplayName={#AppName} {#AppVersion}
OutputDir={#OutDir}
OutputBaseFilename={#AppName}-{#AppVersion}-win64-setup
Compression=lzma2/ultra64
SolidCompression=yes
LZMAUseSeparateProcess=yes
LZMANumBlockThreads=4
WizardStyle=modern
CloseApplications=yes
RestartApplications=no
ChangesAssociations=yes

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[CustomMessages]
english.CompMain=HUSTpdf (trình đọc PDF + dịch Anh-Việt trên CPU và GPU Vulkan)
english.CompCuda=Tăng tốc GPU NVIDIA bằng CUDA (cuBLAS, ~560 MB). Không chọn thì GPU NVIDIA vẫn chạy qua Vulkan
english.TypeFull=Đầy đủ (CPU, Vulkan, CUDA)
english.TypeCompact=Gọn (CPU, Vulkan - không có CUDA)
english.TypeCustom=Tùy chọn
english.TaskDesktop=Tạo biểu tượng trên màn hình nền
english.TaskOpenWith=Thêm HUSTpdf vào menu "Open with" của file PDF (không đổi chương trình mặc định)
english.RunApp=Mở HUSTpdf
english.ModelNote=Mô hình dịch (~1,1 GB) không có trong bộ cài: lần chạy đầu HUSTpdf sẽ hỏi tải về từ Hugging Face hoặc chọn file GGUF có sẵn.

[Types]
Name: "full"; Description: "{cm:TypeFull}"
Name: "compact"; Description: "{cm:TypeCompact}"
Name: "custom"; Description: "{cm:TypeCustom}"; Flags: iscustom

[Components]
Name: "main"; Description: "{cm:CompMain}"; Types: full compact custom; Flags: fixed
#ifdef StageCuda
Name: "cuda"; Description: "{cm:CompCuda}"; Types: full
#endif

[Tasks]
Name: "desktopicon"; Description: "{cm:TaskDesktop}"; GroupDescription: "{cm:AdditionalIcons}"
Name: "openwith"; Description: "{cm:TaskOpenWith}"

[Files]
Source: "{#StageMain}\*"; DestDir: "{app}"; Components: main; Flags: ignoreversion recursesubdirs createallsubdirs
#ifdef StageCuda
Source: "{#StageCuda}\*"; DestDir: "{app}"; Components: cuda; Flags: ignoreversion recursesubdirs createallsubdirs
#endif

[InstallDelete]
; Upgrading with CUDA deselected: remove CUDA files left by a previous installation.
#ifdef StageCuda
Type: files; Name: "{app}\ggml-cuda.dll"; Components: not cuda
Type: files; Name: "{app}\cublas64_*.dll"; Components: not cuda
Type: files; Name: "{app}\cublasLt64_*.dll"; Components: not cuda
Type: files; Name: "{app}\README-CUDA.txt"; Components: not cuda
#endif

[Icons]
Name: "{autoprograms}\{#AppName}"; Filename: "{app}\{#AppExe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExe}"; Tasks: desktopicon

[Registry]
; "Open with" entry for .pdf (does not take over the default PDF program).
Root: HKA; Subkey: "Software\Classes\HUSTpdf.pdf"; ValueType: string; ValueName: ""; ValueData: "PDF (HUSTpdf)"; Flags: uninsdeletekey; Tasks: openwith
Root: HKA; Subkey: "Software\Classes\HUSTpdf.pdf\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\{#AppExe},0"; Tasks: openwith
Root: HKA; Subkey: "Software\Classes\HUSTpdf.pdf\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#AppExe}"" ""%1"""; Tasks: openwith
Root: HKA; Subkey: "Software\Classes\.pdf\OpenWithProgids"; ValueType: string; ValueName: "HUSTpdf.pdf"; ValueData: ""; Flags: uninsdeletevalue; Tasks: openwith
Root: HKA; Subkey: "Software\Classes\Applications\{#AppExe}\SupportedTypes"; ValueType: string; ValueName: ".pdf"; ValueData: ""; Flags: uninsdeletekey; Tasks: openwith

[Run]
Filename: "{app}\{#AppExe}"; Description: "{cm:RunApp}"; Flags: nowait postinstall skipifsilent

[Code]
procedure CurPageChanged(CurPageID: Integer);
begin
  if CurPageID = wpFinished then
    WizardForm.FinishedLabel.Caption := WizardForm.FinishedLabel.Caption + #13#10#13#10 + CustomMessage('ModelNote');
end;
