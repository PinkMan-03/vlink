@echo off

net session >nul 2>&1
if %errorlevel% neq 0 (
    powershell -Command "Start-Process '%~f0' -Verb RunAs"
    exit /b
)

set "APP_NAME_VIEWER=VLink Viewer"
set "APP_NAME_PLAYER=VLink Player"
set "APP_NAME_ANALYZER=VLink Analyzer"
set "APP_NAME_CMD=VLink CMD"
set "APP_PATH_VIEWER=%~dp0bin\vlink-viewer.exe"
set "APP_PATH_PLAYER=%~dp0bin\vlink-player.exe"
set "APP_PATH_ANALYZER=%~dp0bin\vlink-analyzer.exe"
set "APP_PATH_CMD=%~dp0bin\run_cmd.bat"
set "APP_PATH_PROXY=%~dp0bin\vlink-proxy.exe"
set "DESKTOP_SHORTCUT_VIEWER=%USERPROFILE%\Desktop\%APP_NAME_VIEWER%.lnk"
set "DESKTOP_SHORTCUT_PLAYER=%USERPROFILE%\Desktop\%APP_NAME_PLAYER%.lnk"
set "DESKTOP_SHORTCUT_ANALYZER=%USERPROFILE%\Desktop\%APP_NAME_ANALYZER%.lnk"
set "DESKTOP_SHORTCUT_CMD=%USERPROFILE%\Desktop\%APP_NAME_CMD%.lnk"
set "START_MENU_FOLDER=%APPDATA%\Microsoft\Windows\Start Menu\Programs\%APP_NAME_VIEWER%"
set "START_MENU_SHORTCUT_VIEWER=%START_MENU_FOLDER%\%APP_NAME_VIEWER%.lnk"
set "START_MENU_SHORTCUT_PLAYER=%START_MENU_FOLDER%\%APP_NAME_PLAYER%.lnk"
set "START_MENU_SHORTCUT_ANALYZER=%START_MENU_FOLDER%\%APP_NAME_ANALYZER%.lnk"
set "START_MENU_SHORTCUT_CMD=%START_MENU_FOLDER%\%APP_NAME_CMD%.lnk"

echo Installing...

powershell -NoProfile -Command ^
  "$ws = New-Object -ComObject WScript.Shell; $s = $ws.CreateShortcut('%DESKTOP_SHORTCUT_VIEWER%'); $s.TargetPath = '%APP_PATH_VIEWER%'; $s.IconLocation = '%APP_PATH_VIEWER%,0'; $s.Save()"

powershell -NoProfile -Command ^
  "$ws = New-Object -ComObject WScript.Shell; $s = $ws.CreateShortcut('%DESKTOP_SHORTCUT_PLAYER%'); $s.TargetPath = '%APP_PATH_PLAYER%'; $s.IconLocation = '%APP_PATH_PLAYER%,0'; $s.Save()"

powershell -NoProfile -Command ^
  "$ws = New-Object -ComObject WScript.Shell; $s = $ws.CreateShortcut('%DESKTOP_SHORTCUT_ANALYZER%'); $s.TargetPath = '%APP_PATH_ANALYZER%'; $s.IconLocation = '%APP_PATH_ANALYZER%,0'; $s.Save()"

powershell -NoProfile -Command ^
  "$ws = New-Object -ComObject WScript.Shell; $s = $ws.CreateShortcut('%DESKTOP_SHORTCUT_CMD%'); $s.TargetPath = '%APP_PATH_CMD%'; $s.IconLocation = '%APP_PATH_PROXY%,0'; $s.WindowStyle = 7; $s.Save()"

mkdir "%START_MENU_FOLDER%" 2>nul

powershell -NoProfile -Command ^
  "$ws = New-Object -ComObject WScript.Shell; $s = $ws.CreateShortcut('%START_MENU_SHORTCUT_VIEWER%'); $s.TargetPath = '%APP_PATH_VIEWER%'; $s.IconLocation = '%APP_PATH_VIEWER%,0'; $s.Save()"

powershell -NoProfile -Command ^
  "$ws = New-Object -ComObject WScript.Shell; $s = $ws.CreateShortcut('%START_MENU_SHORTCUT_PLAYER%'); $s.TargetPath = '%APP_PATH_PLAYER%'; $s.IconLocation = '%APP_PATH_PLAYER%,0'; $s.Save()"

powershell -NoProfile -Command ^
  "$ws = New-Object -ComObject WScript.Shell; $s = $ws.CreateShortcut('%START_MENU_SHORTCUT_ANALYZER%'); $s.TargetPath = '%APP_PATH_ANALYZER%'; $s.IconLocation = '%APP_PATH_ANALYZER%,0'; $s.Save()"

powershell -NoProfile -Command ^
  "$ws = New-Object -ComObject WScript.Shell; $s = $ws.CreateShortcut('%START_MENU_SHORTCUT_CMD%'); $s.TargetPath = '%APP_PATH_CMD%'; $s.IconLocation = '%APP_PATH_PROXY%,0'; $s.WindowStyle = 7; $s.Save()"

assoc .vdb=vdbfile
assoc .vdbx=vdbxfile
assoc .vcap=vcapfile
assoc .vcapx=vcapxfile

ftype vdbfile="%APP_PATH_PLAYER%" "%%1"
ftype vdbxfile="%APP_PATH_PLAYER%" "%%1"
ftype vcapfile="%APP_PATH_PLAYER%" "%%1"
ftype vcapxfile="%APP_PATH_PLAYER%" "%%1"

reg add "HKEY_CLASSES_ROOT\vdbfile\DefaultIcon" /ve /t REG_SZ /d "%APP_PATH_PLAYER%,0" /f
reg add "HKEY_CLASSES_ROOT\vdbxfile\DefaultIcon" /ve /t REG_SZ /d "%APP_PATH_PLAYER%,0" /f
reg add "HKEY_CLASSES_ROOT\vcapfile\DefaultIcon" /ve /t REG_SZ /d "%APP_PATH_PLAYER%,0" /f
reg add "HKEY_CLASSES_ROOT\vcapxfile\DefaultIcon" /ve /t REG_SZ /d "%APP_PATH_PLAYER%,0" /f

:: if not exist "%USERPROFILE%\.vlink_proto_dir" echo. > "%USERPROFILE%\.vlink_proto_dir"
:: if not exist "%USERPROFILE%\.vlink_fbs_dir" echo. > "%USERPROFILE%\.vlink_fbs_dir"

echo Done.

exit /b 0
