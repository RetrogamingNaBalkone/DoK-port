# N21P: профиль двух экранов и интерфейса

Статус: готово (эмулятор), 30 октября 2026 года. Входы N18–N21 приняты; физические измерения и
прослушивание перенесены пользователем в N24. Эта задача не начинает N22.

Текущая итерация оптимизации — **N21P C21**, а не повтор задачи N21.
Full RU/EN C17 функционально прошли12880 presentations на язык, но один3VB
в case16 дал минимум29,42267 FPS и строгий FAIL. Повтор1066 presentations
воспроизводит этот случай. C18 offscreen checks замедлили растр и отклонены;
C19 once-per-call visible range ускорил его с4,65448 до4,03640 мс. C20
перенёс только diagnostic snapshot clean после GPU/HUD, сохранив CPU cost,
но один3VB остался. C21 переносит только diagnostic PRE visited count до
финальной чётной публикации descriptor. Его focused1066 прошёл строгий
бюджет:691 normal interval (437 по1 VBlank,254 по2 VBlank), минимум29,913049
FPS, ни одного3 VBlank и пропуска profile. Затем полная C21 матрица приняла
103 отчёта, включая дорогой audit и восемь strict DMA captures. Root final
archive verification проверил11 266 file pins; все452 current source/tool
inputs и семь frozen manifests точны. Ни один провал не исключался
переклассификацией обычного кадра или ослаблением DMA guards.

## Итоговые измерения

| Набор | Before FPS | C21 FPS | Минимальное секундное окно C21 |
|---|---:|---:|---:|
| Накопленный RU |15,514626|44,800385|29,913049|
| Накопленный EN |15,310037|44,689456|29,913049|
| Production live RU, три уровня |—|58,094290|49,855082|
| Production live EN, три уровня |—|57,903817|48,857980|

На каждый normal SCN банк —6637 измеряемых интервалов/stage samples,
6666 rendered normal frames и полный набор12 880 presentations/profiles.
Ни одного интервала>2VB; RU4995 и EN5004 секундных окон прошли. Goal60 false.
Audit функционально прошёл, его15,072343 FPS/minimum8,158104 опубликованы
отдельно. Подробные before/after distributions, all-awake/stress/loading,
GPU maxima и compiler stack frames — в полном локальном
`analysis/nds-n21p-performance.json`; Git summary —
`analysis/summaries/nds-n21p-performance.json`.

Средний normal CPU54,53/55,13→16,13/16,15 мс; map25,30/25,57→1,384/1,385 мс,
native discovery10,08/10,22→3,447 мс. Normal map updates260/6637samples,
skipped6377. Meaningful touch upper6VB/100,291 мс (42samples/bank).
Live loading upper RU10,497 с/EN9,828 с, language≤1,020/0,969 с; это отдельные
observer bounds, не physical DS timings и не steady FPS.

## Парный протокол

До функциональных изменений собраны неизменённые по поведению N21 baseline:
`build/nds/n21p/baseline-{ru,en}`, `baseline-audit-ru`,
`baseline-frontend-ru`, `baseline-fixture-ru`. Отличие от принятого N21 —
таймеры стадий в существующем scene-tail; новое heap-выделение не добавлено.
Все варианты имеют собственные ROM/ELF/map, source snapshots и build-manifest.
Исторические результаты N19/N20 являются контекстом, а не новым before-run.

Накопленный SCN-пакет — неизменённый N20:63 случая,12880 presentations,
включая HUD665 стадий,23 catalog PCM, полный титул110, живой бой и94 стадии
touch. Дополнительный бой использует принятый N20 боевой пакет. Production
frontend собирается отдельно с `GAMEPLAY_FRONTEND=1`; SCN-путь —0. Тяжёлые
guards задаются `SPRITE_AUDIT=1 N21P_AUDIT=1` и измеряются отдельно.
Нормальный renderer сохраняет все предметы, эффекты, звук и исходную точность.

Сборка: `py -3.14 -B -X utf8 ports/nds/tools/check_ui_performance.py <label>`.
В `label` окончания `ru/en` выбирают банк, `audit` включает guards,
`frontend` включает production, `fixture` — явные N21 визуальные переходы.
Общий BlocksDS1.24/GCC16.2 и пакеты закреплены в build log/manifests.
Все гостевые запуски и независимые pixel/audio/FAT/input проверки выполняет
root; эта инструкция не выдаёт частоту host/core за NDS FPS.

## Измерительный ABI

Fine geometry profiling отделён флагом `N21P_GEOMETRY_PROFILE`, по умолчанию0.
ABI1 хранит в28…31 прежние prepare/submit/GE/I/O duplicates renderer history;
ABI2 C9 хранит build/cull/merge/remainder; ABI3 diagnostic хранит
build/cull/merge/direct-door-pass. В ABI3 остаток вычисляется отдельно:
geometry − build − cull − merge − doors. Он не объявляется временем дверей.
Normal fine0 не содержит per-quad timer calls и дополнительных16 B scene fields;
общий stage profile и восемь history records сохраняются. C9 измерил около1.8ms
накладных расходов fine timers; итоговая normal cadence проверяется без них.

C8 дополняет стабильную запись:60/61 — actual map DMA VCOUNT,62 — дополнительное
ожидание boundary (также входит в41),63 — GPU startLine | endLine<<16 либо
UINT32_MAX без upload. Cold pre-render GPU preload сохраняет прежний порядок
и отмечается renderer history LOADING; обычный post-swap upload проверяется
относительно joint boundary того же кадра.

`g_nds_map_probe[60]` указывает на64 DWORD в существующем map runtime.
Слова16…63 — стабильная копия завершённой presentation, seqlock17 нечётен
только при её копировании. Читатель проверяет sequence до/после и отличает
новые кадры по20. Таймер — ARM9 `cpuGetTiming`,33513982 Гц.

| Слово | Значение |
|---|---|
|16…23|magic0x21504b44,sequence,ABI1,case,frame,ordinal,timerHz,level|
|24…31|map updates,discovery records,uploaded bytes,interval,prepare,submit,GE wait,I/O ticks|
|32…39|entry audio,input/pre-simulation,simulation,geometry,sprites whole,discovery subset of sprites,overlay,effects|
|40…47|submit including GE wait,VBlank wait,stream commit,HUD,map,post-map audio,telemetry,total CPU excluding VBlank wait|
|48…55|map snapshot/glyphs,raster,class1/POST,buttons/POST copy,flush/DMA,VRAM memcmp,publication,total map|
|56…59|history pointer,capacity8,stride192 bytes,latest global frame ID|
|60/61|actual map DMA start/end VCOUNT; UINT32_MAX без DMA|
|62|extra bounded VBlank wait ticks; входит также в41|
|63|GPU upload startLine &#124; endLine&lt;&lt;16; UINT32_MAX без upload|

Слова0…8 — текущие рабочие измерения, не стабильная завершённая копия.
Вложенные стадии нельзя суммировать дважды:37 входит в36,48…55 входят в44.
Интервал cadence берётся из принятого renderer history, а не из суммы стадий.
До/после используются одинаковые реальные маршруты и normal/stress labels.

Последний кадр дополнен восемью полными48-DWORD записями в прежнем scene tail:
1536 B внутри уже выделенного slab. Каждая запись имеет собственный seqlock
в word1; slot=`(global frame-1)%8`. Ring публикуется до stable latest descriptor.
Это позволяет читать полный профиль при нечётном map snapshot sequence,
не читая нестабильные пиксели. При загрузке уровня runtime очищается; записи
с magic0 не являются кадрами. Финальная проверка требует все normal intervals
в stage profile, cadence по-прежнему берётся из полного renderer history.
Baseline имел только latest запись и собрал12880/12880 кадров; дополнительная
финальная публикация ring включена в фактическую cadence. CPU body timing
заканчивается перед собственной публикацией профиля, поэтому её нельзя
выдавать за полную длительность presentation interval.

## Изменения, ожидающие совместной проверки

C8 исправляет найденный C7 DMA251→12 в тяжёлом case39. После VBlank main
делает только bounded map transfer, затем GPU stream, HUD и поздний map
audit/publication. Дорогой audit readback не задерживает pending textures.
Admission192…224 допускает одно ожидание следующего boundary; новый timestamp
и sampled counter заменяют старые, extra wait включён в actual cadence.
Повторная небезопасная admission приводит к fault. Для map проверяются
192≤start≤end≤262 и длительность меньше развёртки. Запас admission относится
к измеренному эмулятору и не заменяет N24.

GPU сохраняет принятый N12 VCOUNT window192…262 либо0…47; post-swap upload
дополнительно ограничен119×2130 bus ticks от нового joint boundary. Это
проверяет тот же epoch, исключая скрытый полный оборот VCOUNT. Cold pre-render
upload не переносится в этот порядок. Нормальные кадры не переименовываются
в loading. Stage44/map_total содержат сумму prepare+transfer+commit CPU,
исключая GPU/HUD/wait между этими вызовами; renderer history ABI сохранён.

Frustum C8 считает только плоскости, чей bit ещё остался в common-outside AND,
и выносит общие camera dot products. Все предикаты остаются64-битными.
604425 visibility quads полного signed32 domain и13816 полных сцен, включая
12880 actual runtime camera frames, совпали byteexact со старой реализацией.
Отдельные proof и source bridge описаны в `N21P-scene-visibility.md`.

Растер и DMA карты переходят на dirty state: неподвижное изображение не
перерисовывается, динамические изменения объединяются с ориентиром12 Гц,
drag/zoom/кнопки показываются сразу. Ввод и gameplay продолжаются каждый кадр.
Последний показанный PRE/POST снимок хранится независимо от текущего мира.
Копирование отображаемого состояния имеет собственный ограниченный seqlock;
длительная подготовка 3D не закрывает наблюдение состояния.

Отдельно ускоряется исходный producer раскрытия: transient индекс billboard
по клетке сохраняет native insertion order; exact signed32 near clipping
заменяет ненужный64-bit divide; transient cache хранит точный результат
исходного целочисленного деления. Compact cell producer вычисляет только
используемые x/z/kind/flags, сохраняя все validation branches и порядок;
неиспользуемые UV/y и полные1108-byte copies исключены. Modulo32 multiplication
и unsigned sign-fill реализуют тот же signed low32/SAR, без изменения точности.
Состав геометрии, source AABB bug, Shell sort,
320 колонок и поправка global projectile low-WORD leak не меняются.
N07 frustum/corrected bounds не подменяют native disclosure.

`N21P_AUDIT=0` исключает только диагностический VRAM readback/memcmp.
DMA и растер не исключаются из обычного бюджета; счётчики verified при
отключённой проверке не увеличиваются. Независимый observer проверяет реальный
LCD-растр. HUD не flush-ит неизменённый буфер; палитра и метаданные остаются
наблюдаемыми. Возврат из frontend принудительно инвалидирует карту и HUD.

Исторический candidate7 перенёс raster до SwapBuffers, но оставил map DMA
после GPU upload и нарушил map VBlank window в нескольких маршрутах. Он
отклонён. Текущий transfer-first порядок и отдельный admission/epoch guard
описаны выше; итоговый gate проверяет каждый перенос, включая cold frames.

Frontend больше не flush-ит98 KiB неизменного static payload на каждом loop:
top/bottom/text/palettes публикуются при draw. POPAL inspector bytes flush-ятся
после cache insertion/compaction либо изменения asset/address/span; восстановление
игры сбрасывает remembered identity. Обычная сборка не увеличивает frontend
VRAM verified counters: readback включён только явным audit флагом word38.

Существующий диагностический effect trace ring сохраняет512 записей в SCN,
поскольку накопленный замороженный reader использует этот размер. В production
frontend ring сокращён до384 записей: освобождается6656 B static memory,
чтобы сохранить SDK minimum196608 и reserve655360. Это история наблюдений,
а не очередь игровых событий: scheduler, callbacks, числа объектов/эффектов,
все expensive guards и resident audio неизменны. История оборачивается как
раньше; frontend не использует512-entry SCN reader. По одному ELF можно
восстановить размер локального массива `effect_traces`; build flags фиксируют
`NDS_EFFECT_TRACE_CAPACITY`. Статические лимиты runtime проверяются отдельно.

Независимый review отверг candidate2: сравнение dirty visited с POST могло
навсегда оставить FCMap заливку первого прохода от старого PRE. Candidate3
сравнивает PRE, выполняет один необходимый follow-up по динамическому deadline,
затем снова не рисует неподвижный экран. Реальный host runtime и native RU/EN
подтверждают13 меняющихся пикселей, в том числе86→12 у видимой class1 клетки.
Отвергнутые ROM/source snapshots и причина сохраняются отдельно.

Итоговые hashes/memory/commands собраны в [паспорте N21P](../tasks/N21P.md),
`root-acceptance.json`, `final-archive.json` и `final-review.json`.

## Текущая frozen архитектура C21

После C10 private geometry kernel строит один cell непосредственно в локальном
буфере scene. Неиспользуемый хвост private cell не очищается; публичный API
сохраняет полный zero/candidate/out-on-success контракт и error ordering.
Проверены публичные errors, poisoned tails, полные сцены и старый cache1 путь.
Объектные field54-метаданные подготавливаются отдельно для каждого неизменного
вызова scene/native по фактическому object index. Table не фиксирует runtime
flags, карты, bindings или dynamic class; alias/malformed definitions и signed
WORD semantics проверены отдельно. Scene stack2488 B против исходных2608 B;
native workspace47556 B внутри прежнего scratch131072, persistent allocation0.

StripDraw хранит четыре2-bit индекса и reuse bit в uint16 вместо пяти байтов.
Capacity2048 не менялся; ARM BSS сократился на6144 B. Renderer typed equality
сравнивает пять int32-компонентов без padding. Порядок admission и emission
доказан на actual helpers, включая AD==BD. Row-unroll C12 отвергнут: он был
byte-exact, но замедлял raster на5–6%. Тело raster возвращено к исходному memset.

Hidden→hidden overlay пропускается только после успешной отправки пустого слоя;
visible frames всегда идут полным путём, visible→hidden очищает один раз.
Загрузка уровня и возврат оболочки сбрасывают blank validity. Двухбайтовый флаг
использует прежний padding, ARM sizeof UI не вырос. Audit сохраняет полный путь.

C16 переносит hot scene/geometry/door/discovery/raster/backend helpers в штатную
ITCM SDK, не меняя их тела, API, IRQ или stack settings. У всех семи итоговых
SCN RU/EN/audit/frontend/fixture ELF C21 ITCM29160/32768 B, запас3608 B.
Исходный C16 hot set занимал28872 B; последующие изменения raster учтены в
новом per-variant guard. SDK symbols, DTCM и
межрегионные veneers проверены; product source pins точны у всех вариантов.
Небольшое размещение в быстрой памяти заменяет новый persistent cache: новые
cache invalidation/epoch journal и matrix-incremental алгоритмы не вводились.

Full C16 обнаружил предел upload окна на реальном переходе case27:216→49.
Расширенный transition-пакет воспроизвёл ту же ошибку без длинного префикса.
C17 переносит CPU cache clean GPU sources до submit, сохраняя actual DMA и
guards внутри прежнего окна. Старый public commit контракт не меняется; новый
explicit prepared pair имеет доказанный immutable lease. Profile p26 теперь
сумма preclean/postcommit CPU spans, без VBlank gap. Полные детали и проверки:
[GPU source clean](N21P-stream-clean.md). Это scheduling fix, без ослабления
deadline или классификации нормальных кадров.

C19 вычисляет видимый x-range клеток один раз на вызов, сохраняя линейный
индекс и inclusive border semantics. В FCMap проходе координаты по-прежнему
считаются после glyph/visited filter. Hero arrow остаётся отдельным footer,
включая scale2 выход за клетку. C20/C21 откладывают только диагностический
snapshot clean и PRE visited count до map/GPU/HUD, перед final descriptor/even
publication. Pixel cache clean остаётся до wait; producer, PRE/POST и растр
не сдвигаются. Полная CPU-стоимость этих участков остаётся в p6/p7.

Короткие неизменённые heavy14/15/39 прошли153/153 интервала по2VB и66 секундных
окон на29.913049 FPS. CPU case14 C15→C16:29.906→25.603 мс, geometry8.483 мс,
native discovery5.498 мс, submit5.627 мс. Сам producer остаётся точным и
работает каждый игровой кадр; частота карты ограничивает только её изображение.
Исторический short/production startup491520 B, SDK229376 B. Финальные SCN
normal/audit имеют startup495616 B/SDK225280 B; production/fixture сохраняют
491520/229376 B. Minimum196608 B, reserve655360 B соблюдены, closure3670016 B.
Это историческое short diagnostic evidence. Full C17 нашёл один3VB в case16;
focused1066 C21 устранил его без скрытия стоимости: проблемный CPU≈31,913 мс,
map_publish≈0,710 мс, а wait уменьшился с16,421 до0,361 мс. Это попадание в
прежний deadline, а не устранение вычислений. Full RU/EN,
frontend/fixture/audit matrix C21 завершилась и прошла независимо.
Цель60 FPS не достигнута, строгий минимум≈30 FPS
проверяется отдельно по всем нормальным интервалам полного набора.

Команды и доказательства изменений:
[private kernel](N21P-geometry-inplace.md),
[field54](N21P-field54-prepared.md), [strip packing](N21P-strip-packing.md),
[blank overlay](N21P-overlay-blank.md), [ITCM](N21P-hotcode-placement.md).
Frozen manifests не переписываются: ровно два report/archive Python-tools,
доработанные после сборок, имеют explicit companion с SHA обеих версий для
каждого исторического C16 target. Все C21 tools финализированы до первой RU
сборки: семь manifest требуют точного текущего source/tool совпадения без
overrides. Product/ROM/ELF также требуют полного совпадения.

Решение — продолжать с текущим аппаратным3D/ARM7/VRAM split после остановки
этой итерации, только по новому поручению N22. Полная игра ещё не проверена;
маршруты текущего прототипа не доказывают60 FPS или физическую DS.
Исторические3 metadata/7 intermediate execution references имеют отдельную
[retention policy](N21P-historical-artifact-retention.md);100% nested historical
byte retention не заявляется. Это не current source/performance waiver.

