# N13 — исходные LIGHT и эффекты

N13 готова функционально на эмуляторе. Полный и финальный scoped прогоны
разделены в [паспорте](../tasks/N13.md) и [compact](../../../analysis/nds-n13-effects.json).
Совместная производительность и архитектурное решение принадлежат N13P.

## Образцы и адреса

PKR RU SHA-256 `12f78afc0eba05f76b5dc0bb99da38d7daa20585024c0ba958fe828aa3864d28`,
PKE EN `cce7bc5993d0734ad87e93db37a70b8b52faedfff14ac48d6e317e5ac996a37b`.
Далее адреса Ghidra NE-loader, а не файловые смещения или селекторы DPMI.
`export_effect_evidence.ps1` открывает сохранённые проекты read-only, проверяет
SHA до/после и сохраняет ASM, сегменты и отдельный автоматический C в
`research/evidence/nds/n13/original/{ru,en}`. Псевдокод не является модулем порта.

| Механизм | RU | EN | Проверка |
|---|---|---|---|
| Выбор LIGHT | `1120:147D…1497` | `1078:1483…149D` | 2865 векторов на образец, включая WORD overflow |
| Unsigned clamp | `1088:0544` | `1028:196E` | строки 0…65535 |
| Image crop | `10F8:05C9` | `1060:06D9` | 150 полных изображений на образец |
| Water callback | `1068:0741` | `1020:0C33` | 18 шагов и отмена |
| Water getter | `1068:07A1` | `1020:0C93` | сброс18→0, сохранение displayed17 |
| VGA6 brightness | `10B8:02ED` | `1048:02ED` | все101 процентов пяти палитр |
| Fade in/out | `1110:0524/05B7` | `1068:0554/0659` | исходные34 вызова каждого перехода |
| Span opaque/masked | `1010:0168/0197` | те же смещения | все256 индексов ×16 строк ×3 LIGHT |

Unicorn исполняет экспортированные исходные инструкции. Контролируются только
выделение памяти, ABI closure copy, очередь, retrace/DAC и диагностический сервис
PKE; это не полная DOS-сессия. Свежие полные DOS PLAY captures трёх RU-уровней
снимает `capture_effects_dos.py`: обычные i8042 Enter/Down, без записи RAM/pose/code,
SHA оригиналов до/после. Они хранятся отдельно от N06.

## Свет и цвет

`depth = sign16(depth_q16 SAR16)`; при signed `depth < near` delta=0,
иначе delta=`sign16(depth-near)`. Signed деление delta/120 отбрасывает дробь к нулю;
результат трактуется как unsigned16 и ограничивается15. В штатном PLAY near=1920
(RU DS:0DFA, EN DS:0EA2). Используются исходные LIGHT1/2/3, без GE modulation.
Исходный raster интерполирует глубину аффинно по экранной оси: X у вертикальных
поверхностей и Y у горизонтальных. Евклидово расстояние не эквивалентно.

LIGHT преобразует индекс до проверки прозрачности. В masked vertical span
исходный ненулевой индекс, перешедший в0, не пишет ни цвет, ни depth. Поэтому
палитры недостаточно: такие masked изображения получают точную нулевую маску.
Opaque horizontal surfaces пишут mapped0. Маски объединяются только когда
совпадают на всех реально используемых индексах конкретного изображения.

Сначала выполняется исходное `VGA6_component * percentage / 100`, затем RGB555.
N04 RGB555 недостаточно для восстановления нечётных VGA6-компонентов. Новый typed
decoder читает768 RAW_SOURCE bytes того же неизменного пакета через его публичный
reader, проверяет SHA, kind, bounds и диапазон0…63; payload и N04 формат не меняются.
Все три сохранённых N06 clean PLAY DAC совпадают с исходной палитрой на99%.
Обычный PLAY N13 использует99%, а не100%.

Fade in:0,3,…99; fade out:100,97,…1. Native retrace адаптируется к NDS VBlank;
исходные значения и количество шагов сохраняются. Пересборка всех16 палитр
занимает8192 байта, в выделенной palette VRAM F16 КиБ.

## Реестр эффектов

| Эффект | Исходное поведение и размещение | Путь NDS |
|---|---|---|
| LIGHT | LIGHT1/2/3, все три уровня |16 точных palette rows, screen-axis subdivision |
| Палитры уровней | GAMEP1/2/3; PLAY заканчивает fade на99% | VGA6→percentage→RGB555 |
| Вода | class12 getter, фаза0…17, divisor18, interval0 | N08 callback + исходный cyclic crop |
| Четыре направления воды | flags0408/0808/1008/2008 | division-before-multiply, native priority |
| Базовые animated objects | flag0800; getter заказывает200 callbacks | N08 signed intervals, N12 player |
| Дверь | constructor mirror и right/down crop; N11 correction терминального strip | N11 geometry/UV, затем LIGHT |
| Отдельный reflected world pass | в рассмотренном штатном PLAY пути не обнаружен | не добавляется вымышленный проход |
| Image flag0020 | изменение обхода рёбер raster; в исходных Sprite directives отсутствует | не объявляется зеркалом мира |
| Image flag4000 | ветвь с чтением неинициализированного stack-slot; в данных отсутствует | unsupported, не маскируется регулярным эффектом |

В FCMap исходных уровней1/3 glyphs воды60/62/123/125 отсутствуют. На уровне2 их
386/341/127/141 соответственно. Class12 glyph178 также вызывает effect getter,
но его Sprite имеет flags0010 и не сдвигается: называть все class12 клетки водой
ошибочно. Определения водных ресурсов присутствуют во всех трёх LEVEL.

Проверенный штатный RU loop: `1008:0DAF` dispatch, `0E44` build `1098:30C1`,
`0E84` draw `1098:5435`; последний рисует один sorted list. Горизонтальные draw
`10B0:157E/1AB6` вызывают обычный raster с image. Water getter возвращает тот же
image pointer и меняет crop config, не отражённую камеру или второй мир.
Эта граница доказательства относится к изученному PLAY path данных образцов;
универсальное утверждение обо всех недостижимых ветвях EXE не делается.

Water callback устанавливает displayed=counter и затем увеличивает counter.
На18 следующий callback отменяется; getter при counter18 сбрасывает0, сохраняя
показанную17, и снова заказывает callback. Native constructor capacity4 означает
три slots. Getter игнорирует native capacity refusal после сброса счётчика;
NDS учитывает его отдельным счётчиком и сохраняет существующие callbacks.
Interval0 означает один шаг за service sweep, а не обещание периода60 FPS.
Диагностический clock fixture166 ticks/sweep явно отделён от N08 real clock.

## DS-адаптация и бюджет

`effects.c` разбивает поверхность по границам строк LIGHT. Shared rational
intersections квантуются в существующую целочисленную world lattice. Смежные
стена/пол получают одинаковые разбиения общих рёбер: без этого T junction
показывал чёрные пиксели на DS. Первые три вершины каждого GE quad выбираются
невырожденными; проверка flat coverage сохраняет исходный2 px color guard
и проверку дыр на внутренних рёбрах без ослабления.

384 КиБ transformed-pixel cache выделены внутри прежнего1 МиБ asset pool.
Сначала pin всех sources и variants, затем разрешение pointers после compaction.
Палитровый цвет сам по себе не дублирует pixels. GPU cache сохраняет N12
plan→submit→VBlank→commit и safe scanline guards. Reserve655360 байт не используется.
Накопленный профиль и оценка оставшегося бюджета для боя принадлежат N13P.
Палитровый fade меняет также256 BG colors HUD в том же commit: в DOS DAC
общий для всего кадра. Эти512 дополнительных upload bytes учитываются отдельно
от8192 байт GE palette. Контроль при1% проверяет весь верхний кадр на чёрный.

Native water phases не предполагаются нулевыми у popup: наблюдение LEVEL2
дало phase3 у flags0408. `capture_effects_dos.py` сопоставляет полные9216 bytes
исходной воды и четыре runtime flags с linear image headers, сохраняет два
одинаковых endpoint frame/phase observations. Это не чтение по DPMI-selector.
Отдельный compile-time phase tuple воспроизводит состояние в DS; Native crop
и actual ARM9 bytes совпадают. Два других уровня проверены в тех же poses,
с подтверждённым отсутствием размещённой scrolling water.

На27 poses9283 interiors согласованы с native scalar LIGHT; четверть исходного
пикселя около band cuts и области за округлёнными исходными outer endpoints
учтены отдельно. Это не обещание битового равенства полного DOS raster.
В этих poses original cells не объединены. Расширять N07 merge на LIGHT-геометрию
без нового доказательства нельзя: source depth интерполируется по исходной клетке.

Ошибки, capacity refusals, источник времени и искусственные phase/frame walks
должны оставаться в итоговых отчётах. Неуспешный runtime не является приёмкой.
