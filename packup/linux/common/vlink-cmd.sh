#!/bin/bash

if [[ "$OSTYPE" == "darwin"* ]]; then
    WORK_DIR="$(cd "$(dirname "${BASH_SOURCE[0]:-$0}")" && pwd)"
else
    WORK_DIR="$(cd "$(dirname "${BASH_SOURCE:-$0}")" && pwd)"
fi

bash --init-file <(echo "cd \"$(pwd)\" && source \"$WORK_DIR/setup_runtime.sh\"")
