# N11: SDK shell, порядок сервисов и observation ABI

Реализация — [main.c](../../../ports/nds/platform/interactions_probe/main.c),
контракт наблюдения —
[interactions_probe.h](../../../ports/nds/platform/interactions_probe/interactions_probe.h).
Это конечная диагностическая последовательность из шести независимых fixtures.
Она использует настоящий SDK input adapter, N08 clock/queue, N09 controls/motion,
N10 collision, N11 interactions и аппаратный renderer. Это ещё не игровой UI,
полное прохождение или автоматическая кампания. Definition modes/delays не
подменяются ради короткого теста. Подбор ключей остаётся N14, finish отдан
синхронному sink как событие; переходы уровней относятся к N22.

## Native порядок и выбранный NDS service

Свежий N11 экспорт PKR/PKE показывает outer loop: timer start → event dispatch
→ build/draw/present → raw commands → timer stop → C2 update → hero event
handler, включая Use. Следующий dispatch вызывает FCMap effect и motion героя.
PKR `1008:0DAF`, `0F8F…0FA4`, `0FA8…0FCD`, `0FD1…0FF3`; addresses — Ghidra
NE-loader, не file offsets, NE indices или DPMI runtime selectors. Полные ASM
и SHA — в `research/evidence/nds/n11/original/{ru,en}/`.

**Принятое NDS routing:** ввод опрашивается на каждом доступном VBlank service,
Use обслуживается main вне dispatch перед следующим FCMap/motion callback.
Входные samples упорядочены N09 input buffer. Каждый filtered sample поступает
в два независимых public `dok_control_latch`: actions main принимает каждый
service, motion — только в hero callback. Use из motion-копии подавляется,
чтобы одна release не выполняла действие дважды. Так частый main service не
теряет несколько коротких movement/fire presses между hero callbacks; OR-mask
не заменяет счётчики fronts. X+B releases в одном sample дают один Use;
раздельные releases остаются отдельными действиями. Удержание не повторяет Use.

Pause и диагностический A выполняют public UI boundary input buffer и обоих
latches. Старые presses/held блокируются до отпускания. A является переходом
только в ожидающей стадии; в остальных стадиях сохраняет обычную mapping.
Probe `advance_ready` позволяет harness дождаться исходного callback, нужного
для доказательства стадии. Ранний A в waiting stage поглощается UI, не меняет
позу и не считается успешной проверкой.

Source C2 начинается с4, не с фиксированных30. Перед новым dispatch применяется
`dok_interactions_cadence(old_C2, completed_sweep_ticks)` для завершённого
предыдущего неприостановленного service. Zero-duration sample не делится.
Пара sweep_begin/end образует непрерывные границы: telemetry tail после end
попадает в следующий sample. Loading/setup не подмешиваются к active samples.
Pause замораживает world clock, dispatch и C2. Первый service после resume не
применяет sample, завершённый в pause. Это явно выбранная привязка native C2 к
NDS service; частота DOS outer loop и аппаратные DS FPS этим не доказаны.

Цикл: poll → C2 предыдущего sample → action Use → N08 dispatch → prepare/submit
на каждом втором service, иначе VBlank wait → clock end и SWEEP trace. Hero
callback: motion latch take → убрать уже обслуженный Use → refresh native
command mask → HERO_BEGIN → FCMap effect → FLOOR → N09 motion через N10
transaction → post-work stopwatch → camera commit → CALLBACK. SLOW не
очищается shell перед floor: N09 захватывает flags и очищает его исходным
маскированием внутри motion calculate. Direct conveyor имеет отдельный
collision report; correction/rejection сразу сбрасывает camera history,
перед перезаписью отчёта обычными quarter steps. Cumulative counters включают
оба вида движения.

## Fixtures и стадии

В каждом case заново загружается actual RU-мир, epoch211…216; мир для RU/EN
общий. Герой и все class7 owners регистрируются до dispatch. Исходные
definitions immutable. Повторная загрузка diagnostic case не объявляется
переходом игровой кампании или переносом inventory.

| Case | Мир и объект | Последовательность |
|---|---|---|
|1|LEVEL1 a, key2, mode1, delay40000|Use без ключа → LOCKED → fixture keys2 → Use → opening/full → occupancy1 блокирует AUTO → A снимает occupancy → AUTO/delay/CLOSE → closed → A |
|2|LEVEL1 d, key0, mode1, delay40000|Use → reflected pair opening/full → occupied AUTO → A → исходное закрытие → A |
|3|LEVEL1 g, key0, mode1, delay40000|Use → vertical crop/full → occupied AUTO → A → исходное закрытие → A |
|4|LEVEL2 h, key8, mode0, delay0|LOCKED → fixture keys8 → Use/open → A вооружает ручное Use закрытия → CLOSE blocked при occupancy1 → A снимает occupancy → closed → A |
|5|LEVEL2 FC sensor f→h; conveyor; B2 finish|first f → A другой cell того же f → A исходный обычный пол → A reenter f → A actual conveyor → A B2 score=total−1 → A score=world total → FINISH → A |
|6|LEVEL3 FC sensor f→k; B2 finish|sensor first/same/out/reenter → B2 score=total−1 → score=world total → FINISH → A/COMPLETE |

Начальные свободные hero/camera poses a/d/g указаны в
[N11-render](N11-render.md). Для h используется `(7920,−300,22800)`, heading0.
L2 sensor first `(23280,−300,27120)`, same glyph `(23280,−300,27600)`;
L3 first `(27600,−300,24720)`, same `(28080,−300,24720)`. Out — исходный seed
героя. Conveyor L2 `(1200,−300,1200)`; B2 L2 `(12720,−300,3600)`, L3
`(17520,−300,9840)`. Эти poses проверяются native square footprint отдельно
от существования изображения. Сброс pose/inertia — явный diagnostic fixture,
не новый teleport mechanic.

Fixture relocation проверяет bounded clear destination, снимает прежнюю
собственную MMap клетку, ставит героя в новую и балансирует door occupancy,
если endpoint относится к двери. Foreign occupancy не стирается. Новый pose
сбрасывает camera history и hero stopwatch; следующий elapsed0. Все такие
изменения, выдача keys/score и контролируемый door count имеют отдельный
FIXTURE trace и полный snapshot. Они не подаются скрытой записью RAM harness.

AUTO сохраняет две исходные задержки и повтор при занятой двери. Ранние Use
не меняют mode на manual. Sensor stages проверяют identity объекта, а не
номер клетки: first/reenter дают route, same glyph не даёт. В FINISH_WAIT
обязательно проходит callback со score=total−1; только последующий A задаёт total.
В actual N05/runtime моделях всех трёх уровней category99_total=0, поэтому
неравный fixture равен−1, а разрешающий finish fixture равен0.
Синхронный sink считает LOCKED/FINISH; звуки и сообщения подключат N18/N19.

## Память и рендеринг

Caller-owned world union содержит live state либо startup interaction/router
tests. Render scratch и284-byte door unit workspace имеют непересекающиеся
времена жизни. Events bank расположен в принятом events slab; scene, assets,
I/O и audio reserve остаются в N03 ceilings. Runtime проверяет Main RAM mode,
static/startup, SDK headroom и аварийный reserve655360 bytes. Итоговые размеры
ELF/slabs/stack и реальные проверки сохраняет gate; успешные host размеры
не подменяют ARM9.

Renderer получает copied phase view только после simulation. Подготовка и
synchronous submit используют additive N11 adapter над неизменным N07.
Lease освобождается перед следующим dispatch. Render проверяет неизменность
motion/history, всех maps/flags/counters/door state/revision и event bank.
Camera использует принятую N10 interpolation/admission, без occupancy side
effects. Полностью открытая панель отсутствует по явной visual correction;
проходимость отдельно определяется flags того же simulation boundary.

Верх — целевой256×192 (scene154/HUD38), низ — diagnostic console. Постоянная
карта и окончательный UI не заявляются готовыми. Presentation every other
service не является доказательством реальных30FPS на DS/DS Lite; итоговый
аппаратный прогон остаётся N24.

## Read-only observation ABI

Globals: `g_nds_interaction_probe`, trace ring128 records и snapshot ring8.
ABI version1, probe460 bytes, trace692 bytes, snapshot19520 bytes. Перед
payload публикуется odd sequence и flush cacheline; затем payload flush,
even sequence и flush. Observer читает до/после sequence, проверяет ordinal
и не пишет RAM, CPU state, clocks или результаты.

Snapshot header64 bytes: identity/epoch/case/stage sequence, callback и
соответствующий следующий trace ordinal, dimensions, hero identity. Payload:
MMap/TMap/FCMap/visited, uint16 flags/counts[256], known[256], signed16
phase/extent[256], byte state/ready/marker[256]. Публикация копирует payload
напрямую в volatile ring; локального20KB snapshot на стеке нет.

Связь snapshot_ordinal/slot существует только у записи, перед которой он
только что опубликован: LEVEL_BEGIN, FIXTURE, FLOOR, CALLBACK, USE, OPEN,
CLOSE, AUTO. INPUT/RENDER/SWEEP/STAGE не повторяют последнюю ссылку. Reader
обязан обнаруживать потерю ring ordinal; переполнение не скрывается пропуском
старых доказательств.

Первый emulator run выявил недостаточность прежних четырёх snapshot slots:
producer может публиковать trace/snapshot вперёд последнего top-level probe,
а один service содержит несколько publications. Усиление до8 slots относится
к наблюдаемости, не меняет симуляцию. Reader по-прежнему строго проверяет
ordinal/trace identity каждого snapshot; успешное исполнение нового gate
должно подтвердить отсутствие потерь, размер кольца сам по себе этого не доказывает.

Trace сохраняет полный N10 motion/input/clock prefix и новые case/stage/C2/
keys/score/revision поля. HERO_BEGIN17 — before floor; FLOOR15 — после FC
effect/direct wrapper; CALLBACK2 — после обычного motion. SWEEP16 содержит
использованный C2 и завершённые boundaries. `advance_ready` относится к
diagnostic state, не к source door.ready.

`native_valid` бывает только у USE/OPEN/CLOSE/AUTO и sensor FLOOR. Массив
input[17] совпадает с isolated interaction_dump transport, before/after[18]
содержат scalar door state и raw queue. Callback queue наблюдается до N08
post-dispatch compaction; `native_keep` фиксирует pending cancellation,
deadlines сохранены отдельно. Observer сравнивает native scalar effects и
queue requests с учётом этого состояния, не приравнивает raw count к уже
уплотнённой очереди. Для USE outer hero handler всегда снимает Use bit после
target handler; для sensor FLOOR source event field остаётся исходным и
не выдумывается локальный consumed value, которого API не возвращает.

Окончательные команды `check_interactions.py`, `build_interactions.ps1`,
`run_interactions_emulator.py`, source/build pins и runtime evidence фиксируются
в N11 passport/handoff после исполнения. Данный документ описывает контракт;
сам по себе он не подтверждает прохождение emulator gate.
