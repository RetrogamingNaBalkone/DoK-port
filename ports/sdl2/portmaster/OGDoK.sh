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
if [[ -f user/trace-frames && -z "${DOK_FRAME_TIMINGS:-}" ]]; then
    if trace_dir=$(mktemp -d "$GAMEDIR/user/frames.XXXXXX"); then
        export DOK_FRAME_TIMINGS="$trace_dir/timings.csv"
        printf 'OGDoK frame timing: %s\n' "$DOK_FRAME_TIMINGS"
    fi
fi
cleanup() { pm_finish; }
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
# GPTOKEYB may contain the launcher and its arguments (PortMaster contract).
$GPTOKEYB OGDoK &
pm_platform_helper "$GAMEDIR/OGDoK"
./OGDoK --user-dir "$GAMEDIR/user" "$@"
result=$?
if [ "$result" -eq 66 ]; then
    # EX_NOINPUT from OGDoK: no usable RES archive was found.
    case "${LC_ALL:-${LC_MESSAGES:-${LANGUAGE:-${LANG:-}}}}" in
        ru|ru[_@.:]*)
            message='Скопируйте хотя бы один файл из оригинальной игры в папку ogdok рядом с OGDoK: OLD_GOLD.RES, PKR.RES или PKE.RES. Затем запустите OGDoK снова.'
            ;;
        *)
            message='Copy at least one original game file into the ogdok folder beside OGDoK: OLD_GOLD.RES, PKR.RES, or PKE.RES. Then launch OGDoK again.'
            ;;
    esac
    printf '%s\n' "$message"
    pm_show_error "$message"
fi
exit "$result"
