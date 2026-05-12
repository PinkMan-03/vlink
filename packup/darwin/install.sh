#!/usr/bin/env bash

VLINK_ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE:-$0}")" && pwd)"

echo "Install..."

ln -sf "$VLINK_ROOT_DIR" "$HOME/Desktop/VLink Player"

# [ ! -f ~/.vlink_proto_dir ] && touch ~/.vlink_proto_dir
# [ ! -f ~/.vlink_fbs_dir ] && touch ~/.vlink_fbs_dir

echo "Done."
