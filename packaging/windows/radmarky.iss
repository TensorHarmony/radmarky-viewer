#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef NumericAppVersion
  #define NumericAppVersion "0.0.0"
#endif

#ifndef ReleaseDir
  #define ReleaseDir "..\..\build\release\Release"
#endif

#ifndef VCRedistPath
  #define VCRedistPath "..\..\out\prerequisites\vc_redist.x64.exe"
#endif

#ifndef VCRedistMajor
  #define VCRedistMajor 14
#endif

#ifndef VCRedistMinor
  #define VCRedistMinor 0
#endif

#ifndef VCRedistBuild
  #define VCRedistBuild 0
#endif

#ifndef VCRedistRevision
  #define VCRedistRevision 0
#endif

#define AppName "RadMarky Viewer"
#define AppPublisher "TensorHarmony Technologies Inc."
#define AppExecutable "radmarky_viewer.exe"

[Setup]
AppId={{83C3A95A-8769-4310-A351-1067353BFBB3}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher={#AppPublisher}
VersionInfoVersion={#NumericAppVersion}
VersionInfoCompany={#AppPublisher}
VersionInfoDescription={#AppName} installer
DefaultDirName={autopf}\RadMarky Viewer
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
LicenseFile=..\..\LICENSE
SetupIconFile=..\..\resources\platform\windows\radmarky.ico
UninstallDisplayIcon={app}\radmarky.ico
OutputDir=..\..\out\installers
OutputBaseFilename=RadMarky-Viewer-{#AppVersion}-Setup
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
CloseApplications=yes
CloseApplicationsFilter={#AppExecutable}
RestartApplications=no
MinVersion=10.0

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "Create a &desktop shortcut"; GroupDescription: "Additional shortcuts:"; Flags: unchecked

[Files]
; RadMarky application
Source: "{#ReleaseDir}\radmarky_viewer.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\..\resources\platform\windows\radmarky.ico"; DestDir: "{app}"; DestName: "radmarky.ico"; Flags: ignoreversion

; Qt runtime
Source: "{#ReleaseDir}\Qt6Core.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#ReleaseDir}\Qt6Gui.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#ReleaseDir}\Qt6OpenGL.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#ReleaseDir}\Qt6OpenGLWidgets.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#ReleaseDir}\Qt6Svg.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#ReleaseDir}\Qt6Widgets.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#ReleaseDir}\platforms\qwindows.dll"; DestDir: "{app}\platforms"; Flags: ignoreversion

; ITK and GDCM
Source: "{#ReleaseDir}\ITKCommon-5.4.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#ReleaseDir}\ITKIOGDCM-5.4.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#ReleaseDir}\ITKIOImageBase-5.4.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#ReleaseDir}\ITKIONIFTI-5.4.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#ReleaseDir}\gdcmcharls.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#ReleaseDir}\gdcmCommon.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#ReleaseDir}\gdcmDICT.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#ReleaseDir}\gdcmDSED.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#ReleaseDir}\gdcmIOD.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#ReleaseDir}\gdcmjpeg8.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#ReleaseDir}\gdcmjpeg12.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#ReleaseDir}\gdcmjpeg16.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#ReleaseDir}\gdcmMSFF.dll"; DestDir: "{app}"; Flags: ignoreversion

; VTK runtime
Source: "{#ReleaseDir}\verdict.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#ReleaseDir}\vtkCommonColor-9.3.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#ReleaseDir}\vtkCommonComputationalGeometry-9.3.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#ReleaseDir}\vtkCommonCore-9.3.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#ReleaseDir}\vtkCommonDataModel-9.3.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#ReleaseDir}\vtkCommonExecutionModel-9.3.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#ReleaseDir}\vtkCommonMath-9.3.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#ReleaseDir}\vtkCommonMisc-9.3.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#ReleaseDir}\vtkCommonSystem-9.3.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#ReleaseDir}\vtkCommonTransforms-9.3.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#ReleaseDir}\vtkDICOMParser-9.3.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#ReleaseDir}\vtkFiltersCore-9.3.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#ReleaseDir}\vtkFiltersExtraction-9.3.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#ReleaseDir}\vtkFiltersGeneral-9.3.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#ReleaseDir}\vtkFiltersGeometry-9.3.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#ReleaseDir}\vtkFiltersHybrid-9.3.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#ReleaseDir}\vtkFiltersHyperTree-9.3.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#ReleaseDir}\vtkFiltersModeling-9.3.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#ReleaseDir}\vtkFiltersSources-9.3.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#ReleaseDir}\vtkFiltersStatistics-9.3.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#ReleaseDir}\vtkFiltersTexture-9.3.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#ReleaseDir}\vtkFiltersVerdict-9.3.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#ReleaseDir}\vtkGUISupportQt-9.3.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#ReleaseDir}\vtkImagingColor-9.3.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#ReleaseDir}\vtkImagingCore-9.3.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#ReleaseDir}\vtkImagingGeneral-9.3.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#ReleaseDir}\vtkImagingHybrid-9.3.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#ReleaseDir}\vtkImagingSources-9.3.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#ReleaseDir}\vtkInteractionStyle-9.3.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#ReleaseDir}\vtkInteractionWidgets-9.3.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#ReleaseDir}\vtkIOCore-9.3.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#ReleaseDir}\vtkIOImage-9.3.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#ReleaseDir}\vtkIOLegacy-9.3.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#ReleaseDir}\vtkIOXML-9.3.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#ReleaseDir}\vtkIOXMLParser-9.3.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#ReleaseDir}\vtkkissfft-9.3.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#ReleaseDir}\vtkloguru-9.3.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#ReleaseDir}\vtkmetaio-9.3.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#ReleaseDir}\vtkParallelCore-9.3.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#ReleaseDir}\vtkParallelDIY-9.3.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#ReleaseDir}\vtkRenderingAnnotation-9.3.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#ReleaseDir}\vtkRenderingContext2D-9.3.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#ReleaseDir}\vtkRenderingCore-9.3.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#ReleaseDir}\vtkRenderingFreeType-9.3.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#ReleaseDir}\vtkRenderingHyperTreeGrid-9.3.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#ReleaseDir}\vtkRenderingOpenGL2-9.3.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#ReleaseDir}\vtkRenderingUI-9.3.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#ReleaseDir}\vtksys-9.3.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#ReleaseDir}\vtktoken-9.3.dll"; DestDir: "{app}"; Flags: ignoreversion

; FFmpeg and supporting libraries
Source: "{#ReleaseDir}\avcodec-63.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#ReleaseDir}\avformat-63.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#ReleaseDir}\avutil-61.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#ReleaseDir}\swscale-10.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#ReleaseDir}\libx264-164.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#ReleaseDir}\archive.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#ReleaseDir}\brotlicommon.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#ReleaseDir}\brotlidec.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#ReleaseDir}\bz2.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#ReleaseDir}\double-conversion.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#ReleaseDir}\fmt.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#ReleaseDir}\freetype.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#ReleaseDir}\gif.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#ReleaseDir}\glew32.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#ReleaseDir}\jpeg62.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#ReleaseDir}\libexpat.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#ReleaseDir}\liblzma.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#ReleaseDir}\libpng16.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#ReleaseDir}\lz4.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#ReleaseDir}\md4c.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#ReleaseDir}\openjp2.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#ReleaseDir}\pcre2-16.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#ReleaseDir}\pugixml.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#ReleaseDir}\tiff.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#ReleaseDir}\z.dll"; DestDir: "{app}"; Flags: ignoreversion

; Bundled prerequisite (not installed into the application directory)
Source: "{#VCRedistPath}"; DestName: "vc_redist.x64.exe"; Flags: dontcopy

[Icons]
Name: "{autoprograms}\{#AppName}"; Filename: "{app}\{#AppExecutable}"; WorkingDir: "{app}"; IconFilename: "{app}\radmarky.ico"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExecutable}"; WorkingDir: "{app}"; IconFilename: "{app}\radmarky.ico"; Tasks: desktopicon

[Run]
Filename: "{app}\{#AppExecutable}"; Description: "Launch {#AppName}"; Flags: nowait postinstall skipifsilent

[Code]
const
  VCRuntimeRegistryKey =
    'SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\x64';

function InstalledVCRuntimeIsCurrent: Boolean;
var
  Installed: Cardinal;
  Major: Cardinal;
  Minor: Cardinal;
  Build: Cardinal;
  Revision: Cardinal;
begin
  Result := False;

  if not RegQueryDWordValue(
    HKLM64, VCRuntimeRegistryKey, 'Installed', Installed) or
    (Installed <> 1) then
    Exit;

  if not RegQueryDWordValue(
    HKLM64, VCRuntimeRegistryKey, 'Major', Major) then
    Exit;
  if not RegQueryDWordValue(
    HKLM64, VCRuntimeRegistryKey, 'Minor', Minor) then
    Exit;
  if not RegQueryDWordValue(
    HKLM64, VCRuntimeRegistryKey, 'Bld', Build) then
    Exit;
  if not RegQueryDWordValue(
    HKLM64, VCRuntimeRegistryKey, 'Rbld', Revision) then
    Exit;

  Result :=
    (Major > {#VCRedistMajor}) or
    ((Major = {#VCRedistMajor}) and (Minor > {#VCRedistMinor})) or
    ((Major = {#VCRedistMajor}) and (Minor = {#VCRedistMinor}) and
      (Build > {#VCRedistBuild})) or
    ((Major = {#VCRedistMajor}) and (Minor = {#VCRedistMinor}) and
      (Build = {#VCRedistBuild}) and (Revision >= {#VCRedistRevision}));
end;

procedure AppendReadyMemoSection(
  var Memo: String; const Section, NewLine: String);
begin
  if Section = '' then
    Exit;

  if Memo <> '' then
    Memo := Memo + NewLine + NewLine;
  Memo := Memo + Section;
end;

function UpdateReadyMemo(
  Space, NewLine, MemoUserInfoInfo, MemoDirInfo, MemoTypeInfo,
  MemoComponentsInfo, MemoGroupInfo, MemoTasksInfo: String): String;
var
  PrerequisiteInfo: String;
begin
  Result := '';
  AppendReadyMemoSection(Result, MemoUserInfoInfo, NewLine);
  AppendReadyMemoSection(Result, MemoDirInfo, NewLine);
  AppendReadyMemoSection(Result, MemoTypeInfo, NewLine);
  AppendReadyMemoSection(Result, MemoComponentsInfo, NewLine);
  AppendReadyMemoSection(Result, MemoGroupInfo, NewLine);
  AppendReadyMemoSection(Result, MemoTasksInfo, NewLine);

  PrerequisiteInfo := 'Required prerequisite:' + NewLine;
  if InstalledVCRuntimeIsCurrent then
    PrerequisiteInfo := PrerequisiteInfo + Space +
      'Microsoft Visual C++ v14 x64 Runtime: a compatible version is ' +
      'already installed, so no runtime change is required.'
  else
    PrerequisiteInfo := PrerequisiteInfo + Space +
      'Microsoft Visual C++ v14 x64 Runtime ' +
      '{#VCRedistMajor}.{#VCRedistMinor}.{#VCRedistBuild}.{#VCRedistRevision}' +
      ' will be installed or updated system-wide. The Microsoft runtime is ' +
      'bundled with this installer, so no download is required. Windows may ' +
      'require a restart.';
  AppendReadyMemoSection(Result, PrerequisiteInfo, NewLine);
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
var
  ResultCode: Integer;
begin
  Result := '';

  if InstalledVCRuntimeIsCurrent then
  begin
    Log('A current Microsoft Visual C++ v14 x64 runtime is already installed.');
    Exit;
  end;

  Log('Installing the Microsoft Visual C++ v14 x64 runtime.');
  WizardForm.PreparingLabel.Caption :=
    'Installing Microsoft Visual C++ runtime prerequisite...';
  ExtractTemporaryFile('vc_redist.x64.exe');

  if not Exec(
    ExpandConstant('{tmp}\vc_redist.x64.exe'),
    '/install /passive /norestart', '', SW_SHOW, ewWaitUntilTerminated,
    ResultCode) then
  begin
    Result := 'The Microsoft Visual C++ runtime installer could not be started.';
    Exit;
  end;

  if ResultCode = 3010 then
  begin
    NeedsRestart := True;
    Exit;
  end;

  if ResultCode <> 0 then
    Result := Format('Microsoft Visual C++ runtime installation failed ' +
      '(exit code %d).', [ResultCode]);
end;
