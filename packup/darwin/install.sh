#!/usr/bin/env bash

if [[ "$OSTYPE" == "darwin"* ]]; then
    VLINK_ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]:-$0}")" && pwd)"
else
    VLINK_ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE:-$0}")" && pwd)"
fi

echo "Install..."

ln -sf "$VLINK_ROOT_DIR" "$HOME/Desktop/VLink Player"

# [ ! -f ~/.vlink_proto_dir ] && touch ~/.vlink_proto_dir
# [ ! -f ~/.vlink_fbs_dir ] && touch ~/.vlink_fbs_dir

echo "Done."
