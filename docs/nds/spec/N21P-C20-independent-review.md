# N21P C20: независимая проверка переноса snapshot cache clean

29 октября 2026 года. **Static lifetime/order PASS.** Продукт и постоянные
инструменты не изменялись; эмуляторы и повторные host/native raster suites
этим аудитором не запускались. Frozen equality и runtime acceptance —
отдельные проверки.

`research/evidence/nds/n21p/resource-audit/C20-review.json` закрепляет13входов;
`review_c20.py` воспроизводит exact movement и source/hash/order assertions.
Reviewed snapshot — `resource-audit/c20-snapshot/`, база — `pre-map-flush`.

## Единственное изменение

Ровно пять прежних DC_FlushRange, всего22016байт:
planes12288, POST visited4096, PRE visited4096, glyphs1024 и palette512,
переносятся из prepare reason block в commit pending_reason block,
сразу после `phase=cpuGetTiming()` перед counters/pending descriptor updates.
Обратное перемещение восстанавливает **весь runtime.c** побайтово.
Main, runtime header и raster byteexact совпали с базой.

Pixel cache clean остаётся перед map DMA. PRE/POST copies, raster, source
class1 side effect на каждом world frame, pending data, error branches,
input latency counters, probes и sequence operations не меняются.
Нет нового API, state, cache, buffer или изменения памяти.

## Lease и публикация

Map DMA потребляет только pixels, чья coherency preparation не перенесена.
Пять массивов — CPU snapshot для диагностического reader; GPU DMA не читает
их, ARM7 audio IPC не получает их адреса. CPU raster использует ту же ARM9
cache и завершается до wait. Palette захватывается до wait как прежде;
переносится её cache clean, а не момент capture.

Planes находятся в IO offsets49152..61439, POST visited —45056..49151,
map pixels —65536и далее. PRE visited/glyphs/palette находятся в persistent
map runtime scene tail; HUD mirror расположен перед runtime и ограничен
существующим static assert. Audio backing и GPU payload pools — другие
leases. Ни main submit/wait, ни GPU commit, ни HUD update не заменяют и не
мутируют эти snapshot arrays. Нового borrow через shared scratch нет.

`nds_map_begin` flush'ит odd sequence **до** snapshot mutation. Точный main
порядок: prepare → submit/wait → actual map DMA FIRST → GPU commit → HUD →
map commit/new five cleans → counters/descriptor publication → global probe
flush → even sequence flush. Private pending_probe может быть собран раньше,
но не является опубликованным descriptor до copy в global probe. Reader
не должен принимать odd snapshot; данные очищены до even publication.

Cacheline overlap с соседними runtime fields не создаёт нового alias/lifetime:
clean-and-invalidate сохраняет весь dirty line, последующие CPU metadata
writes и прежние probe flush идут под тем же odd interval. Idle reason0
не меняет пять cached display arrays, поэтому clean по-прежнему нужен только
при actual redraw. Public update fallback также выполняет transfer перед
новым clean и публикацией. Reentrant prepare/reset между prepare и commit
по-прежнему не входит в существующую call discipline.

## Честный учёт и предел вывода

Profile6 уже является суммой prepare/live и commit publication CPU spans;
новый clean попадает в commit span ровно один раз. Profile7 складывает
prepare, transfer и commit CPU; main profile28 складывает те же три части.
Wait в эти CPU spans не добавляется. Cache clean work не исчезает: он
перемещается за presentation boundary. Post-boundary tail обязан остаться
в следующем presentation interval; полный root cadence gate это проверяет.

Static proof не моделирует ARM cache hardware и не доказывает ускорения.
Не запускались бесполезные повторения неизменного raster/native oracle
или host cache model, который не может подтвердить реальную ARM coherency.
Root должен проверить focus и весь набор по actual LCD pixels, snapshot
readers, strict map/GPU DMA windows и cadence, включая post-boundary tail.
