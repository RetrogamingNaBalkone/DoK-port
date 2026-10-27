# G20A: первая native контрольная точка streaming segment BSP

27 октября2026. Продолжение [выбора архитектуры](gba-architecture-selection.md).
**Текущий streaming/quad вариант не выбран:** он проиграл действующему128
рендеру. Это не доказательство непригодности полноценного sector/column/span
рендера. К точечной доводке kernels не переходить; следующий крупный опыт —
подготовка видимых интервалов и общих spans до sampling, затем сравнение OBJ.
G20/G20A/goal открыты. Основной2775 и оба96/72 quality ROM сохранены.

## Реализованное отличие от прежнего grid-BSP

Статическая сцена подготовлена offline из ROM asset package. BSP разбивает
пространство плоскостями настоящих wall/rim отрезков. Runtime идёт от ближней
части к дальней и сразу проецирует/рисует статические части. Общего массива
статических граней и их сортировки нет. Leaf хранит клетки низкого потолка;
их геометрия и высоты сохранены. Динамические грани дверей/панелей и billboards
распределяются по BSP с разбиением экранных X-интервалов. UV исходной грани
сохраняется при clipping. Внутри leaf спрайты ограничиваются высотой потолка.

Это уже работающий новый порядок построения/композиции на GBA, а не второй
occlusion prepass над старым списком всех стен. **Но** пиксельные kernels и
coverage bitset переиспользованы, потолки ещё рисуются отдельными клеточными
affine quads. Общая visplane/span архитектура и software direct framebuffer
не реализованы этим вариантом и не считаются проверенными.

ROM — `research/experiments/g20a-stream-bsp-v3/hw_v128_stream_bsp/dok-diagnostic.gba`,
SHA256 `bfdba7d0455199e5599ebea9bf795abefdaff0c730f63db4b848e71a12825184`,
16 429 504B. Чистая повторная сборка `research/experiments/g20a-stream-bsp-repeat/` побайтно
совпадает, включая последующие compile-time scratch capacity assertions.
Контроль128 в обеих сборках побайтно совпал с опубликованным2775.
Новый ROM не назначен default и не копировался в artifacts.

Код: `src/renderer_stream_state.inc`, `src/renderer_stream.inc`, gated adapters
в renderer/rect/sprites; упаковка `scripts/gba_stream_scene.py`, основа —
`scripts/compile_gba_render_scene.py`. Все поля/указатели runtime — GBA;
world units480 на клетку. Это не NE/Ghidra/DPMI адреса. Оригинальные EXE не
анализировались и не изменялись; сторонний engine code не переносился.

## Измерения

Native-clock mGBA libretro SHA5cac11c…667e4, replacement BIOS, без overclock,
тот же PCM и методика текущего128. Spatial440 замораживает симуляцию, убирает
capture hold из FPS samples; не доказывает whole-level gameplay minimum.

|Мир|Камер|Медиана CPU относительно2775|Spatial minimum2775 → BSP|
|---|---:|---:|---:|
|L1|136|1,507×|9,95 →7,96FPS|
|L2|168|1,573×|9,95 →6,64FPS|
|L3|136|1,361×|14,93 →11,95FPS|

Из440 видов0 стали быстрее по показанному FPS,424 медленнее,16 равны.
Короткие живые follow-camera/PCM прогоны24 кадра RU/EN:
19,37/19,11/27,57 у2775 против13,40/12,15/19,11 уBSP. Это парные24-frame
fixtures, **не** прежние turn120/walk120 и не новый полный приёмочный проход.

Отдельная instrumented атрибуция, cycles; таймеры включают накладные расходы:

|Вид|Validation|Dynamic partition|Box tests|Dynamic raster|Static atoms, включая projection|Ceilings, включая setup|
|---|---:|---:|---:|---:|---:|---:|
|L1 cell520,225°|61 824|33 536|91 456|275 648|365 632|546 176|
|L1 cell3640,225°|61 760|34 112|29 568|159 552|189 632|798 976|
|L2 cell1592,135°|77 440|72 576|79 424|581 568|441 088|226 112|
|L2 cell2871,225°|77 376|44 608|61 056|588 032|427 456|346 816|
|L3 cell1544,315°|49 536|24 192|60 928|285 440|310 016|704|

Устранение общей сортировки само по себе недостаточно. Сохранилась стоимость
отдельного sampling/coverage и потолочных quads, добавились traversal,
разбиение динамики и валидация8192B исходных TMap/FCMap. На stationary кадрах
контроль дополнительно использует уже проверенный static projection cache.
Даже короткий moving-camera контроль быстрее BSP, поэтому stationary cache
не является единственным объяснением проигрыша. Дальнейшее размещение helpers
и настройка порогов этого наброска сейчас не приоритетны.

## Корректность и ограничения

18 стартовых PPU кадров, три мира ×RU/EN ×три направления: coverage exact у18,
native HUD/weapon overlay exact, максимум272 изменённых пикселя сцены из38 400.
Перекрытия теперь идут по пространственному BSP, а не прежнему среднему ключу;
точное RGB совпадение не заявляется. Пример тяжёлого L2 cell1592,135°:
[контроль](../research/experiments/g20a-stream-bsp-v3/hw_v128_entity_rect/stream-qa-0-L2-27120-11760-135.png),
[BSP](../research/experiments/g20a-stream-bsp-v3/hw_v128_stream_bsp/stream-qa-0-L2-27120-11760-135.png).
Сравнение на240×160 показало сохранение рисунка уступов/решёток; отдельной
оценки движения на физическом маленьком экране не было.

`verify_gba_stream_scene.py` прошёл48 native fixtures: старты, тяжёлые виды,
обе локализации, TMap edit/restore. Проверяет выравнивание overlays, отсутствие
циклов/повторных ссылок dynamic fragment lists, точное однократное владение
каждой исходной экранной колонкой динамической грани, прежние image/surface
visibility leases и guards. Максимум35 fragments в этом наборе при4096 capacity.
При TMap mutation включается полный прежний путь, его PPU exact относительно
контроля. Это не доказательство всех FCMap mutation semantics или всех
возможных pixel occlusion. Сохранение leases существенно: `gba_visual_poll`
подаёт их в animation/AI consideration, графическая замена не должна незаметно
менять первоначальное пробуждение.

EWRAM261296/free848; IWRAM23336/free1240 до сохранённого резерва8192B стеков.
Main stack high water2528/margin5408 в проверенном наборе. Крупные temporary
buffers занимают прежние covered/surfaces slots; их ёмкости проверены статически.
Полная UI/water/AI/combat/item/interaction regression, динамические переходы
между backend, all-awake stress и physical GBA для нового ROM **не приняты**.
Проигрывающий вариант не продвигается до такого этапа.

V1/V2 — **исправленные ранние наброски, не итоговые доказательства**. Первоначально
world XY overlay ошибочно начинался с невыравненного `covered[]` (offset2 mod4).
ARM word accesses искажали разбиение динамики. V3 явно выравнивает базу, новая
native проверка контролирует адрес и сохранение колонок. Замеры V1/V2 оставлены
в build для истории, но не используются для оценки архитектуры. В V3 сохранён
существующий rectangle sampler для clipped billboards и переиспользуется
проверенный exact map comparator; новые sampling ASM kernels не добавлялись.

## Воспроизведение

```powershell
& C:/Source/wasdify/.venv/Scripts/python.exe scripts/prototype_gba_bsp.py --out research/experiments/g20a-stream-bsp-repeat --variants hw_v128_stream_bsp hw_v128_entity_rect
& C:/Source/wasdify/.venv/Scripts/python.exe scripts/benchmark_gba_spatial_sweep.py --build-dir research/experiments/g20a-stream-bsp-v3/hw_v128_stream_bsp --report research/experiments/g20a-stream-bsp-v3/spatial.json
& C:/Source/wasdify/.venv/Scripts/python.exe scripts/profile_gba_stream_scene.py --build-dir research/experiments/g20a-stream-bsp-v3/hw_v128_stream_bsp --report research/experiments/g20a-stream-bsp-v3/profile.json
& C:/Source/wasdify/.venv/Scripts/python.exe scripts/verify_gba_stream_scene.py --build-dir research/experiments/g20a-stream-bsp-v3/hw_v128_stream_bsp --reference-build research/experiments/g20a-stream-bsp-v3/hw_v128_entity_rect --report research/experiments/g20a-stream-bsp-v3/structure.json
& C:/Source/wasdify/.venv/Scripts/python.exe scripts/report_gba_stream_scene.py
```

Авторитетная сводка: `analysis/g20a-stream-bsp-trial.json`, с SHA входных
отчётов и ROM. Отдельный [аудит дальности/темноты/AI](gba-dark-distance.md)
учитывает новые идеи пользователя; сокращение дальности и AI здесь не вводились.
