# OGDoK for PortMaster

Copy `OGDoK.sh` and `ogdok/` into your ports directory. Add your own
`OLD_GOLD.RES`, `PKR.RES` and/or `PKE.RES` beside `ogdok/OGDoK`.
Launch OGDoK from the ports menu. Resources are not included.

Settings, saves, screenshots and logs live in `ogdok/user/`. When updating,
replace the program files and keep that directory and your RES files.
The package never supplies a user profile. Saves can be copied between
Windows, Linux and PortMaster; resource paths and settings are device specific.

The default canvas is 1280x720, GLES2, Nearest, FXAA/xBR off. The launcher
starts fullscreen on the first run and does not override saved video settings.
Native SDL gamepad input is used; the PortMaster exit combination is provided
by gptokeyb. No keyboard emulation is installed.

Use the in-game controls menu to select the modern or classic preset and
inspect bindings. Start pauses, D-pad navigates menus, A confirms, B returns.

Hardware acceptance is pending on RG Vita Pro/ROCKNIX. Future RG DS Plus
tests use 1024x768 on one screen and record the state of the second screen.
Neither device establishes performance on RK3326.

Build instructions: `docs/source-port/linux-portmaster.md` in the source repo.
Thanks to the creators of Old Gold and Dungeons of the Kremlin for the
original games, and to SDL and PortMaster contributors for the platform tools.
