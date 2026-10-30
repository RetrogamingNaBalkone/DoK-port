# N21P: обновление постоянной карты по изменениям

Готово в составе N21P C21 (эмулятор),30 октября2026. Итоговые runtime gates
и метрики — в [паспорте N21P](../tasks/N21P.md). C19 ограничил traversal
точным видимым диапазоном, сохранив пиксельную семантику. N20 native
x86 oracle остаётся проверкой пикселей; host-тест проверяет новую частоту,
владение снимками и побочные эффекты настоящего platform runtime.

Итоговый порядок: prepare CPU/pixel clean до SwapBuffers, bounded transfer
после VBlank перед GPU, commit audit/diagnostic clean/PRE count после GPU/HUD
до even publication. Profile учитывает собственные CPU spans без gaps.
Подробности: [range](N21P-map-range.md), [clean](N21P-map-flush.md),
[count](N21P-map-count.md), [совместная архитектура](N21P-performance.md).

## Dirty state и частота

Ввод передаётся `nds_map_input` на каждой игровой презентации. Реальное
изменение viewport от drag/zoom, подсветка/отпускание кнопки, смена языка,
загрузка мира и `nds_map_invalidate` вызывают немедленный raster/DMA.
Frontend после восстановления режима SUB bitmap и палитры обязан вызывать
invalidate: сохранённые RAM pixels не доказывают содержимое занятой меню VRAM.

Остальные изменения сравниваются точно, без хешей: активные клетки трёх
плоскостей и **PRE visited**, классы/маркеры/corpse state, координаты клетки и heading
героя, наличие карты и итоговый viewport. Они собираются до следующего
разрешённого обновления через `BUS_CLOCK/12`. При 30–60 презентациях/с это
примерно10–12 обновлений/с, без привязки к source ticks или паузе часов.
Если изменений нет, истечение периода само по себе не вызывает растеризацию,
DMA, копирование snapshot или чтение всей VRAM. Неизменный idle bitmap
остаётся на нижнем экране.

Следующий замер нашёл стоимость самого dirty detection. Его точные сравнения
теперь используют выровненные32-bit loads с GCC/Clang `__may_alias__`,
4-WORD разворачиванием, проверкой выравнивания и byte fallback/tail. Это
platform-код, не переносимый wire cast. Сначала проверяются дешёвые
hero/view/metadata отличия; при любом найденном отличии дальнейшие сравнения
не нужны для того же dirty OR. Сравнение PRE, включая долг FCMap, сохраняется.

Class1 glyph list строится при загрузке из immutable effective definitions.
Для0…4 glyphs 32-bit scan MMap пропускает группы без совпадений с помощью
has-zero-byte filter. Этот фильтр может дать соседний borrow false positive,
поэтому каждый фактический visited write дополнительно проверяет **точный
glyph byte**. Для большего числа class1 definitions либо невыровненного MMap
остаётся исходный полный traversal. Неполные WORD в конце любой карты
проверяются побайтно. Cache list/count занимает8 bytes прежнего scene tail.

ARM9 compiler output candidate5 подтверждает реальные 32-bit loads: comparator
`bytes_differ+0x22…0x3e` загружает восемь WORD и сравнивает16 bytes; встроенный
class1 scan `nds_map_update+0x222` использует LDR перед zero-byte rejection,
а фактические writes защищены LDRB/CMP. В объекте audit0 нет вызовов libc
`memcmp`. Stack usage map update968 bytes. Hashes объекта, исходников,
disassembly и `.su` — `research/evidence/nds/n21p/map-word-arm-proof.json`.
Это доказательство компиляции, замер времени остаётся отдельным runtime gate.

Исходный второй проход automap выставляет visited для MMap class1. Этот
побочный эффект выполняется на **каждой** прежней логической map update,
включая пропущенные raster/DMA. Перенос эффекта в редкий redraw изменил бы
мир; этого нет. Когда redraw выполняется, PRE snapshot снимается до class1,
POST — после. При пропуске оба опубликованных снимка остаются байт-в-байт
прежними, а live visited продолжает меняться. Классы — неизменяемые определения
уровня; двери меняют marker, AI меняет corpse state. Первый кадр уровня всегда
полностью строит metadata до class1 traversal.

Сравнение visited именно с PRE существенно: FCMap first pass последнего
bitmap использовал PRE, поэтому переход PRE0→class1 POST1 оставляет долг
одной растеризации. При следующем dynamic deadline выполняется redraw с PRE1,
и новый цвет пола появляется; затем неизменная карта снова остаётся без DMA.
Сравнение с POST ошибочно скрывало этот followup и отвергнуто при review.
Новый host fixture использует видимый class1 с FCMap marker12, отличным
от background86 и excluded. Native RU/EN x86 дают86 до и12 после followup,
ровно13 изменённых пикселей; тот же результат проверен в C runtime.

`N21P_AUDIT=0` убирает только диагностический VRAM `memcmp`; verified counter
при этом честно остаётся0. `N21P_AUDIT=1` сравнивает каждый фактический upload.
Raster, palette, DMA, discovery и class1 writes остаются настоящей работой
продукта. Динамическая память не добавляется; дополнительные scalar/descriptor
поля находятся в прежнем scene tail, проверяемом static assertion main.
Кандидат4 добавляет там же ограниченную диагностическую историю стадий:
8 записей по48 32-битных слов,1536 bytes. Она не использует heap или reserve и нужна
для чтения завершённых кадров, если observer пропустил краткую публикацию
последнего кадра. Алгоритм карты от этой истории не зависит.

## Разделение CPU preparation и scanout commit

Кандидат7 разделяет вызов на `nds_map_prepare` до ожидания VBlank и
`nds_map_commit` после него. Prepare выполняет dirty detection, snapshot,
source raster, class1 writes, POST snapshot, кнопки и очистку RAM cache.
В C8 отдельный `nds_map_transfer` выполняет только DMA сразу после VBlank,
сохраняет нечётный probe и не увеличивает committed counters. После GPU upload
и HUD `nds_map_commit` выполняет optional VRAM audit, увеличивает
uploads/updates/verified и публикует чётный descriptor. Старый
`nds_map_update` остаётся wrapper prepare→commit для host/API compatibility.

Дополнительного bitmap нет: используется прежний49152-byte mirror. Новый
pending descriptor256 B и четыре scalar поля16 B размещены в scene tail.
Probe становится odd перед фактической записью snapshots/metadata и остаётся
odd до завершения DMA/commit. CPU preparation не выдаётся за показанный кадр.
При clean update commit не делает DMA; повторный commit без preparation
ничего не делает. Контракт кадра — prepare, сохранение map mirror, ожидание
VBlank, commit.

## Probe ABI2

C8 использует тот же RAM layout: `prepared=1` означает готовый bitmap,
`prepared=2` — DMA завершён, audit/publication ещё впереди. Повторный transfer
не копирует VRAM. Commit сам вызывает transfer, если отдельного вызова не было.
Host проверяет нечётный lock и нулевые verified/updates после transfer в обоих
audit modes. Старые точные bytes сохранены в `pre-transfer-map/manifest.json`.
Profile7 суммирует только prepare+transfer+commit CPU, исключая промежуток
GPU/HUD/wait. Реальный presentation interval продолжает включать всю работу.

Magic прежний `0x20504b44`, version2, seqlock word2. Words3…59 описывают
**последний фактически показанный** bitmap с двумя исключениями: word5 — live
game frame на каждой презентации для input script; word31 — текущий fault.
PRE visited23, planes24…26, glyphs27, INDEX8 pixels28, palette29 и POST visited55
не меняются до нового raster/DMA. Hero/view/capture/left/top в этом descriptor
также относятся к показанному кадру. Их нельзя подменять текущими значениями
мира при native pixel comparison.

| Word | Назначение |
|---|---|
| 60 | RAM pointer на profile[64] |
| 61 | RAM pointer на live[48] |
| 62 | frame последнего фактического raster/DMA |
| 63 | причины последнего raster/DMA: bit0 invalidate/load, bit1 touch, bit2 language, bit3 dynamic deadline |

Live block обновляется под тем же seqlock на каждой презентации:

| Индексы | Содержание в порядке возрастания |
|---|---|
| 0…5 | frame, source ticks, level, epoch, ordinal, case ID |
| 6…12 | hero world X/Z/heading, scale, follow, center X/Z Q16 |
| 13…17 | capture, touch held/X/Y, paused |
| 18…24 | AI/combat/hero callbacks, world serial, camera X/Z/heading |
| 25…30 | pause/resume counts, gesture starts, button actions, drags, blocked |
| 31…35 | discovery records, marked, class1 visited writes, live visited pointer, live MMap pointer |
| 36…40 | follow X/Z offsets Q16, map_available, input bus ticks, max visible input latency |
| 41…47 | updates, uploaded bytes, verified, fault, current redraw reason, skipped updates, deferred dirty updates |

Live pointers34/35 — диагностические адреса изменяемого мира, без snapshot
lifetime между презентациями. Native pixel oracle использует стабильные
указатели основного displayed descriptor. Только события с визуальным
изменением создают измерение touch-to-visible latency; неподвижный stylus
не заставляет bitmap обновляться из-за движения героя в follow mode.

Profile[0…7] — cpuGetTiming units: detection/snapshot и prepare cache flush,
raster, class1 traversal, POST/buttons, фактический DMA в commit, optional
VRAM memcmp, live/probe publication, сумма CPU prepare+commit. Последнее поле
не включает ожидание VBlank между вызовами; реальные игровые интервалы
и touch-to-visible latency продолжают включать необходимое ожидание.
Пропущенные операции публикуют0; detection и class1 измеряются и при skip.
Map также записывает profile[10]/[11]: actual REG_VCOUNT непосредственно
до/после DMA; без DMA оба UINT32_MAX. Остальные profile[8…63] принадлежат
общему измерителю N21P. Profile имеет собственный цикл публикации общего
измерителя; map seqlock защищает displayed/live snapshots.

Общий измеритель публикует profile[56] как pointer на эту историю,
profile[57]=8 записей, profile[58]=192 bytes stride и profile[59] как latest
game frame. Каждая48-u32 запись содержит собственный seqlock в word1:
observer принимает одинаковое чётное значение до и после чтения. Нельзя
считать историю частью map seqlock или читать изменяемый слот без его
собственной проверки. Последняя stage record также остаётся в profile[16…63].
Общий измеритель копирует DMA start/end lines в profile[60]/[61]
(stage record words44/45); это actual register samples, не вычисленная оценка.

## Воспроизведение host-проверки

```powershell
py -3.14 -B -X utf8 ports/nds/tools/check_map_dirty.py
```

Оба режима audit0/1:16948549 checks каждый,44 фактических DMA,164 skips,
111 deferred updates; ещё два native RU/EN followup vectors. Проверяются
PRE0/POST1 первого кадра и обязательный FCMap followup, class1 writes при
неизменных опубликованных снимках,120 world samples/с с11…13 динамическими
redraw, idle без DMA, все zoom и saturation, немедленные drag/button changes,
language/invalidate, pause/held touch и сохранение чужих profile words.
Linux static build компилируется, исполнение Linux не заявлено.
Дополнительно проверены все16 сочетаний source/destination alignment modulo4,
длины0…128, отличие в каждом отдельном байте и отсутствие влияния tail за
пределом длины. Class1 fast/fallback проверены для всех4096 размеров1…64 по
обеим осям, с0…5 glyphs, включая0/1/0x80/0x81/0xff и borrow cases. Сравниваются
все active visited writes и неизменность всех inactive cells.
Ещё41 split-specific check подтверждает отсутствие DMA/изменения VRAM и
committed counters до commit, odd/even lifetime, PRE0/POST1 порядок,
реальный upload при commit и отсутствие upload при clean/repeated commit.
Искусственное ожидание между вызовами исключается из CPU stage7; actual
line fields проверены на wiring и sentinel через host device stub. Значения
VCOUNT и целостность LCD проверяет отдельный emulator runtime gate.
Отчёт с hashes исходников и executables:
`research/evidence/nds/n21p/host-dirty-map.json`.

Принятые до изменений runtime/raster файлы сохранены с hashes в
`research/evidence/nds/n21p/initial/dirty-map/manifest.json`. Это baseline,
не перезаписанный оптимизированными исходниками. Физические IRQ/DMA/LCD/touch
и субъективная оценка карты остаются N24.

Предыдущий candidate4 алгоритм до 32-bit scan также сохранён с hashes:
`research/evidence/nds/n21p/pre-word-map/manifest.json`.
Вариант до prepare/commit split сохранён с hashes в
`research/evidence/nds/n21p/pre-split-map/manifest.json`.

