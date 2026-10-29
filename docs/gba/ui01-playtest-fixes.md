# UI01: выход из предыстории и текстура потолочных уступов

29 октября 2026 года, исправления по пользовательскому плейтесту G21.
Текущая ROM — `artifacts/gba/DoK-port_UI01.gba`, 16015212 байт, SHA-256
`c12fff7033d02c6bc6cd2ddfd3d0c65ec5880ce2419b6cf50ac497973ab86844`.
Сборка, ELF/map, полные отчёты и PNG — `build/gba/ui01-playtest-fixes/`.
Компактный итог — `analysis/gba/ui01-playtest-fixes-20261029.json`.

## Предыстория

При выходе flow уже переходил в TITLE и возвращал фокус на «Предысторию»,
но видимый экран мог оставаться текстовым. STORY сохраняет те же UI state/focus,
что главное меню; renderer ошибочно запоминал эту пару как готовое меню в VRAM.
Теперь текстовые flow-экраны инвалидируют кеш статического меню.

Управление сохранено: A/Start листают вперёд и закрывают последнюю страницу;
B листает назад и закрывает первую. Select переключает язык. Музыка продолжается,
мир не создаётся и не изменяется. Прежняя проверка состояния flow пропускала баг;
новая сравнивает все 38400 индексов и 512 байт палитры меню до входа и после выхода.
54 сценария RU/EN покрывают B, A и Start, однокадровые нажатия с восемью фазами
и удержание 20 кадров. На прежней ROM `f16dfa82…714f21` проверка падает сразу
после B: `Story exit left stale text instead of the menu`.

## Потолочные уступы

Симптом воспроизведён в LEVEL1 RU, на старте `(15120,−300,22800)`, heading90:
зелёная полоса над деревянной дверью. В сохранённом DOS-кадре G00
`research/evidence/reference/ru-01/level1-start.idx` этой полосы нет.
Ресурс потолка — `ДПОТОЛОК1`; ошибочный участок принадлежал вертикальному
уступу с материалом `ДСТЕНАК1`. В нижней части этой стеновой текстуры есть трава.

Ошибка — перепутанные границы V. При inset400 и высоте изображения192 оригинал
передаёт **V_start=0, V_end=80**, а порт выбирал 80…191. Исправленный raster
принимает независимую включительную нижнюю границу V, сохраняя исходную высоту
как шаг между колонками. Prelit-банк и быстрый прямоугольный путь сохранены;
таблицы полных масштабов не применяются к укороченному диапазону. Публичные
структуры, размеры workspace и исходные изображения не менялись.
Внутренние вызовы остальных raster-путей явно передают полный диапазон (`−1`).

Доказательства — сохранённые Ghidra-экспорты `research/evidence/rendering/{ru,en}/`
и `research/evidence/surfaces/{ru,en}/`. Следующие адреса — **Ghidra NE-loader**,
не файловые смещения и не runtime-селекторы DPMI:

- RUS world builder `1098:3757…3779`: последним аргументом передаётся
  `image_height*inset/960`, перед ним нулевая верхняя граница V.
- RUS constructor `10B0:0B32…0B4F` сохраняет V_start/V_end в record+4B/+4D;
  draw `10B0:0C1E…0C96` строит UV с верхом0 и низом V_end.
- ENG: constructor `1040:0B47…0B61`, draw `1040:0C36…0CAE`.

`verify_ceiling_sides_gba.py` исполняет constructor и draw обеих версий в Unicorn.
Обходится только инициализатор массива; raster-dispatch перехватывается для чтения
реальных UV. Восемь векторов: высоты96/192 × inset10/400 × RU/EN.
SHA-256 PKR.EXE — `12f78afc0eba05f76b5dc0bb99da38d7daa20585024c0ba958fe828aa3864d28`,
PKE.EXE — `cce7bc5993d0734ad87e93db37a70b8b52faedfff14ac48d6e317e5ac996a37b`;
до/после проверки совпали. Оригиналы не изменялись. Нового Ghidra-экспорта
и нового полного DOS-сеанса в этом проходе нет: использованы сохранённые доказательства.

Фактические PPU-пиксели уступов сверены с исходными RES/LIGHT/GAMEP:
12 случаев, 7496 пикселей, RU/EN, три позиции камеры, холодный и прогретый
кеш геометрии. Старый renderer отклоняется этим тестом. PNG просмотрены.
Это исправление восстановления; новая текстура или художественная замена не вводились.

## Проверки и воспроизведение

```powershell
py -3.14 -X utf8 ports/gba/scripts/build_gba_frontend.py --out build/gba/ui01-playtest-fixes
$gbaFixBuild = 'build/gba/ui01-playtest-fixes/hardware/hw_v128_entity_rect'
py -3.14 -X utf8 ports/gba/scripts/verify_story_gba.py --build-dir $gbaFixBuild --report build/gba/ui01-playtest-fixes/story.json
py -3.14 -X utf8 ports/gba/scripts/verify_ceiling_sides_gba.py --build-dir $gbaFixBuild --report build/gba/ui01-playtest-fixes/ceiling.json
py -3.14 -X utf8 ports/gba/scripts/verify_performance_arm.py --build-dir $gbaFixBuild --report build/gba/ui01-playtest-fixes/arm.json
py -3.14 -X utf8 ports/gba/scripts/verify_frontend_latency.py --build-dir $gbaFixBuild --report build/gba/ui01-playtest-fixes/latency.json
py -3.14 -X utf8 ports/gba/scripts/verify_flow_gba.py --build-dir $gbaFixBuild --report build/gba/ui01-playtest-fixes/flow.json
py -3.14 -X utf8 ports/gba/scripts/verify_ui_gba.py --build-dir $gbaFixBuild --report build/gba/ui01-playtest-fixes/ui.json
py -3.14 -X utf8 ports/gba/scripts/verify_gba_hardware_water.py --build-dir $gbaFixBuild --report build/gba/ui01-playtest-fixes/water.json
py -3.14 -X utf8 ports/gba/scripts/verify_gba_hardware_transitions.py --build-dir $gbaFixBuild --mode4-transitions --report build/gba/ui01-playtest-fixes/transitions.json
```

Все перечисленные проверки прошли. ARM: 512 rectangle-векторов, из них341
с ограниченным V_end; прежние sampler/coverage/projection/tiled/ABI/guards suites.
Flow — все шесть миров и14 исходных текстовых ресурсов; UI — ввод/пауза/язык.
Короткие UI-нажатия:16 случаев, boot10 VBlank, максимальная задержка5 кадров.
Вода:144 фазы. Переходы:294 случая/5002 video callbacks, смешанных кадров0.
Windows host UI и шесть renderer-видов выполнены; Linux static и Thumb
скомпилированы, Linux-бинарники не исполнялись. Общие Zig/Python/mGBA/Unicorn
использованы без установки отдельных toolchain.

Дополнительное сравнение renderer — `benchmark_gba_hardware.py` с параметрами
`--build-dir $gbaFixBuild --reference-build build/gba/ui01-story-exit/hardware/hw_v128_entity_rect
--languages 0 1 --report build/gba/ui01-playtest-fixes/renderer-comparison.json`.
В18 ракурсах coverage совпадает, HUD не изменился; изменяются потолочные
уступы LEVEL1/2. LEVEL3 в этих ракурсах попиксельно прежний. Изменение CPU
времени в этих замерах менее0,2%; это не замер FPS полного прохождения.

EWRAM261444/262144 B, IWRAM code/data24576/24576 B; отдельные8192 B
стеков/BIOS сохранены. Наблюдавшийся main stack2576 B, нетронутый запас5360 B.
Физическая GBA, оригинальный BIOS и новый полный audio/performance-прогон
не проверены. Все экспериментальные невыбранные renderer-варианты заново
не принимались. G20B остаётся готовой, G21 в работе, G20 открыт по бюджету;
следующий ID — **G21**, дальнейший плейтест и релизная регрессия, затем G22.
