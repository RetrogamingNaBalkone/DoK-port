#!/usr/bin/env bash
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../../.." && pwd)
api=${1:-OPENGL}
case "$api" in OPENGL|GLES2) ;; *) echo 'Usage: build-linux.sh [OPENGL|GLES2]' >&2; exit 2;; esac
case "$(uname -m)" in x86_64) platform=linux-x86_64;; aarch64) platform=portmaster-aarch64;; *) echo 'Unsupported architecture' >&2; exit 2;; esac
build="$root/build/sdl2/$platform-${api,,}"
cmake -S "$root/ports/sdl2" -B "$build" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DDOK_GRAPHICS_API="$api" -DBUILD_TESTING=ON
cmake --build "$build" --parallel "${DOK_BUILD_JOBS:-2}"
if [ -z "${DISPLAY:-}" ] && command -v xvfb-run >/dev/null 2>&1; then
    SDL_AUDIODRIVER=dummy xvfb-run -a ctest --test-dir "$build" --output-on-failure
else
    SDL_AUDIODRIVER=dummy SDL_VIDEODRIVER="${SDL_VIDEODRIVER:-dummy}" ctest --test-dir "$build" --output-on-failure
fi
