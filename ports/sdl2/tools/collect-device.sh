#!/usr/bin/env bash
# Run on the handheld. Writes only to the explicitly supplied new file.
set -eu
if [ "$#" != 1 ] || [ -e "$1" ]; then echo 'Usage: collect-device.sh NEW_REPORT.txt' >&2; exit 2; fi
{
    date -u
    uname -a
    for path in /etc/os-release /etc/rocknix-release /proc/device-tree/model; do
        if [ -r "$path" ]; then printf '\n%s\n' "$path"; cat "$path"; printf '\n'; fi
    done
    getconf GNU_LIBC_VERSION || true
    for tool in sdl2-config eglinfo glxinfo; do
        if command -v "$tool" >/dev/null 2>&1; then
            case "$tool" in sdl2-config) "$tool" --version;; glxinfo) "$tool" -B;; *) "$tool";; esac
        fi
    done
    for path in /sys/class/drm/*/modes /sys/devices/system/cpu/cpufreq/policy*/scaling_*freq /sys/class/devfreq/*/cur_freq /proc/bus/input/devices; do
        if [ -r "$path" ]; then printf '\n%s\n' "$path"; cat "$path"; fi
    done
    printf '\nPortMaster installation (record version from its UI if not present here):\n'
    for dir in /opt/system/Tools/PortMaster /opt/tools/PortMaster "${XDG_DATA_HOME:-$HOME/.local/share}/PortMaster" /roms/ports/PortMaster; do
        if [ -d "$dir" ]; then
            printf '%s\n' "$dir"
            for name in version version.txt; do if [ -f "$dir/$name" ]; then cat "$dir/$name"; fi; done
        fi
    done
} > "$1" 2>&1
