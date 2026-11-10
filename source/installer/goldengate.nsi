; Golden Gate for Windows 11 - installer (NSIS).
; Per-user install (no admin needed): everything Golden Gate changes is in
; HKCU, so it installs to %LocalAppData% and starts at login for this user.
; The uninstaller runs "GoldenGate.exe --restore" first to put Windows back.
Unicode true
!include "MUI2.nsh"
!include "FileFunc.nsh"

!define APP "Golden Gate"
!define VER "2.4.0"
!define EXE "GoldenGate.exe"

Name "${APP}"
OutFile "..\dist\GoldenGate-Setup.exe"
RequestExecutionLevel user
InstallDir "$LOCALAPPDATA\GoldenGate"
ShowInstDetails show
ShowUnInstDetails show
SetCompressor /SOLID lzma
BrandingText "${APP} ${VER}"

!define MUI_ICON "..\assets\GoldenGate.ico"
!define MUI_UNICON "..\assets\GoldenGate.ico"
!define MUI_ABORTWARNING
!define MUI_FINISHPAGE_TEXT "Golden Gate is installed. It starts automatically when you sign in.$\r$\n$\r$\nOpen its settings any time from the circle icon at the top of the screen, or the tray icon."

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "..\dist\LICENSE.txt"
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "English"

Section "Golden Gate" SecMain
  SectionIn RO
  ; Stop a running copy before replacing files (Windows keeps a running exe, so the
  ; update wouldn't apply). Only now, once you've clicked Install, so the Dock and
  ; panel stay up while you go through the pages. It's stopped, not asked to quit:
  ; a normal quit gives the Windows taskbar back, and an update must not flash it.
  ; The new version is started again below, and keeps the taskbar hidden.
  nsExec::Exec 'taskkill /F /IM ${EXE}'
  Pop $0
  Sleep 500
  SetOutPath "$INSTDIR"
  File "..\dist\${EXE}"
  File "..\assets\GoldenGate.ico"
  File "..\dist\LICENSE.txt"
  File "..\dist\LICENSE-GPL-3.0.txt"
  File "..\dist\README.txt"
  SetOutPath "$INSTDIR\wallpapers"
  File "..\assets\wallpapers\*.jpg"
  SetOutPath "$INSTDIR\cursors"
  File /r "..\assets\cursors\*"
  SetOutPath "$INSTDIR\icons"
  File "..\assets\icons\*.png"
  File "..\assets\icons\icons.map"
  SetOutPath "$INSTDIR\fonts"
  File "..\assets\fonts\*"

  ; install the Inter font for this user (per-user font, no admin)
  StrCpy $0 "$FONTS"
  SetShellVarContext current
  StrCpy $1 "$LOCALAPPDATA\Microsoft\Windows\Fonts"
  CreateDirectory "$1"
  ClearErrors
  CopyFiles /SILENT "$INSTDIR\fonts\Inter-Regular.ttf" "$1\Inter-Regular.ttf"
  CopyFiles /SILENT "$INSTDIR\fonts\Inter-Medium.ttf" "$1\Inter-Medium.ttf"
  CopyFiles /SILENT "$INSTDIR\fonts\Inter-SemiBold.ttf" "$1\Inter-SemiBold.ttf"
  CopyFiles /SILENT "$INSTDIR\fonts\Inter-Bold.ttf" "$1\Inter-Bold.ttf"
  WriteRegStr HKCU "Software\Microsoft\Windows NT\CurrentVersion\Fonts" "Inter (TrueType)" "$1\Inter-Regular.ttf"
  WriteRegStr HKCU "Software\Microsoft\Windows NT\CurrentVersion\Fonts" "Inter Medium (TrueType)" "$1\Inter-Medium.ttf"
  WriteRegStr HKCU "Software\Microsoft\Windows NT\CurrentVersion\Fonts" "Inter SemiBold (TrueType)" "$1\Inter-SemiBold.ttf"
  WriteRegStr HKCU "Software\Microsoft\Windows NT\CurrentVersion\Fonts" "Inter Bold (TrueType)" "$1\Inter-Bold.ttf"

  ; Start Menu shortcut + settings shortcut
  CreateDirectory "$SMPROGRAMS\Golden Gate"
  CreateShortcut "$SMPROGRAMS\Golden Gate\Golden Gate.lnk" "$INSTDIR\${EXE}" "" "$INSTDIR\GoldenGate.ico"
  CreateShortcut "$SMPROGRAMS\Golden Gate\Golden Gate Settings.lnk" "$INSTDIR\${EXE}" "" "$INSTDIR\GoldenGate.ico"

  ; start at login. The app replaces this with a per-user sign-in task on first run
  ; (it starts sooner than Run-key programs); this stays only as the fallback.
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Run" "GoldenGate" '"$INSTDIR\${EXE}" --autostart'

  ; Add/Remove Programs
  !define UNINST "Software\Microsoft\Windows\CurrentVersion\Uninstall\GoldenGate"
  WriteRegStr HKCU "${UNINST}" "DisplayName" "Golden Gate"
  WriteRegStr HKCU "${UNINST}" "DisplayVersion" "${VER}"
  WriteRegStr HKCU "${UNINST}" "DisplayIcon" "$INSTDIR\GoldenGate.ico"
  WriteRegStr HKCU "${UNINST}" "UninstallString" '"$INSTDIR\uninstall.exe"'
  WriteRegStr HKCU "${UNINST}" "Publisher" "Golden Gate"
  WriteRegStr HKCU "${UNINST}" "InstallLocation" "$INSTDIR"
  WriteRegDWORD HKCU "${UNINST}" "NoModify" 1
  WriteRegDWORD HKCU "${UNINST}" "NoRepair" 1
  ${GetSize} "$INSTDIR" "/S=0K" $0 $1 $2
  IntFmt $0 "0x%08X" $0
  WriteRegDWORD HKCU "${UNINST}" "EstimatedSize" "$0"

  WriteUninstaller "$INSTDIR\uninstall.exe"

  ; start (again) straight away: an update replaces Golden Gate in a few seconds
  ; rather than leaving you without it. --autostart: no Settings window on an
  ; update (a first install still opens it to welcome you).
  Exec '"$INSTDIR\${EXE}" --autostart'
SectionEnd

Section "Uninstall"
  ; stop the app, then put Windows' look back before deleting files
  ExecWait '"$INSTDIR\${EXE}" --quit'
  nsExec::Exec 'taskkill /F /IM ${EXE}'
  Pop $0
  Sleep 500
  ExecWait '"$INSTDIR\${EXE}" --restore'
  Sleep 400

  ; remove the per-user Inter font registration (leave the file; harmless)
  DeleteRegValue HKCU "Software\Microsoft\Windows NT\CurrentVersion\Fonts" "Inter (TrueType)"
  DeleteRegValue HKCU "Software\Microsoft\Windows NT\CurrentVersion\Fonts" "Inter Medium (TrueType)"
  DeleteRegValue HKCU "Software\Microsoft\Windows NT\CurrentVersion\Fonts" "Inter SemiBold (TrueType)"
  DeleteRegValue HKCU "Software\Microsoft\Windows NT\CurrentVersion\Fonts" "Inter Bold (TrueType)"
  Delete "$LOCALAPPDATA\Microsoft\Windows\Fonts\Inter-Regular.ttf"
  Delete "$LOCALAPPDATA\Microsoft\Windows\Fonts\Inter-Medium.ttf"
  Delete "$LOCALAPPDATA\Microsoft\Windows\Fonts\Inter-SemiBold.ttf"
  Delete "$LOCALAPPDATA\Microsoft\Windows\Fonts\Inter-Bold.ttf"

  DeleteRegValue HKCU "Software\Microsoft\Windows\CurrentVersion\Run" "GoldenGate"
  nsExec::Exec 'schtasks /delete /tn GoldenGate /f'   ; the sign-in task
  Pop $0
  DeleteRegKey HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\GoldenGate"

  Delete "$SMPROGRAMS\Golden Gate\*.lnk"
  RMDir "$SMPROGRAMS\Golden Gate"

  RMDir /r "$INSTDIR\wallpapers"
  RMDir /r "$INSTDIR\cursors"
  RMDir /r "$INSTDIR\icons"
  RMDir /r "$INSTDIR\fonts"
  Delete "$INSTDIR\${EXE}"
  Delete "$INSTDIR\GoldenGate.ico"
  Delete "$INSTDIR\LICENSE.txt"
  Delete "$INSTDIR\LICENSE-GPL-3.0.txt"
  Delete "$INSTDIR\README.txt"
  Delete "$INSTDIR\uninstall.exe"
  RMDir "$INSTDIR"

  ; settings in %AppData%\GoldenGate are left so a reinstall remembers them;
  ; offer to remove them
  MessageBox MB_YESNO|MB_ICONQUESTION "Also remove your Golden Gate settings?" IDNO keep
  RMDir /r "$APPDATA\GoldenGate"
keep:
SectionEnd
