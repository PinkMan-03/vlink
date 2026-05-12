#!/usr/bin/env bash

VLINK_ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE:-$0}")" && pwd)"

echo "Uninstall..."

[ -L "$HOME/Desktop/VLink Player" ] && rm -f "$HOME/Desktop/VLink Player"

# [ -f ~/.vlink_proto_dir ] && rm -f ~/.vlink_proto_dir
# [ -f ~/.vlink_fbs_dir ] && rm -f ~/.vlink_fbs_dir

echo "Done."
