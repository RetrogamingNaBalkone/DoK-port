# N21P C12: короткие строки карты без libc calls

Меняется только внутренний `rect` в `ui/map_raster.c`.
Exact C11 raster/header и принятые N20 native proof sources сохранены
в `research/evidence/nds/n21p/pre-map-unroll/` с pins.
Loop order двух map passes, PRE visited, `seen`, class/marker handling,
неизменная трёхпиксельная стрелка и все offsets остались прежними.

## Inclusive width и clipping

Внутренний row fill использует explicit uint8 stores для ширины1…9.
Прежнее libc memset сохранено для general width>9 и полного40960-byte
background clear. Current supported scales2/3/4/6/8 дают:

|Rect|Ширина до clipping|
|---|---|
|FCMap fill, горизонтальная стена|scale+1:3/4/5/7/9|
|Внутренний marker|scale−1:1/2/3/5/7|
|Горизонтальная дверь|scale:2/3/4/6/8|
|Вертикальная линия|1|

Исходная сортировка endpoints, отбрасывание и clipping побайтно неизменны.
После clipping ширина не увеличивается и остаётся≥1. Row loop сохраняет
включительный y2. Вся операция использует byte pointers/stores: typed word
casts, strict-aliasing assumptions и требования выравнивания не добавлены.
Actual shared ARM GCC16.2 выдаёт девять STRB paths; libc call остаётся только
в general fallback и background clear. Static stack rect24 B, raster96 B.

## Воспроизведение

```powershell
py -3.14 -B -X utf8 ports/nds/tools/map/check_map_unroll.py
```

Host old/new differential проверяет17524 raster cases с random glyph planes,
visited, map availability, monster state, heading, всеми scales и offsets.
Отдельный набор охватывает все углы0…360, invalid angle361,7784 прямых rect
cases с обратными endpoints, clipping, width1…12 и полным256-byte fallback.
Проверены44 invalid API guards. Все cases исполняются на четырёх byte
alignments; prefix/tail guards включают как минимум40 байт после viewport.
Совпало1036615680 пикселей, guards и button-strip tail не повреждены.

Fresh native checker повторяет954 принятых RU/EN x86 comparisons,
39075840 пикселей, на новом N21P DLL. Оригинальный N20 checker не изменён:
execution copy меняет только три filesystem expressions — ROOT, import path
и build output directory. Сам набор cases, оригинальные constructors/loops,
Unicorn execution и assertions сохранены. Accepted N20 reports не переписаны.
SHA оригинальных PKR/PKE проверяются до/после; адресная система — Ghidra
NE loader, исходные automap entry PKR1098:1290/PKE1030:1374.
Прежние viewport/scale и initialized stale-arrow adaptations сохраняются.

`map-unroll-equivalence.json` содержит pins, scope и path-only execution copy;
`map-unroll-native.json` — свежие954 native rows;
`map-unroll-source-supersession.json` — узкий bridge от exact прежнего raster.
Source loop/helper bodies и clipping-prefix отдельно сверены на неизменность.
Exact C12 raster сохранён в `c12-map/`.

Этот шаг не меняет частоту карты или ввод. Dirty-only/динамический cadence
и immediate gesture policy принадлежат runtime N21P. Host equivalence
не означает прохождения CPU/FPS бюджета; результат измеряется отдельно
на эмуляторе. Физическая DS/DS Lite остаётся N24.
