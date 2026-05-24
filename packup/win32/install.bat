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
set "APP_PATH_VIEWER=%~dp0bin\vlink-viewer.exe"
set "APP_PATH_PLAYER=%~dp0bin\vlink-player.exe"
set "APP_PATH_ANALYZER=%~dp0bin\vlink-analyzer.exe"
set "APP_PATH_CMD=%~dp0bin\run_cmd.bat"
set "APP_PATH_PROXY=%~dp0bin\vlink-proxy.exe"

if not defined PUBLIC set "PUBLIC=%SystemDrive%\Users\Public"
if not defined ProgramData set "ProgramData=%SystemDrive%\ProgramData"

set "DESKTOP_DIR=%PUBLIC%\Desktop"
set "DESKTOP_SHORTCUT_VIEWER=%DESKTOP_DIR%\%APP_NAME_VIEWER%.lnk"
set "DESKTOP_SHORTCUT_PLAYER=%DESKTOP_DIR%\%APP_NAME_PLAYER%.lnk"
set "DESKTOP_SHORTCUT_ANALYZER=%DESKTOP_DIR%\%APP_NAME_ANALYZER%.lnk"
set "DESKTOP_SHORTCUT_CMD=%DESKTOP_DIR%\%APP_NAME_CMD%.lnk"
set "START_MENU_FOLDER=%ProgramData%\Microsoft\Windows\Start Menu\Programs\%APP_NAME_VIEWER%"
set "START_MENU_SHORTCUT_VIEWER=%START_MENU_FOLDER%\%APP_NAME_VIEWER%.lnk"
set "START_MENU_SHORTCUT_PLAYER=%START_MENU_FOLDER%\%APP_NAME_PLAYER%.lnk"
set "START_MENU_SHORTCUT_ANALYZER=%START_MENU_FOLDER%\%APP_NAME_ANALYZER%.lnk"
set "START_MENU_SHORTCUT_CMD=%START_MENU_FOLDER%\%APP_NAME_CMD%.lnk"

echo Installing...

if not exist "%DESKTOP_DIR%" mkdir "%DESKTOP_DIR%" >nul 2>&1
if not exist "%START_MENU_FOLDER%" mkdir "%START_MENU_FOLDER%" >nul 2>&1

powershell -NoProfile -Command ^
  "$ws = New-Object -ComObject WScript.Shell; $s = $ws.CreateShortcut('%DESKTOP_SHORTCUT_VIEWER%'); $s.TargetPath = '%APP_PATH_VIEWER%'; $s.IconLocation = '%APP_PATH_VIEWER%,0'; $s.Save()" 2>nul

powershell -NoProfile -Command ^
  "$ws = New-Object -ComObject WScript.Shell; $s = $ws.CreateShortcut('%DESKTOP_SHORTCUT_PLAYER%'); $s.TargetPath = '%APP_PATH_PLAYER%'; $s.IconLocation = '%APP_PATH_PLAYER%,0'; $s.Save()" 2>nul

powershell -NoProfile -Command ^
  "$ws = New-Object -ComObject WScript.Shell; $s = $ws.CreateShortcut('%DESKTOP_SHORTCUT_ANALYZER%'); $s.TargetPath = '%APP_PATH_ANALYZER%'; $s.IconLocation = '%APP_PATH_ANALYZER%,0'; $s.Save()" 2>nul

powershell -NoProfile -Command ^
  "$ws = New-Object -ComObject WScript.Shell; $s = $ws.CreateShortcut('%DESKTOP_SHORTCUT_CMD%'); $s.TargetPath = '%APP_PATH_CMD%'; $s.IconLocation = '%APP_PATH_PROXY%,0'; $s.WindowStyle = 7; $s.Save()" 2>nul

powershell -NoProfile -Command ^
  "$ws = New-Object -ComObject WScript.Shell; $s = $ws.CreateShortcut('%START_MENU_SHORTCUT_VIEWER%'); $s.TargetPath = '%APP_PATH_VIEWER%'; $s.IconLocation = '%APP_PATH_VIEWER%,0'; $s.Save()" 2>nul

powershell -NoProfile -Command ^
  "$ws = New-Object -ComObject WScript.Shell; $s = $ws.CreateShortcut('%START_MENU_SHORTCUT_PLAYER%'); $s.TargetPath = '%APP_PATH_PLAYER%'; $s.IconLocation = '%APP_PATH_PLAYER%,0'; $s.Save()" 2>nul

powershell -NoProfile -Command ^
  "$ws = New-Object -ComObject WScript.Shell; $s = $ws.CreateShortcut('%START_MENU_SHORTCUT_ANALYZER%'); $s.TargetPath = '%APP_PATH_ANALYZER%'; $s.IconLocation = '%APP_PATH_ANALYZER%,0'; $s.Save()" 2>nul

powershell -NoProfile -Command ^
  "$ws = New-Object -ComObject WScript.Shell; $s = $ws.CreateShortcut('%START_MENU_SHORTCUT_CMD%'); $s.TargetPath = '%APP_PATH_CMD%'; $s.IconLocation = '%APP_PATH_PROXY%,0'; $s.WindowStyle = 7; $s.Save()" 2>nul

assoc .vdb=vdbfile       >nul 2>&1
assoc .vdbx=vdbxfile     >nul 2>&1
assoc .vcap=vcapfile     >nul 2>&1
assoc .vcapx=vcapxfile   >nul 2>&1

ftype vdbfile="%APP_PATH_PLAYER%" "%%1"    >nul 2>&1
ftype vdbxfile="%APP_PATH_PLAYER%" "%%1"   >nul 2>&1
ftype vcapfile="%APP_PATH_PLAYER%" "%%1"   >nul 2>&1
ftype vcapxfile="%APP_PATH_PLAYER%" "%%1"  >nul 2>&1

reg add "HKEY_CLASSES_ROOT\vdbfile\DefaultIcon"   /ve /t REG_SZ /d "%APP_PATH_PLAYER%,0" /f >nul 2>&1
reg add "HKEY_CLASSES_ROOT\vdbxfile\DefaultIcon"  /ve /t REG_SZ /d "%APP_PATH_PLAYER%,0" /f >nul 2>&1
reg add "HKEY_CLASSES_ROOT\vcapfile\DefaultIcon"  /ve /t REG_SZ /d "%APP_PATH_PLAYER%,0" /f >nul 2>&1
reg add "HKEY_CLASSES_ROOT\vcapxfile\DefaultIcon" /ve /t REG_SZ /d "%APP_PATH_PLAYER%,0" /f >nul 2>&1

ie4uinit.exe -show           >nul 2>&1
ie4uinit.exe -ClearIconCache >nul 2>&1

echo Done.
exit /b 0
