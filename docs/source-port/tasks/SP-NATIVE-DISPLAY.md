# SP-NATIVE-DISPLAY — разрешение первого запуска

Статус: выполнено программно, 8 ноября 2026. Запрос пользователя: автоопределение
разрешения во всех версиях OGDoK. Windows-плейтест не переоткрывается.

## Контракт и реализация

После SDL_Init и выбора профиля, только при отсутствии settings.ini, запросить
SDL_GetDesktopDisplayMode(0). SDL-дисплей 0 соответствует основному экрану
нового окна. Поддерживаемые размеры 320…7680 × 200…4320 записываются в width,
height, fullscreen_width и fullscreen_height. При ошибке остаётся 1280×720,
диагностика попадает в port.log. Список режимов software-растеризации не меняется.

Настройки задаются до CLI overrides, сохраняются обычным save_settings.
Существующий профиль, включая явные настройки пользователя, не переопределяется.
Windows/Linux стартуют оконно; PortMaster launcher задаёт DOK_PORTMASTER=1,
по которому новый профиль стартует fullscreen. Фиксированного INI-шаблона нет.
Новый игровой CLI и миграции промежуточных профилей не добавлены.

## Проверки и воспроизведение

Windows Release/CTest 20/20 и реальные hidden-процессы PASS. Linux GL/GLES и
ARM64/QEMU CTest по 20/20. Linux GL/GLES: Xvfb 1024×768 и 1920×1080, обычный
и PortMaster-режимы, перезапуск с сохранёнными 800×600, временный --size640×480
без перезаписи родного дефолта — PASS. Launcher mock 1/1 PASS.

```sh
bash ports/sdl2/tools/check-display-modes.sh build/sdl2/linux-x86_64-opengl/OGDoK build/sdl2/checks/native-next
python3 ports/sdl2/tools/check_display_defaults.py --exe build/sdl2/windows/OGDoK.exe --out build/sdl2/checks/native-windows-next
```

Полные результаты: build/sdl2/checks/native-display-01, native-display-gles-01,
native-display-windows-01. Экранов Vita Pro/DS Plus пока нет в аппаратной
приёмке. Следующий шаг — SP20-03 плейтест Linux и SP20-04/05 на Vita Pro;
будущая DS Plus проверяется отдельно при 1024×768. GBA/NDS и оригиналы не менялись.

Пакеты: OGDoK-Win-native-display-20261108.zip, Linux playtest r3 и PortMaster
playtest r4 в artifacts/sdl2, без RES. Windows packaged 8/8 и Linux runtime
15/15 PASS. Состав Unix-архивов и контрольные суммы проверены. Постоянные
результаты — research/evidence/source-port/sp-native-display-20261108/.
