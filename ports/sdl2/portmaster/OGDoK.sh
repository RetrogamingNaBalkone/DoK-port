#!/bin/bash
# PortMaster native SDL controller support; gptokeyb handles exit only.
XDG_DATA_HOME=${XDG_DATA_HOME:-$HOME/.local/share}
if [ -d /opt/system/Tools/PortMaster ]; then
    controlfolder=/opt/system/Tools/PortMaster
elif [ -d /opt/tools/PortMaster ]; then
    controlfolder=/opt/tools/PortMaster
elif [ -d "$XDG_DATA_HOME/PortMaster" ]; then
    controlfolder="$XDG_DATA_HOME/PortMaster"
else
    controlfolder=/roms/ports/PortMaster
fi
if ! source "$controlfolder/control.txt"; then exit 1; fi
if [ -f "$controlfolder/mod_${CFW_NAME}.txt" ]; then source "$controlfolder/mod_${CFW_NAME}.txt"; fi
get_controls
GAMEDIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)/ogdok"
cd "$GAMEDIR" || exit 1
mkdir -p user || exit 1
exec > >(tee user/launcher.log) 2>&1
if [ "${DEVICE_ARCH:-$(uname -m)}" != aarch64 ]; then echo 'OGDoK requires aarch64'; exit 1; fi
export SDL_GAMECONTROLLERCONFIG="${sdl_controllerconfig:-${SDL_GAMECONTROLLERCONFIG:-}}"
export LD_LIBRARY_PATH="$GAMEDIR/libs.aarch64${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export DOK_PORTMASTER=1
cleanup() { pm_finish; }
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
# GPTOKEYB may contain the launcher and its arguments (PortMaster contract).
$GPTOKEYB OGDoK &
pm_platform_helper "$GAMEDIR/OGDoK"
./OGDoK --user-dir "$GAMEDIR/user" "$@"
result=$?
exit "$result"
