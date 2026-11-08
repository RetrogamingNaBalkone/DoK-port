#!/usr/bin/env bash
set -euo pipefail
if [ "$#" != 2 ]; then echo 'Usage: check-display-modes.sh EXECUTABLE NEW_OUTPUT_ROOT' >&2; exit 2; fi
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../../.." && pwd)
for mode in 1024x768 1920x1080; do
    xvfb-run -a -s "-screen 0 ${mode}x24" python3 "$root/ports/sdl2/tools/check_display_defaults.py" \
        --exe "$1" --out "$2/$mode" --expected "$mode"
done
