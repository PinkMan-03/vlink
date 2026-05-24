@echo off
setlocal enableextensions

net session >nul 2>&1
if %errorlevel% equ 0 goto :is_admin
powershell -NoProfile -Command "$p = Start-Process -FilePath '%~f0' -Verb RunAs -Wait -PassThru -ErrorAction Stop; exit $p.ExitCode" 2>nul
exit /b %errorlevel%
:is_admin

set "APP_NAME_VIEWER=VLink Viewer"
set "APP_NAME_PLAYER=VLink Player"
set "APP_NAME_ANALYZER=VLink Analyzer"
set "APP_NAME_CMD=VLink CMD"
set "TERMINAL_DIR=%~dp0bin\terminal"

if not defined PUBLIC set "PUBLIC=%SystemDrive%\Users\Public"
if not defined ProgramData set "ProgramData=%SystemDrive%\ProgramData"

set "DESKTOP_SHORTCUT_VIEWER=%PUBLIC%\Desktop\%APP_NAME_VIEWER%.lnk"
set "DESKTOP_SHORTCUT_PLAYER=%PUBLIC%\Desktop\%APP_NAME_PLAYER%.lnk"
set "DESKTOP_SHORTCUT_ANALYZER=%PUBLIC%\Desktop\%APP_NAME_ANALYZER%.lnk"
set "DESKTOP_SHORTCUT_CMD=%PUBLIC%\Desktop\%APP_NAME_CMD%.lnk"
set "START_MENU_FOLDER=%ProgramData%\Microsoft\Windows\Start Menu\Programs\%APP_NAME_VIEWER%"

set "LEGACY_DESKTOP_VIEWER=%USERPROFILE%\Desktop\%APP_NAME_VIEWER%.lnk"
set "LEGACY_DESKTOP_PLAYER=%USERPROFILE%\Desktop\%APP_NAME_PLAYER%.lnk"
set "LEGACY_DESKTOP_ANALYZER=%USERPROFILE%\Desktop\%APP_NAME_ANALYZER%.lnk"
set "LEGACY_DESKTOP_CMD=%USERPROFILE%\Desktop\%APP_NAME_CMD%.lnk"
set "LEGACY_START_MENU_FOLDER=%APPDATA%\Microsoft\Windows\Start Menu\Programs\%APP_NAME_VIEWER%"

echo Uninstall...

if exist "%DESKTOP_SHORTCUT_VIEWER%"   del /f /q "%DESKTOP_SHORTCUT_VIEWER%"   >nul 2>&1
if exist "%DESKTOP_SHORTCUT_PLAYER%"   del /f /q "%DESKTOP_SHORTCUT_PLAYER%"   >nul 2>&1
if exist "%DESKTOP_SHORTCUT_ANALYZER%" del /f /q "%DESKTOP_SHORTCUT_ANALYZER%" >nul 2>&1
if exist "%DESKTOP_SHORTCUT_CMD%"      del /f /q "%DESKTOP_SHORTCUT_CMD%"      >nul 2>&1
if exist "%START_MENU_FOLDER%" rmdir /s /q "%START_MENU_FOLDER%" >nul 2>&1

if exist "%LEGACY_DESKTOP_VIEWER%"   del /f /q "%LEGACY_DESKTOP_VIEWER%"   >nul 2>&1
if exist "%LEGACY_DESKTOP_PLAYER%"   del /f /q "%LEGACY_DESKTOP_PLAYER%"   >nul 2>&1
if exist "%LEGACY_DESKTOP_ANALYZER%" del /f /q "%LEGACY_DESKTOP_ANALYZER%" >nul 2>&1
if exist "%LEGACY_DESKTOP_CMD%"      del /f /q "%LEGACY_DESKTOP_CMD%"      >nul 2>&1
if exist "%LEGACY_START_MENU_FOLDER%" rmdir /s /q "%LEGACY_START_MENU_FOLDER%" >nul 2>&1

if exist "%TERMINAL_DIR%" rmdir /s /q "%TERMINAL_DIR%" >nul 2>&1

assoc .vdb=   >nul 2>&1
assoc .vdbx=  >nul 2>&1
assoc .vcap=  >nul 2>&1
assoc .vcapx= >nul 2>&1

reg delete "HKEY_CLASSES_ROOT\vdbfile"    /f >nul 2>&1
reg delete "HKEY_CLASSES_ROOT\vdbxfile"   /f >nul 2>&1
reg delete "HKEY_CLASSES_ROOT\vcapfile"   /f >nul 2>&1
reg delete "HKEY_CLASSES_ROOT\vcapxfile"  /f >nul 2>&1

reg delete "HKCU\Software\Microsoft\Windows\CurrentVersion\Explorer\FileExts\.vdb"   /f >nul 2>&1
reg delete "HKCU\Software\Microsoft\Windows\CurrentVersion\Explorer\FileExts\.vdbx"  /f >nul 2>&1
reg delete "HKCU\Software\Microsoft\Windows\CurrentVersion\Explorer\FileExts\.vcap"  /f >nul 2>&1
reg delete "HKCU\Software\Microsoft\Windows\CurrentVersion\Explorer\FileExts\.vcapx" /f >nul 2>&1

ie4uinit.exe -show           >nul 2>&1
ie4uinit.exe -ClearIconCache >nul 2>&1

echo Done.
exit /b 0
