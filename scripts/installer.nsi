Unicode true
RequestExecutionLevel user
SetCompressor /SOLID lzma
SetCompressorDictSize 64

!ifndef PROJECT_ROOT
  !error "PROJECT_ROOT is required"
!endif
!ifndef OUT_DIR
  !error "OUT_DIR is required"
!endif

Name "FunASR Subtitle"
Caption "FunASR Subtitle Setup"
OutFile "${OUT_DIR}\FunASRSubtitle-Setup.exe"
InstallDir "$LOCALAPPDATA\Programs\FunASRSubtitle"
InstallDirRegKey HKCU "Software\FunASRSubtitle" "InstallDir"
Icon "${PROJECT_ROOT}\assets\funasr-app.ico"
UninstallIcon "${PROJECT_ROOT}\assets\funasr-app.ico"
BrandingText "FunASR Subtitle · Windows CPU real-time subtitles"
VIProductVersion "0.1.0.0"
VIAddVersionKey "ProductName" "FunASR Subtitle"
VIAddVersionKey "CompanyName" "FunASR Subtitle"
VIAddVersionKey "FileDescription" "Windows CPU real-time system audio subtitles"
VIAddVersionKey "FileVersion" "0.1.0"
VIAddVersionKey "LegalCopyright" "See NOTICE.md"

Page directory
Page instfiles
UninstPage uninstConfirm
UninstPage instfiles

Section "FunASR Subtitle" MainSection
  SetOutPath "$INSTDIR"
  File "${PROJECT_ROOT}\build\Release\FunASRSubtitle.exe"
  File "${PROJECT_ROOT}\assets\funasr-app.ico"
  File "${PROJECT_ROOT}\assets\funasr-tray.ico"
  File "${PROJECT_ROOT}\README.md"
  File "${PROJECT_ROOT}\vendor\NOTICE.md"

  SetOutPath "$INSTDIR\models"
  File "${PROJECT_ROOT}\models\sensevoice-small-q8.gguf"
  File "${PROJECT_ROOT}\models\fsmn-vad.gguf"
  SetOutPath "$INSTDIR\logs"

  CreateDirectory "$SMPROGRAMS\FunASR Subtitle"
  CreateShortCut "$SMPROGRAMS\FunASR Subtitle\FunASR Subtitle.lnk" "$INSTDIR\FunASRSubtitle.exe" "" "$INSTDIR\funasr-app.ico" 0
  CreateShortCut "$SMPROGRAMS\FunASR Subtitle\Uninstall.lnk" "$INSTDIR\Uninstall.exe"
  CreateShortCut "$DESKTOP\FunASR Subtitle.lnk" "$INSTDIR\FunASRSubtitle.exe" "" "$INSTDIR\funasr-app.ico" 0

  WriteUninstaller "$INSTDIR\Uninstall.exe"
  WriteRegStr HKCU "Software\FunASRSubtitle" "InstallDir" "$INSTDIR"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\FunASRSubtitle" "DisplayName" "FunASR Subtitle"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\FunASRSubtitle" "DisplayVersion" "0.1.0"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\FunASRSubtitle" "Publisher" "FunASR Subtitle"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\FunASRSubtitle" "InstallLocation" "$INSTDIR"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\FunASRSubtitle" "DisplayIcon" "$INSTDIR\funasr-app.ico"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\FunASRSubtitle" "UninstallString" '"$INSTDIR\Uninstall.exe"'
  WriteRegDWORD HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\FunASRSubtitle" "NoModify" 1
  WriteRegDWORD HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\FunASRSubtitle" "NoRepair" 1
SectionEnd

Section "Uninstall"
  Delete "$DESKTOP\FunASR Subtitle.lnk"
  Delete "$SMPROGRAMS\FunASR Subtitle\FunASR Subtitle.lnk"
  Delete "$SMPROGRAMS\FunASR Subtitle\Uninstall.lnk"
  RMDir "$SMPROGRAMS\FunASR Subtitle"
  DeleteRegKey HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\FunASRSubtitle"
  DeleteRegKey HKCU "Software\FunASRSubtitle"
  Delete "$INSTDIR\FunASRSubtitle.exe"
  Delete "$INSTDIR\funasr-app.ico"
  Delete "$INSTDIR\funasr-tray.ico"
  Delete "$INSTDIR\README.md"
  Delete "$INSTDIR\NOTICE.md"
  Delete "$INSTDIR\models\sensevoice-small-q8.gguf"
  Delete "$INSTDIR\models\fsmn-vad.gguf"
  Delete "$INSTDIR\Uninstall.exe"
  RMDir "$INSTDIR\models"
  RMDir "$INSTDIR\logs"
  RMDir "$INSTDIR"
SectionEnd
