#!/usr/bin/env bash

VLINK_ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE:-$0}")" && pwd)"
VLINK_BIN_DIR="$VLINK_ROOT_DIR/bin"
DESKTOP_DIR="$VLINK_ROOT_DIR/desktop"

echo "Install..."

_create_app() {
    local _name="$1"
    local _id="$2"
    local _script="$3"
    local _icon="$4"
    local _app="$HOME/Desktop/${_name}.app"

    rm -rf "$_app" 2>/dev/null
    mkdir -p "$_app/Contents/MacOS" "$_app/Contents/Resources" 2>/dev/null || return 0

    cat > "$_app/Contents/Info.plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleName</key>
    <string>${_name}</string>
    <key>CFBundleExecutable</key>
    <string>launcher</string>
    <key>CFBundleIconFile</key>
    <string>icon.png</string>
    <key>CFBundleIdentifier</key>
    <string>com.vlink.${_id}</string>
    <key>CFBundleVersion</key>
    <string>1.0</string>
    <key>CFBundleShortVersionString</key>
    <string>1.0</string>
    <key>CFBundlePackageType</key>
    <string>APPL</string>
    <key>LSMinimumSystemVersion</key>
    <string>10.13</string>
</dict>
</plist>
EOF

    cat > "$_app/Contents/MacOS/launcher" <<EOF
#!/usr/bin/env bash
exec "${VLINK_BIN_DIR}/${_script}" "\$@"
EOF
    chmod +x "$_app/Contents/MacOS/launcher" 2>/dev/null

    if [ -f "$DESKTOP_DIR/${_icon}" ]; then
        cp -f "$DESKTOP_DIR/${_icon}" "$_app/Contents/Resources/icon.png" 2>/dev/null
    fi

    touch "$_app" 2>/dev/null
}

if [ -d "$HOME/Desktop" ]; then
    _create_app "VLink Viewer"   viewer   run_viewer.sh   vlink-viewer.png
    _create_app "VLink Player"   player   run_player.sh   vlink-player.png
    _create_app "VLink Analyzer" analyzer run_analyzer.sh vlink-analyzer.png
    _create_app "VLink CMD"      cmd      run_cmd.sh      vlink-cmd.png

    if [ -L "$HOME/Desktop/VLink Player" ]; then
        rm -f "$HOME/Desktop/VLink Player" 2>/dev/null
    fi
fi

echo "Done."
exit 0
