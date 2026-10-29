# N07: граница аппаратного 3D backend

29 октября 2026 года. Это исследование установленного SDK и принятая граница
архитектуры для N07. Backend собран ARM9; до фактического исполнения это не
означает готовый renderer или измеренную производительность DS. Основа
геометрии — оригиналы PKR/PKE и [контракт N06](N06-camera.md), без GBA-кода.

## Первичные источники и адресация

Локальный SDK: `C:/Users/dspev/.local/msys64/opt/wonderful/thirdparty/blocksds/core`,
BlocksDS 1.24.0-1. Проверены `libs/libnds/include/nds/arm9/videoGL.h`,
`video.h`, примеры `examples/graphics_3d/paletted_textures/source/main.c`,
`2d_and_3d/source/main.c`, `box_test/source/main.c` и tutorial source.
SHA выбранных файлов — `research/evidence/nds/n07/sdk-gpu-sources.json`.

Онлайн-сверка 29 октября 2026 года:

- [SDK videoGL API](https://blocksds.skylyrac.net/libnds/videoGL_8h.html)
  определяет fixed-point API, форматы материалов, чтение матриц и PosTest.
- [SDK video API](https://blocksds.skylyrac.net/libnds/video_8h.html)
  определяет размеры и допустимые назначения девяти банков VRAM.
- [Первичный tutorial BlocksDS](https://blocksds.skylyrac.net/tutorial/intermediate/3d_graphics/)
  описывает 6144 vertices, 2048 polygons, доступ к texture VRAM и rendered-line
  diagnostics. При расхождении с tutorial используем закреплённый header:
  texture palettes разрешены E/F/G, а не F/G/H; F имеет 16 КиБ, E — 64 КиБ.
- [GBATEK автора эмулятора no$gba](https://problemkaputt.de/gbatek-ds-3d-status.htm)
  документирует GXSTAT, RAM_COUNT и RDLINES_COUNT. Это первичное аппаратное
  исследование, а не документация Nintendo; неопределённости источника не
  превращаются в гарантии порта.

Адреса ниже — ARM9 memory-mapped I/O, не DOS NE offsets или DPMI selectors.
Исходные EXE здесь не анализировались и не изменялись.

## Численные пределы и единицы

Vertex command `glVertex3v16()` принимает signed 4.12: `[-32768,32767]`,
то есть `[-8,8−1/4096]` GPU units. Matrix entries/translation используют
signed 20.12 в int32. Texture UV — signed 12.4. Автоматические macros с
непроверенными shift/cast не являются проверкой входного диапазона.

Один целиком camera-relative vertex16 с масштабом 1 unit=4096 world units
не охватывает диагональ карты 64×64×480: она больше 43 тысяч world units.
Грубое деление world на два охватывает карту, но округляет нечётную высоту
ceiling и увеличивает ошибку вблизи. Это не принимается молча.

Принято: **1 GPU unit=512 world units, локальные vertices chunk/cell**.
Chunk origin сначала получает semantic camera-relative f32 translation;
локальные позиции остаются в пределах vertex16. `vertex16=localworld*8`
точно представляет исходные целые coordinates, включая нечётный ceiling−951.
Шаг GPU равен 1/8 world unit. Ошибки Q12 matrix, rotation,
translation, division и raster проверяются отдельно на actual PosTest.

Origins XYZ расположены на lattice512, ближайшей к центру клетки. Единая
rotation `nearest(Q15/8)` используется и для origin translation, и для local
vertices. Translation использует arithmetic floor, сохраняющий целочисленный
lattice shift независимо от знака. Обычная GE composition `P×M` всё же
создаёт разницу 1 clip LSB между origins. Независимый численный контрпример:
camera `(0,−300,0)`, sin=cos=23170, endpoint `(−512,0,2048)`, origins0/512
по X. Это стало причиной изменения архитектуры до full-scene реализации.

Backend формирует **единые quantized clip columns** `C=floor(P×R/4096)`.
Camera anchor — `floor(camera/512)*512`; его общая camera clip translation
вычисляется один раз. Origin добавляет только `Ccol*integerGPUunits`.
В GE загружается combined projection при identity position matrix. Поэтому
смена origin компенсируется exact integer translation; итог общих endpoints
побитово одинаков. Большие merged surfaces делятся по допустимому диапазону.

Реальный GE-only gate в закреплённом melonDS DS 1.4.0 дал **PASS**:
24 actual PosTest records, шесть shared endpoint pairs через разные origins;
semantic matrices12, actual GE clip matrices16 и actual XYZW совпали с
независимым oracle без допуска. Максимальные ошибки выбранных continuous
controls относительно N06 Q15: X0.0734821 px, Y0.213889 px. Это измеренные
контрольные точки, не универсальный предел всех вершин и не raster QA.
Первичный отчёт — `build/nds/n07/emulator/geometry-only/result.json`.

Q15→Q12 rotation даёт ошибку коэффициента не более 1/8192. Она измеряется
относительно N06 отдельно от целочисленного native projection и GPU raster.
Все world-relative calculations используются только после subtraction camera;
огромный global translation не загружается в GPU.

Backend получает проверенный пакет world/local positions и не выбирает
другой масштаб в зависимости от камеры. Расчёты checked int64 на CPU,
конечные v16/f32 range checks выполняются **до первого FIFO command кадра**.
Камера допускается только в области, для которой доказан f32/clip range;
внешняя диагностическая камера должна явно пройти тот же gate.

## Проекция и видимость

LCD viewport — верхние строки0…153. DS SDK использует bottom-origin Y,
поэтому вызов — `glViewport(0,38,255,191)`. Первый независимый full-scene
coverage gate обнаружил сдвиг38px при наивном `(0,0,255,153)`; его исправили
в backend, маски сравнения не ослабляли. Ни HUD, ни нижняя диагностика не
должны занимать polygons 3D engine. Доказанное N06 continuous mapping:

```text
eye_z = depth + 200
clip_x = 5*lateral/4 − eye_z/320
clip_y = −5*dy/2 + eye_z/160
clip_w = eye_z
```

Используется ручная off-axis `m4x4`: не symmetric perspective и не near-plane
edges, предварительно округлённые вызовом `glFrustumf32`. CPU representation
матрицы и фактическая `GL_GET_MATRIX_PROJECTION` должны совпасть; затем
PosTest проверяет реальные clip XYZW landmarks внутри GPU. После чтения
XYZW отдельно проверяется экранное преобразование с pixel-centre convention.
Это различает ошибки матрицы, viewport и raster sampling.

Для vertical planes native near — depth=0, eye_z=200; billboard требует
depth>0. Численная плоскость GPU не подменяет source class-specific rules.
Особая horizontal negative-depth SAR5 ветка N06 не равна обычному near clip.
N07 должен либо реализовать её геометрическую адаптацию с fixtures, либо
явно оставить соответствующий критерий открытым; удалять эти поверхности
ради удобства GPU нельзя.

Номинальные far=49152world, near=200world; Q12 coefficients depth: 4129/−3213.
Фактические после quantization — near200.0068085, far49850.1818world.
CPU source clipping остаётся depth0; GPU depth precision — отдельная
адаптация менее0.007world на near, которую нельзя назвать exact200.
Far выбирается вне всей допустимой карты и вертикального диапазона, с
доказательством границы по реальным данным и допуску камеры. При этом
он не отсекает исходно видимые поверхности. Source builder AABB не считается
найденной frustum far plane. `POLY_RENDER_FAR_POLYS` предотвращает скрытое
удаление пересекающего far polygon; точное значение far ещё принимается
главной спецификацией N07 после world range audit.

Сначала применяется доказуемое отсечение вне frustum и внешних/back faces,
затем допустимое объединение совпадающих coplanar surfaces. Нельзя
использовать box-test false как единственное доказательство invisible:
большой box, содержащий frustum, имеет особый случай. Нельзя включать
произвольный fog/distance cutoff для прохождения capacity gate.

N07 opaque mode: texture indices и исходная palette, lights/fog/blend/AA
выключены до отдельных контрольных сцен. LIGHT, вода, reflections и
палитровые эффекты остаются N13; это не обещание полной визуальной верности.

## Backend и публикация кадра

Предлагаемый переносимый frame packet содержит:

1. Camera pose, accepted projection identity, world generation/epoch.
2. Chunk origin и локальный диапазон vertices, fixed-point positions/UV.
3. Polygon material ID, facing и исходную provenance поверхности.
4. Полный набор resource IDs/payload hashes, необходимых этому кадру.
5. Counts input/culled/merged/submitted и проверенную оценку hardware storage.

Frame admission проверяет arithmetic, coords/UV, все texture leases,
vertex/polygon budget и caller-owned storage перед аппаратной публикацией.
При отказе previous frame остаётся видимым; кадр не обрезается по capacity.
Candidate CPU scratch может изменяться, published descriptor — нет.
Границы и output descriptor следуют N03/N04, per-frame malloc не нужен.

Backend вычисляет immutable `CameraPlan` один раз на кадр: anchor, rotation,
общие clip columns и camera translation. Preflight и emit используют тот же
plan, для клетки меняется только точная lattice translation. Повторное
построение общих columns на каждую клетку было устранено после измерения
CPU stages; геометрия и fixed-point результаты остались теми же.

Простой backend использует vertex16/quads/triangles и явные матрицы. Packed
display lists, vertex-diff commands, custom assembler, aggressive visibility
и micro-optimizations не нужны до принятия сцены/бюджета и измерений.

## Принятый VRAM layout и texture cache

| Банк | Размер | Назначение |
|---|---:|---|
| A | 128 КиБ | Texture slot0 |
| B | 128 КиБ | Texture slot1 |
| C | 128 КиБ | Нижняя tiled4bpp диагностическая console |
| D | 128 КиБ | Texture slot3 |
| E | 64 КиБ | Main 2D HUD/background |
| F | 16 КиБ | Texture palettes |
| G | 16 КиБ | Нераспределённый запас |
| H | 32 КиБ | Нераспределённый запас |
| I | 16 КиБ | Нераспределённый запас |
| **Итого** | **656 КиБ** | Texture capacity 384 КиБ; reserve 64 КиБ |

Точные mapping offsets задаются header enums и проверяются после init.
Bitmap8 HUD требует отдельного remap используемых цветов, чтобы индекс0
был прозрачен в сцене; полностью чёрные glyph pixels не должны исчезнуть.
Таблица не обещает, что произвольная RGB555 HUD bitmap поместится в E.

Первый вариант кэша — **полный статический texture working set уровня**, если
измеренный дедуплицированный набор помещается в 384 КиБ с alignment. Тогда
в active movement нет texture uploads, а все textures frame-pinned. Это
архитектурное упрощение до измерения, не предположение о размере working set.
Ресурсы с разными logical IDs могут alias только при совпадении payload и
требуемого palette/material mode; reuse поколений не изменяет pinned lease.
POT padding не растягивает logical texture. UV bounds учитывают logical size.

Если working set не помещается, нужен явный admission/eviction protocol.
Данные нельзя перезаписывать, пока текущий raster ещё использует texture
bank. Предпочтителен loading frame с остановленным 3D либо доказанное
разделение банков. Один `swiWaitForVBlank()` не доказывает безопасный upload:
3D начинает scanlines до конца VBlank. Нельзя чинить cache miss белой
текстурой, скрытым material substitution или mid-frame remap банка.

Preload явно выключает main 3D layer и rendering core `POWER_3D_CORE`,
сохраняя geometry/matrix core для SDK. По завершении полного upload оба
включаются раздельно: core после upload, 3D layer только после первого нового
frame swap. Старый geometry list с новой texture set не показывается.
Отказ SDK оставляет loading mode, частичная сцена не публикуется.
Backend sample/OR sticky flags выполняется до каждого DISP3DCNT write.

N04 package и формат не меняются. GPU_INDEX8_POT и PALETTE_RGB555 дают
готовые payload для GL_RGB256; source transparent/color0 rules выбираются
по material contract, не глобально для пола и стен.

## Счётчики и измерения

| Адрес/бит | Смысл | Где снимать |
|---|---|---|
| 04000604, bits0…11 | Polygon RAM count, максимум2048 | После завершения geometry commands, до swap |
| 04000606, bits0…12 | Vertex RAM count, максимум6144 | В той же точке |
| 04000060, bit13 | Polygon/vertex overflow, sticky W1C | По завершённому показанному кадру |
| 04000060, bit12 | Rendered-line buffer underflow, sticky W1C | По завершённому показанному кадру |
| 04000600, bit15 | Matrix stack overflow/underflow | Отдельно от geometry overflow |
| 04000600, bit27 | Geometry engine busy | Для fence перед counts/PosTest |
| 04000320, bits0…5 | Minimum buffered lines минус2 за предыдущий кадр | После завершения показа |

RAM counters сбрасываются после swap/VBlank; поздний ноль не доказывает
пустую сцену. `RDLINES_COUNT=0` сам по себе не доказывает actual underflow:
нужен bit12. При очистке W1C нужно сохранить enable bits и не скрыть ошибку
до записи в frame telemetry. Геометрический fence ограничен timeout с
ошибкой; бесконечное ожидание не является успешным бюджетом.

CPU timer охватывает build/cull/admission и submit, включая FIFO stalls.
Upload bytes/counts и диапазон scanlines записываются отдельно.
GPU не предоставляет простой timestamp полного raster; его нельзя
выдумать из длительности `glFlush()`. Для raster сохраняются underflow,
RDLINES_COUNT и frame cadence. Измерение graphics geometry completion
отделяется от rendered-line completion.

Ранний budget должен удерживать **весь** required frame в capacity и два
VBlank для тяжёлых камер, а не только первый кадр. В probe нужны loops
неподвижных poses, moving sweeps, near intersections, высоты/низкие потолки
всех трёх уровней и негативные capacity/coordinate/cache cases.
Эмуляторные timer/VBlank значения остаются виртуальными; физические DS FPS,
LCD artifacts, flashcart I/O и реальные line-overrun пределы отложены до N24.

## Выбор ARM9 instruction mode и фаз диагностики

Первые полные 19 cases подтвердили geometry/coverage/negative admission,
но Thumb-вариант не выполнил ранний CPU budget: 190 из936 render frames
занимали три VBlank. Первичный FAIL сохранён в
`research/evidence/nds/n07/experiments/thumb-baseline/`; ROM/FAT не дублируются,
сохранены их SHA, полный report и выбранные PNG.

Принят ARM instruction mode для `render/scene.c`, `render/geometry.c` и
`platform/render_probe/render_backend.c`; shell/loader/прежнее ядро остаются
Thumb. Это использование ARM9 для fixed-point math и FIFO emission, без
изменения surfaces, UV, textures или masks. Воспроизводимый переключатель —
`RENDER_ARCH ?= -marm` в Makefile; исходный опыт воспроизводится override
`make -C ports/nds -f Makefile.n07 RENDER_ARCH=-mthumb` после чистой N07-сборки.

Независимое сравнение ARM/Thumb: все19 верхних PNG и21 semantic fields всех
944 history records совпали. Intermediate moving PNG в исходном Thumb report
не сохранялись; их равенство не заявлено. Сравнение —
`research/evidence/nds/n07/arm-thumb-comparison.json`. После смены mode
934 из936 кадров уложились в два VBlank, два первых heavy frames всё ещё
превысили бюджет. Полная console formatting тогда входила в первый interval;
тот опыт не отделял её стоимость от произвольной фазы старта после loading.
Этот второй FAIL
сохранён в `experiments/arm-before-case-preparation/`.

Следующее принятое разделение — explicit benchmark phases: loading,
active render frames, diagnostic UI, READY. Полная console formatting и
публикация CPU snapshot выполняются перед READY отдельным history record
`DIAGNOSTIC=32`, без `PRESENTED`. Они не называются texture I/O и их ticks
не теряются. Все936 actual render frames, **включая первые и WARMUP**, остаются
под строгим ограничением двух VBlank. UI-фаза не является gameplay loop;
N08 реализует clocks/presentation cadence отдельно. Промежуточный runtime gate
после этого изменения подтвердил **PASS**: 936 render frames, из них488
moving; каждый укладывается в два VBlank, максимум1120380 bus-clock ticks.
19 diagnostic records того промежуточного опыта измерены отдельно,
максимум174422 ticks. Это
виртуальные интервалы эмулятора, не реальный DS FPS.

Поздняя проверка publication обнаружила необходимость полного seqlock
протокола: сначала volatile odd sequence и его cache flush, затем запись
payload и полный flush, затем even sequence и flush. Это применяется к
главному probe, history ring и PosTest records; observer принимает только
одинаковый чётный sequence до и после чтения.

После этой правки повторный runtime снова выявил два first-heavy intervals
по три VBlank при почти прежних prepare/submit ticks. Независимая фазовая
арифметика и pinned SDK объясняют причину: libnds `swiWaitForVBlank()`
очищает pending flag и ждёт **новый** VBlank IRQ, а loading завершался с
произвольной фазой. Это custom libnds function, не BIOS SWI. Для case11
phase≈301430 + work830470 =1131900 ticks, для case14
phase≈324759 + work825226 =1149985; оба превышают второй upcoming boundary
1120380 ticks. Phase здесь выведена из периодических boundaries, а не
снята отдельным hardware timestamp. Полный FAIL и расчёт сохранены в
`experiments/seqlock-phase-failure/` и `presentation-phase-diagnosis.json`.
Семантика подтверждена [официальной документацией libnds](https://blocksds.skylyrac.net/libnds/interrupts_8h.html).

Принято явное начало presentation sequence после loading/READY: один
`SYNC=64` на case ждёт свежий VBlank без подготовки или подачи геометрии.
Его ожидание полностью сохраняется отдельной history record. Якорь
`previous_present_ticks/vblank` фиксируется сразу после wait **до**
history/probe flush; вся следующая publication входит в бюджет первого
actual render frame. Между steady/moving frames синхронизация не добавляется.
SYNC и render используют одну noinline-функцию `wait_presentation_boundary`:
свежий VBlank wait, затем timer sample. Включение 3D-слоя и telemetry после
этой точки входят в следующий interval; последний tail входит в DIAGNOSTIC,
начало которого привязано к последнему presentation sample и VBlank counter.
Никаких tolerance или фиктивной работы для выравнивания не добавлено.
Все936 render frames, включая первые, остаются под прежним строгим gate.
Финальный повтор с этим контрактом прошёл: **PASS**, все936 render frames
(488 moving) не более двух VBlank, максимум1120380 ticks. Полная история
содержит982 records: 8 loading, 19 SYNC, 936 render, 19 DIAGNOSTIC.
SYNC максимум559720 ticks, суммарно9870960; DIAGNOSTIC максимум172339,
суммарно3066860. Каждая SYNC непосредственно связана с первым render
через case/frame IDs и общий VBlank anchor; это не скрытый warmup frame.
Финальные source/input pins не изменились во время прогона. Полный результат —
`build/nds/n07/emulator/renderer/result.json`, постоянная копия и hashes —
`research/evidence/nds/n07/emulator/`.

Полные статические texture sets трёх уровней занимают278528/319488/294912
байт из393216; на каждый уровень одна palette512B. Actors и эффекты N12/N13
в этот working set не входят. На936 observations `RDLINES_COUNT=46`,
underflow/overflow/matrix error равны0. Восемь negative admission checks
дали mask255 при сохранении candidate output sentinel и опубликованного кадра.

Финальная measured RAM closure:
172032 static/startup +2293760 managed slabs +548864 SDK heap headroom
+655360 excluded reserve =3670016B. World storage265864B, scene184408B,
scratch105052B; максимум assets385024B. Это ранний static renderer probe,
не пик RAM полной игры и не доказательство аппаратной совместимости.

## Границы последующей интеграции

- World surface builder и backend используют общий strict-C11 frame packet;
  backend preflight оценивает interior quad как4 vertices, clipped boundary
  quad консервативно как10. Actual RAM_COUNT проверяется отдельно.
- Static level preload принят по измеренному полному working set. Actor
  textures N12 и LIGHT/вода N13 требуют нового общего working-set gate.
- CPU source near semantics и GPU clipping различаются; horizontal SAR5
  ветка и эффекты не объявлены восстановленными этим аппаратным probe.
- Gameplay cadence N08 и полная игра ещё не созданы. Аппаратные FPS,
  flashcart uploads и LCD artifacts проверяются в N24.
