# Install script for the NSIS installer
# (see http://nsis.sourceforge.net/)

!include "MUI2.nsh"

!define APPNAME      "Realm Works Output Converter"
!define COMPANYNAME  "Amusing Time"
!define DESCRIPTION  "A tool to convert a Realm Works Output file into XHTML"

#define name of installer
Name "${COMPANYNAME} - ${APPNAME}"
OutFile "RWoutput-installer.exe"

# define installation directory
InstallDir "$PROGRAMFILES\${COMPANYNAME}\${APPNAME}"

!insertmacro MUI_PAGE_LICENSE "..\RWoutput\LICENSE"
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "English"

!define ARP "Software\Microsoft\Windows\CurrentVersion\Uninstall\${COMPANYNAME} ${APPNAME}"
!include "FileFunc.nsh"

# start default section
Section
   # set the installation directory as the destination for the following actions
   SetOutPath $INSTDIR

   # The files to be installed
   File /r install\*.*

   # create the uninstaller
   WriteUninstaller "$INSTDIR\uninstall.exe"

   # Get the version of the file we just installed
   var /GLOBAL VERSION
   ${GetFileVersion} "$INSTDIR\RWout.exe" $VERSION

   # Start Menu
   CreateDirectory "$SMPROGRAMS\${COMPANYNAME}"
   CreateShortCut  "$SMPROGRAMS\${COMPANYNAME}\${APPNAME}.lnk" "$INSTDIR\RWout.exe"

   # create a shortcut named "new shortcut" in the start menu programs directory
   # point the new shortcut at the program uninstaller
   CreateShortCut "$SMPROGRAMS\Uninstall Realm Works Output Converter.lnk" "$INSTDIR\uninstall.exe"

   # Put into the "Add/Remove" menu
   WriteRegStr HKLM "${ARP}" "DisplayName"     "${APPNAME}"
   WriteRegStr HKLM "${ARP}" "UninstallString" "$\"$INSTDIR\uninstall.exe$\""
   WriteRegStr HKLM "${ARP}" "QuietUninstallString" "$\"$INSTDIR\uninstall.exe$\" /S"
   WriteRegStr HKLM "${ARP}" "InstallLocation" "$\"$INSTDIR$\""
   WriteRegStr HKLM "${ARP}" "Publisher"       "Amusing Time"
   WriteRegStr HKLM "${ARP}" "DisplayVersion"  "$\"$VERSION$\""
   # There is no option for modifying or repairing the install
   WriteRegDWORD HKLM "${ARP}" "NoModify" 1
   WriteRegDWORD HKLM "${ARP}" "NoRepair" 1

   # Set estimated size
   ${GetSize} "$INSTDIR" "/S=0K" $0 $1 $2
   IntFmt $0 "0x%08X" $0
   WriteRegDWORD HKLM "${ARP}" "EstimatedSize" "$0"

SectionEnd

# Need a section for upgrading


# uninstaller section start
Section "uninstall"

    # Remove Start Menu launcher
    Delete "$SMPROGRAMS\${COMPANYNAME}\${APPNAME}.lnk"
    # Try to remove the Start Menu folder - this will only happen if it is empty
    rmDir "$SMPROGRAMS\${COMPANYNAME}"

    # Remove files
    delete $INSTDIR\*.*

    # first, delete the uninstaller
    Delete "$INSTDIR\uninstall.exe"

    # Remove the install directory (and all files)
    rmDir /r $INSTDIR

    # Remove uninstaller information from the registry
    DeleteRegKey HKLM "${ARP}"

# uninstaller section end
SectionEnd
