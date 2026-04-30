#!/usr/bin/env bash

if [[ "$OSTYPE" == "darwin"* ]]; then
    VLINK_ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]:-$0}")" && pwd)"
else
    VLINK_ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE:-$0}")" && pwd)"
fi
VLINK_BIN_DIR="$VLINK_ROOT_DIR/bin"
DESKTOP_DIR="$VLINK_ROOT_DIR/desktop"

echo "Uninstall..."

mkdir -p ~/.local/share/applications/
mkdir -p ~/.local/share/icons/

[ -f ~/.local/share/icons/vlink-viewer.png ] && rm -f ~/.local/share/icons/vlink-viewer.png
[ -f ~/.local/share/applications/vlink-viewer.desktop ] && rm -f ~/.local/share/applications/vlink-viewer.desktop
[ -f ~/.local/share/icons/vlink-player.png ] && rm -f ~/.local/share/icons/vlink-player.png
[ -f ~/.local/share/applications/vlink-player.desktop ] && rm -f ~/.local/share/applications/vlink-player.desktop
[ -f ~/.local/share/icons/vlink-analyzer.png ] && rm -f ~/.local/share/icons/vlink-analyzer.png
[ -f ~/.local/share/applications/vlink-analyzer.desktop ] && rm -f ~/.local/share/applications/vlink-analyzer.desktop
[ -f ~/.local/share/icons/vlink-cmd.png ] && rm -f ~/.local/share/icons/vlink-cmd.png
[ -f ~/.local/share/applications/vlink-cmd.desktop ] && rm -f ~/.local/share/applications/vlink-cmd.desktop

if command -v xdg-user-dir &> /dev/null; then
    DESKTOP="$(xdg-user-dir DESKTOP)"
    if [ -f "$DESKTOP/vlink-viewer.desktop" ]; then
        gio remove "$DESKTOP/vlink-viewer.desktop" &> /dev/null
        [ -f "$DESKTOP/vlink-viewer.desktop" ] && rm -f "$DESKTOP/vlink-viewer.desktop"
    fi
    if [ -f "$DESKTOP/vlink-player.desktop" ]; then
        gio remove "$DESKTOP/vlink-player.desktop" &> /dev/null
        [ -f "$DESKTOP/vlink-player.desktop" ] && rm -f "$DESKTOP/vlink-player.desktop"
    fi
    if [ -f "$DESKTOP/vlink-analyzer.desktop" ]; then
        gio remove "$DESKTOP/vlink-analyzer.desktop" &> /dev/null
        [ -f "$DESKTOP/vlink-analyzer.desktop" ] && rm -f "$DESKTOP/vlink-analyzer.desktop"
    fi
    if [ -f "$DESKTOP/vlink-cmd.desktop" ]; then
        gio remove "$DESKTOP/vlink-cmd.desktop" &> /dev/null
        [ -f "$DESKTOP/vlink-cmd.desktop" ] && rm -f "$DESKTOP/vlink-cmd.desktop"
    fi
fi

# mime
xdg-mime uninstall --mode user "$DESKTOP_DIR/x-vlink-bag.xml" &> /dev/null
update-desktop-database ~/.local/share/applications/ &> /dev/null
update-mime-database ~/.local/share/mime/ &> /dev/null

# icon
function uninstall_theme_icon () {
    local _theme_name="$1"
    [ -f ~/.local/share/icons/"$_theme_name"/256x256/mimetypes/vlink-vdb.png ] && rm -f ~/.local/share/icons/"$_theme_name"/256x256/mimetypes/vlink-vdb.png
    [ -f ~/.local/share/icons/"$_theme_name"/256x256/mimetypes/vlink-vdbx.png ] && rm -f ~/.local/share/icons/"$_theme_name"/256x256/mimetypes/vlink-vdbx.png
    [ -f ~/.local/share/icons/"$_theme_name"/256x256/mimetypes/vlink-vcap.png ] && rm -f ~/.local/share/icons/"$_theme_name"/256x256/mimetypes/vlink-vcap.png
    [ -f ~/.local/share/icons/"$_theme_name"/256x256/mimetypes/vlink-vcapx.png ] && rm -f ~/.local/share/icons/"$_theme_name"/256x256/mimetypes/vlink-vcapx.png
    [ -f ~/.local/share/icons/"$_theme_name"/scalable/mimetypes/vlink-vdb.svg ] && rm -f ~/.local/share/icons/"$_theme_name"/scalable/mimetypes/vlink-vdb.svg
    [ -f ~/.local/share/icons/"$_theme_name"/scalable/mimetypes/vlink-vdbx.svg ] && rm -f ~/.local/share/icons/"$_theme_name"/scalable/mimetypes/vlink-vdbx.svg
    [ -f ~/.local/share/icons/"$_theme_name"/scalable/mimetypes/vlink-vcap.svg ] && rm -f ~/.local/share/icons/"$_theme_name"/scalable/mimetypes/vlink-vcap.svg
    [ -f ~/.local/share/icons/"$_theme_name"/scalable/mimetypes/vlink-vcapx.svg ] && rm -f ~/.local/share/icons/"$_theme_name"/scalable/mimetypes/vlink-vcapx.svg
    gtk-update-icon-cache ~/.local/share/icons/"$_theme_name"/ &> /dev/null
    gtk-update-icon-cache /usr/share/icons/"$_theme_name"/ &> /dev/null
}

THEME_NAME="$(gsettings get org.gnome.desktop.interface icon-theme | sed "s/'//g")"
uninstall_theme_icon hicolor
if [ -n "$THEME_NAME" ]; then
    uninstall_theme_icon "$THEME_NAME"
fi


# [ -f ~/.vlink_proto_dir ] && rm -f ~/.vlink_proto_dir
# [ -f ~/.vlink_fbs_dir ] && rm -f ~/.vlink_fbs_dir

echo "Done."
