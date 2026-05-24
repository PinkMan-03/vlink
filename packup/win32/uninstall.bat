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
set "TERMINAL_DIR=%~dp0bin\terminal"
set "DESKTOP_SHORTCUT_VIEWER=%USERPROFILE%\Desktop\%APP_NAME_VIEWER%.lnk"
set "DESKTOP_SHORTCUT_PLAYER=%USERPROFILE%\Desktop\%APP_NAME_PLAYER%.lnk"
set "DESKTOP_SHORTCUT_ANALYZER=%USERPROFILE%\Desktop\%APP_NAME_ANALYZER%.lnk"
set "DESKTOP_SHORTCUT_CMD=%USERPROFILE%\Desktop\%APP_NAME_CMD%.lnk"
set "START_MENU_FOLDER=%APPDATA%\Microsoft\Windows\Start Menu\Programs\%APP_NAME_VIEWER%"

echo Uninstall...

if exist "%DESKTOP_SHORTCUT_VIEWER%" del "%DESKTOP_SHORTCUT_VIEWER%" /q
if exist "%DESKTOP_SHORTCUT_PLAYER%" del "%DESKTOP_SHORTCUT_PLAYER%" /q
if exist "%DESKTOP_SHORTCUT_ANALYZER%" del "%DESKTOP_SHORTCUT_ANALYZER%" /q
if exist "%DESKTOP_SHORTCUT_CMD%" del "%DESKTOP_SHORTCUT_CMD%" /q

if exist "%START_MENU_FOLDER%" rmdir "%START_MENU_FOLDER%" /s /q

powershell -Command "if (Test-Path '%TERMINAL_DIR%') { Remove-Item '%TERMINAL_DIR%\*' -Recurse -Force }"
if exist "%TERMINAL_DIR%" rmdir "%TERMINAL_DIR%" /s /q

assoc .vdb=
assoc .vdbx=
assoc .vcap=
assoc .vcapx=

reg delete "HKEY_CLASSES_ROOT\vdbfile" /f
reg delete "HKEY_CLASSES_ROOT\vdbxfile" /f
reg delete "HKEY_CLASSES_ROOT\vcapfile" /f
reg delete "HKEY_CLASSES_ROOT\vcapxfile" /f

:: if exist "%USERPROFILE%\.vlink_proto_dir" del "%USERPROFILE%\.vlink_proto_dir" /q
:: if exist "%USERPROFILE%\.vlink_fbs_dir" del "%USERPROFILE%\.vlink_fbs_dir" /q

echo Done.
