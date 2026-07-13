; Oxford — installateur Windows (NSIS). Fork Ardour, build MSYS2/MinGW.
; App relocatable : s'installe dans n'importe quel dossier. DLL mingw bundlées.

!define APP   "Oxford"
!define VER   "9.7.0"
!define PUB   "BourrinAudio"
!define EXE   "lib\ardour9\oxford-9.7.0.exe"
!define SRC   "D:\Oxford\install"
!define VST3SRC "C:\Program Files\Common Files\VST3"   ; plugins BourrinAudio déjà installés (source)
!define ICON  "D:\Oxford\Ardour-9.7.0\gtk2_ardour\icons\Oxford.ico"
!define UKEY  "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APP}"

Name "${APP} ${VER}"
OutFile "D:\Oxford\Oxford-${VER}-Setup.exe"
InstallDir "$PROGRAMFILES64\${APP}"
InstallDirRegKey HKLM "Software\${APP}" "InstallDir"
RequestExecutionLevel admin
SetCompressor /SOLID lzma
Unicode true
BrandingText "${PUB} — ${APP} ${VER}"

!include "MUI2.nsh"
!define MUI_ICON   "${ICON}"
!define MUI_UNICON "${ICON}"
!define MUI_ABORTWARNING
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!define MUI_FINISHPAGE_RUN "$INSTDIR\${EXE}"
!define MUI_FINISHPAGE_RUN_TEXT "Lancer ${APP}"
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "French"
!insertmacro MUI_LANGUAGE "English"

Section "Install"
  SetOutPath "$INSTDIR"
  ; purge de fichiers obsolètes d'installs précédentes (NSIS n'efface pas tout seul) :
  ; oxford-warm-*.colors = nom bugué (Ardour tronque au 1er tiret -> chargeait le mauvais thème)
  Delete "$INSTDIR\share\ardour9\themes\oxford-warm-oxford.colors"
  File /r "${SRC}\*.*"

  ; --- Plugins VST3 BourrinAudio : Sony DPS-D7 (delay) + Sony Reverb (IRs Sony incluses) ---
  SetOutPath "$COMMONFILES64\VST3\Sony Reverb.vst3"
  File /r "${VST3SRC}\Sony Reverb.vst3\*.*"
  SetOutPath "$COMMONFILES64\VST3\Sony DPS-D7.vst3"
  File /r "${VST3SRC}\Sony DPS-D7.vst3\*.*"
  SetOutPath "$INSTDIR"

  WriteRegStr HKLM "Software\${APP}" "InstallDir" "$INSTDIR"

  CreateDirectory "$SMPROGRAMS\${APP}"
  CreateShortCut "$SMPROGRAMS\${APP}\${APP}.lnk" "$INSTDIR\${EXE}" "" "$INSTDIR\${EXE}" 0
  CreateShortCut "$SMPROGRAMS\${APP}\Désinstaller ${APP}.lnk" "$INSTDIR\Uninstall.exe"
  CreateShortCut "$DESKTOP\${APP}.lnk" "$INSTDIR\${EXE}" "" "$INSTDIR\${EXE}" 0

  WriteUninstaller "$INSTDIR\Uninstall.exe"
  WriteRegStr HKLM "${UKEY}" "DisplayName"     "${APP} ${VER}"
  WriteRegStr HKLM "${UKEY}" "UninstallString" "$\"$INSTDIR\Uninstall.exe$\""
  WriteRegStr HKLM "${UKEY}" "DisplayIcon"     "$INSTDIR\${EXE}"
  WriteRegStr HKLM "${UKEY}" "Publisher"       "${PUB}"
  WriteRegStr HKLM "${UKEY}" "DisplayVersion"  "${VER}"
  WriteRegStr HKLM "${UKEY}" "InstallLocation" "$INSTDIR"
  WriteRegDWORD HKLM "${UKEY}" "NoModify" 1
  WriteRegDWORD HKLM "${UKEY}" "NoRepair" 1
SectionEnd

Section "Uninstall"
  Delete "$SMPROGRAMS\${APP}\${APP}.lnk"
  Delete "$SMPROGRAMS\${APP}\Désinstaller ${APP}.lnk"
  RMDir  "$SMPROGRAMS\${APP}"
  Delete "$DESKTOP\${APP}.lnk"
  RMDir /r "$COMMONFILES64\VST3\Sony Reverb.vst3"
  RMDir /r "$COMMONFILES64\VST3\Sony DPS-D7.vst3"
  RMDir /r "$COMMONFILES64\VST3\OxfordVerb.vst3"
  RMDir /r "$COMMONFILES64\VST3\DPS-D7.vst3"
  RMDir /r "$INSTDIR"
  DeleteRegKey HKLM "Software\${APP}"
  DeleteRegKey HKLM "${UKEY}"
SectionEnd
