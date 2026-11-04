# SP15/SP19 R7 — читаемость сюжетов и состав пакета

4 ноября 2026 года. Прямой запрос после R6.

Текст предыстории README, вступлений и концовок увеличен на 25% на desktop:
20/30/40 px при 720p/1080p/1440p вместо 16/24/32. Логический масштаб этих
экранов — высота 576; обычные меню сохраняют 720. Минимальная логическая
ширина 640 и нижняя граница масштаба 1 сохраняют компактные режимы.
Переносы, пагинация и hit rects вычисляются для нового холста.

Фон README (показанный пользователем FON/GAMEP1) приглушён до 60% исходной
яркости каждого RGB-канала. Исходные RES/палитры не изменяются. Сюжетные
иллюстрации и титульная картинка не затемняются; изменение относится к
тексту поверх узорного фона. Меню/игровая сцена/HUD не увеличены.

NOTICES.txt содержал только атрибуцию методики UI, а не игровой компонент.
В поставке он объединён с полным неизменённым текстом SDL2-лицензии в один
LICENSES.txt. Атрибуция Unslop/CC BY 4.0 сохранена; лишнее пояснение о том,
какой код не включён, удалено. Исходный NOTICES.txt остаётся входом упаковки.
В пакете четыре файла: dok_port.exe, SDL2.dll, README.md, LICENSES.txt.
Новые package gates проверяют полноту SDL2-лицензии, атрибуцию и отсутствие
старых отдельных NOTICES.txt/SDL2-LICENSE.txt и батников.

Release CTest17/17 PASS. 518 CPU-кадров и 288 GL/software readback PASS
(три игры, RU/EN, компактные и desktop-размеры); README 2560×1440 просмотрен.
17 menu UX и восемь packaged процессов PASS, GUI subsystem=2 сохранён.
Все диагностические окна скрыты, аудио dummy, настройки изолированы.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File ports/sdl2/tools/build.ps1 -Configuration Release
$env:SDL_AUDIODRIVER='dummy'
build/sdl2/windows/dok_menu_font_probe.exe . research/evidence/source-port/sp15-ui/r7 --readback
py -3.14 -B -X utf8 ports/sdl2/tools/check_menu_ux.py research/evidence/source-port/sp15-ui/r7-process
powershell -NoProfile -ExecutionPolicy Bypass -File ports/sdl2/tools/package.ps1 -PackageName windows-playtest-20261104-sp15-r7
py -3.14 -B -X utf8 ports/sdl2/tools/check_package.py artifacts/sdl2/windows-playtest-20261104-sp15-r7 --hidden
```

Пакет: artifacts/sdl2/windows-playtest-20261104-sp15-r7.zip.
R6 сохранена. Игровые механики и прохождения не перепроверялись.
Следующий основной ID — SP17; полная SP19 остаётся открытой.
