# N21P C21: независимая проверка переноса PRE visited count

30 октября 2026 года. **Static dependency/value/lifetime и frozen equality
PASS.** Продукт и постоянные инструменты не редактировались; эмуляторы,
повторные host/native raster suites и cache models не запускались.
Runtime/performance acceptance этим аудитором не заявлен.

`research/evidence/nds/n21p/resource-audit/C21-review.json` закрепляет18входов,
`review_c21.py` воспроизводит whole-file/hash/order assertions. Reviewed
snapshot — `resource-audit/c21-snapshot/`; база `pre-map-count` совпала
с frozen C20 runtime source. Main/header/raster совпадают byteexact.

Из prepare удалён только PRE count loop; private descriptor word34 временно0.
В commit после late snapshot clean тот же byte predicate `!=0` суммируется
по тому же PRE array и записывается в private pending_probe[34]. Обратное
преобразование восстанавливает **весь** C20 runtime.c побайтово.

Прежний bound был control.width×length на входе prepare. Control step
не меняет dimensions; descriptor words8/9 содержат именно эти width/length.
В допустимом мире каждая сторона≤64, bound≤4096; uint32 accumulator и
последовательность чтения n=0..count−1 те же. Во время submit/wait/DMA/GPU/HUD
нет второго prepare/load/reset или изменения private descriptor dimensions.

PRE visited_snapshot скопирован до source class1 side effect и остаётся
неизменным до commit. POST visited/world visited — отдельные arrays;
поздний count их не использует. Five cache cleans C20 сохраняются перед
новым чтением; shared cacheline metadata и odd publication lifetime те же.
ARM7/audio, GPU payloads и HUD не получают этот snapshot для записи.

Private word34 нигде не читается до commit, не публикуется в глобальный probe
с нулевым placeholder. Count записывается до global descriptor memcpy,
global flush и even sequence. Idle/no-redraw по-прежнему сохраняет последний
display descriptor; PRE/POST, pixels, class1 updates и DMA guards не меняются.
Не добавлены state, layout, API, cache или world mutation.

Profile6 остаётся суммой prepare/live и commit CPU spans; profile7 и main28
учитывают count один раз в новой стадии. Работа не удалена — перенесена за
границу presentation. Поэтому cadence обязан учитывать post-boundary tail
следующего интервала; CPU stage reduction до wait не является бюджетным PASS.

`C21-frozen-review.json` отдельно подтверждает equality reviewed/current
с frozen manifest для6source dependencies и actual ROM/ELF:
beffba6bac637af2b3dfe9b6e9b0da84c9bf4bbfb8f96525dcdcfd0bdc150635;
9e3d514bb1964b7506f4f09c845c69029cde2a8ec998e113a5e22c1eb7cc14ea.

`C21-ARM-review.json` отдельно проверяет actual object section bytes:
один и тот же пятиинструкционный ldrb/cmp/addne/cmp/bne loop переместился
из prepare в commit, исчезнув из старого места. Accumulator setup вне loop.
Prepare code4304→4244байт, stack656→648; commit part520→576байт,
stack48без изменений. Object/disassembly/stack hashes закреплены;
из этих данных не делается timing claim.

C20 historical equality сохранён отдельно в `C20-frozen-review.json`:
4reviewed snapshot sources и реальные ROM/ELF совпали с frozen20manifest.
Текущий C21 runtime намеренно не использовался для перепроверки C20 snapshot.
Это source gate: C20 focus остался budgetFAIL, его correctness не выдаётся
за performance acceptance. Прежние C18/C19/C20 reviews не переписывались.
