#!/usr/bin/env bash

if [[ "$OSTYPE" == "darwin"* ]]; then
    VLINK_ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]:-$0}")" && pwd)"
else
    VLINK_ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE:-$0}")" && pwd)"
fi
VLINK_BIN_DIR="$VLINK_ROOT_DIR/bin"
DESKTOP_DIR="$VLINK_ROOT_DIR/desktop"

echo "Install..."

mkdir -p ~/.local/share/applications/
mkdir -p ~/.local/share/icons/

cp -f "$DESKTOP_DIR/vlink-viewer.png"        ~/.local/share/icons/
cp -f "$DESKTOP_DIR/vlink-viewer.desktop"    ~/.local/share/applications/
cp -f "$DESKTOP_DIR/vlink-player.png"        ~/.local/share/icons/
cp -f "$DESKTOP_DIR/vlink-player.desktop"    ~/.local/share/applications/
cp -f "$DESKTOP_DIR/vlink-analyzer.png"      ~/.local/share/icons/
cp -f "$DESKTOP_DIR/vlink-analyzer.desktop"  ~/.local/share/applications/
cp -f "$DESKTOP_DIR/vlink-cmd.png"           ~/.local/share/icons/
cp -f "$DESKTOP_DIR/vlink-cmd.desktop"       ~/.local/share/applications/

sed -i "s#Icon=vlink-viewer.png#Icon=${DESKTOP_DIR}/vlink-viewer.png#g" ~/.local/share/applications/vlink-viewer.desktop
sed -i "s#Exec=run_viewer.sh#Exec=${VLINK_BIN_DIR}/run_viewer.sh#g" ~/.local/share/applications/vlink-viewer.desktop
sed -i "s#Icon=vlink-player.png#Icon=${DESKTOP_DIR}/vlink-player.png#g" ~/.local/share/applications/vlink-player.desktop
sed -i "s#Exec=run_player.sh#Exec=${VLINK_BIN_DIR}/run_player.sh#g" ~/.local/share/applications/vlink-player.desktop
sed -i "s#Icon=vlink-analyzer.png#Icon=${DESKTOP_DIR}/vlink-analyzer.png#g" ~/.local/share/applications/vlink-analyzer.desktop
sed -i "s#Exec=run_analyzer.sh#Exec=${VLINK_BIN_DIR}/run_analyzer.sh#g" ~/.local/share/applications/vlink-analyzer.desktop
sed -i "s#Icon=vlink-cmd.png#Icon=${DESKTOP_DIR}/vlink-cmd.png#g" ~/.local/share/applications/vlink-cmd.desktop
sed -i "s#Exec=vlink-cmd.sh#Exec=${VLINK_BIN_DIR}/vlink-cmd.sh#g" ~/.local/share/applications/vlink-cmd.desktop

chmod a+x ~/.local/share/applications/vlink-viewer.desktop
chmod a+x ~/.local/share/applications/vlink-player.desktop
chmod a+x ~/.local/share/applications/vlink-analyzer.desktop
chmod a+x ~/.local/share/applications/vlink-cmd.desktop

if command -v xdg-user-dir &> /dev/null; then
    DESKTOP="$(xdg-user-dir DESKTOP)"
    cp -f ~/.local/share/applications/vlink-viewer.desktop "$DESKTOP"
    cp -f ~/.local/share/applications/vlink-player.desktop "$DESKTOP"
    cp -f ~/.local/share/applications/vlink-analyzer.desktop "$DESKTOP"
    cp -f ~/.local/share/applications/vlink-cmd.desktop "$DESKTOP"
    gio set "$DESKTOP/vlink-viewer.desktop" metadata::trusted true &> /dev/null
    gio set "$DESKTOP/vlink-player.desktop" metadata::trusted true &> /dev/null
    gio set "$DESKTOP/vlink-analyzer.desktop" metadata::trusted true &> /dev/null
    gio set "$DESKTOP/vlink-cmd.desktop" metadata::trusted true &> /dev/null
fi

if grep -q "Ubuntu" /etc/os-release 2>/dev/null || grep -q "Ubuntu" /etc/issue 2>/dev/null; then
    _missing=()
    for _pkg in libvdpau-dev libva-drm2 libva-x11-2; do
        dpkg-query -W -f='${Status}' "$_pkg" 2>/dev/null | grep -q "installed" || _missing+=("$_pkg")
    done

    if [ ${#_missing[@]} -gt 0 ]; then
        echo "The following packages are required: ${_missing[*]}"
        echo ""
        if command -v sudo &>/dev/null; then
            if sudo -n true 2>/dev/null; then
                sudo apt-get install -y "${_missing[@]}"
            else
                echo "Requesting elevated privileges..."
                if command -v pkexec &>/dev/null; then
                    pkexec env DISPLAY="$DISPLAY" XAUTHORITY="$XAUTHORITY" \
                        apt-get install -y "${_missing[@]}"
                else
                    sudo apt-get install -y "${_missing[@]}"
                fi
            fi
        elif command -v pkexec &>/dev/null; then
            pkexec env DISPLAY="$DISPLAY" XAUTHORITY="$XAUTHORITY" \
                apt-get install -y "${_missing[@]}"
        else
            echo "No sudo/pkexec available. Please install manually:"
            echo "  sudo apt-get install -y ${_missing[*]}"
        fi
    fi
fi

xdg-mime install --mode user "$DESKTOP_DIR/x-vlink-bag.xml" &> /dev/null

xdg-mime default vlink-player.desktop application/x-vlink-vdb
xdg-mime default vlink-player.desktop application/x-vlink-vdbx
xdg-mime default vlink-player.desktop application/x-vlink-vcap
xdg-mime default vlink-player.desktop application/x-vlink-vcapx
update-desktop-database ~/.local/share/applications/ &> /dev/null
update-mime-database ~/.local/share/mime/ &> /dev/null

function install_theme_icon () {
    local _theme_name="$1"
    mkdir -p ~/.local/share/icons/"$_theme_name"/256x256/mimetypes/
    mkdir -p ~/.local/share/icons/"$_theme_name"/scalable/mimetypes/
    cp -f "$DESKTOP_DIR/vlink-player.png" ~/.local/share/icons/"$_theme_name"/256x256/mimetypes/vlink-vdb.png
    cp -f "$DESKTOP_DIR/vlink-player.png" ~/.local/share/icons/"$_theme_name"/256x256/mimetypes/vlink-vdbx.png
    cp -f "$DESKTOP_DIR/vlink-player.png" ~/.local/share/icons/"$_theme_name"/256x256/mimetypes/vlink-vcap.png
    cp -f "$DESKTOP_DIR/vlink-player.png" ~/.local/share/icons/"$_theme_name"/256x256/mimetypes/vlink-vcapx.png
    cp -f "$DESKTOP_DIR/vlink-player.svg" ~/.local/share/icons/"$_theme_name"/scalable/mimetypes/vlink-vdb.svg 2>/dev/null
    cp -f "$DESKTOP_DIR/vlink-player.svg" ~/.local/share/icons/"$_theme_name"/scalable/mimetypes/vlink-vdbx.svg 2>/dev/null
    cp -f "$DESKTOP_DIR/vlink-player.svg" ~/.local/share/icons/"$_theme_name"/scalable/mimetypes/vlink-vcap.svg 2>/dev/null
    cp -f "$DESKTOP_DIR/vlink-player.svg" ~/.local/share/icons/"$_theme_name"/scalable/mimetypes/vlink-vcapx.svg 2>/dev/null
    gtk-update-icon-cache ~/.local/share/icons/"$_theme_name"/ &> /dev/null
    gtk-update-icon-cache /usr/share/icons/"$_theme_name"/ &> /dev/null
}

THEME_NAME="$(gsettings get org.gnome.desktop.interface icon-theme | sed "s/'//g")"
install_theme_icon hicolor
if [ -n "$THEME_NAME" ]; then
    install_theme_icon "$THEME_NAME"
fi

# [ ! -f ~/.vlink_proto_dir ] && touch ~/.vlink_proto_dir
# [ ! -f ~/.vlink_fbs_dir ] && touch ~/.vlink_fbs_dir

echo "Done."
