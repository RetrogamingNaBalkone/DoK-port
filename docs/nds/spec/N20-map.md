# N20: постоянная карта и независимые touch-жесты

Статус: **готово (эмулятор)**, 29 октября 2026 года. Source/host, независимые
полные RU/EN runtime и визуальные проверки PASS. Общий performance budget
не пройден и остаётся N21P; физическая DS/DS Lite — N24.

Наследуются N19 HUD, private UI presentation descriptor, N18 ARM7 audio,
N17P live gameplay и аппаратный 3D. GBA/SDL2 код и статусы не меняются.

## Экран и координаты

Нижний BG3 SUB — INDEX8 256×192 в VRAM C. Карта занимает y0…159; кнопки
занимают y160…191: «−» x0…84, «+» x85…169, «Центр» x170…255.
Палитра — исходная GAMEPn, RGB6→RGB5 сдвигом вправо на1, без LIGHT и99% fade
верхней сцены. FONT8 берётся из принятого N19 UI bank; кнопки показывают
захват касания сменой фона. EN-подпись — Center.

Шкала2/3/4/6/8 px на клетку, начальная3. В соответствии с DOS горизонтальная
ось обращена: правая граница клетки x находится на `(width-x)*scale`,
верхняя граница строки z — `z*scale`. Controller хранит точку мира в центре
окна в Q16 клеток. Renderer получает целочисленные смещения плана
`offset_x=-left`, `offset_z=-top`, где
`left=floor(center_x_q16*scale/65536)-128`,
`top=floor(center_z_q16*scale/65536)-80`.

Маленькая карта центрирована по каждой оси независимо. Для большей карты
центр ограничен `[viewport/(2*scale), cells-viewport/(2*scale)]` с округлением
внутрь допустимого интервала Q16. Масштабирование сохраняет центр мира,
после чего применяет границы. Начальное следование и «Центр» располагают
центр на native cell coordinate героя; после drag следование выключено.
При zoom в follow mode сохраняется смещение центра относительно героя;
дальнейшее движение героя переносит этот центр, а «Центр» сбрасывает смещение.
Это сохраняет anchor и при переходе из старого clamp в допустимую середину
нового масштаба. Ранний вариант с немедленным центрированием героя после zoom
отвергнут независимой root anchor-проверкой.

## Жесты и пауза

Первое касание только устанавливает новую предыдущую координату. Drag
сдвигает центр на `-delta*65536/scale`; карта движется за стилусом. Кнопка
захватывает жест до отпускания, выполняет один шаг и не превращается в drag
при переходе стилуса в область карты. Крайние масштабы не переполняются.
Выход координаты за экран отменяет жест до отпускания. Пауза также отменяет
жест, и удерживаемый при возврате стилус требует полного отпускания.

Touch никогда не поступает в gameplay command word. Start обрабатывается
вне dispatch через принятые `dok_world_clock_pause/resume`. На паузе scheduler
не запускается, latch очищается, игровые кнопки поглощаются до полного
отпускания. ARM7 audio продолжает звучание по принятой N18 modal semantics.
После resume часы получают новую платформенную базу без добавления времени
паузы. Полное меню паузы и настройки остаются N21.

## Владение памятью

`dok_asset_session` хранит reader и scalar/hash поля, без указателя на catalog
workspace; `dok_assets_open` использует workspace только синхронно. После open
union ProbeIO уже занят packet. Максимальный packet64 cases равен34880 B,
текущий63 cases —34816 B. Map staging49152 B размещён в том же I/O slab по
offset65536…114687 и не пересекается с packet. Post-paint visited4096 B
размещён по offset45056, три map-plane snapshots12288 B — по offset49152.
Эти снимки остаются неизменными в течение следующей simulation/3D preparation;
нижний observer не заимствует изменяемые карты мира. UI bank/private descriptor
остаются после114688. В игровом пути повторного catalog open/verify workspace
нет; payload load использует локальные bounded buffers.

Persistent `NdsMapRuntime`, включая pre-paint visited4096 B, расположен в scene
tail сразу после принятого HUD mirror9728 B. Статические assertions проверяют
все размещения. Временная
console на нижнем экране исключена из map-сборки. Резерв655360 B не расходуется.
Финальные RU/EN: static348524/348396 B, startup520192 B, managed2293760 B,
SDK200704 B, reserve655360 B, closure3670016 B. Сверх обязательного SDK
minimum196608 остаётся только4096 B. `NdsMapRuntime` занимает5820 B после
offset247904, оставляя8420 B scene tail; UI runtime12548 B начинается
с I/O offset114688. Любое дальнейшее размещение требует проверки alignment
и времени жизни, а не расходования reserve.

## Наблюдение без записи RAM

`g_nds_map_probe[64]` имеет magic0x20504B44, version1 и seqlock в word2.
Он становится нечётным перед записью snapshot/metadata/mirror и чётным только
после DMA и flush всех заимствованных данных. Во время загрузки мира также
нечётный. Первая версия держала его нечётным в течение всего 3D preparation,
из-за чего VBlank observer не мог получить стабильное наблюдение. Этот
отвергнутый вариант не является принятым runtime; исправление использует
полные persistent snapshots и короткую publication lifetime, как N19 HUD.

| Word | Содержание |
|---|---|
| 0…2 | magic, version, seqlock |
| 3…9 | level, epoch, frame, case ordinal, case ID, width, length |
| 10…13 | scale, follow, center X/Z Q16 клеток |
| 14…18 | capture0none/1map/2minus/3plus/4center, held, touch X/Y, paused |
| 19…22 | source ticks, герой X/Z в world units, heading |
| 23…29 | RAM visited, TMap, MMap, FCMap, metadata, INDEX8 mirror, palette |
| 30…37 | map_available, fault, upload bytes, updates, visited count, discovery records, last input bus ticks, max latency bus ticks |
| 38…42 | AI/combat/hero callbacks, world serial, VRAM memcmp count |
| 43…49 | actual camera X/Z/heading, pause/resume transitions, gesture starts, button actions |
| 50…54 | left/top integer px, drag count, discovery marked, blocked until release |
| 55…57 | stable post-paint visited, число hero visited writes, live world visited (диагностический адрес без snapshot lifetime) |
| 58…59 | follow offset X/Z Q16 |

Все адреса — ARM9 runtime RAM, не file offsets. Palette —256 little-endian
RGB555 WORD. Metadata —256 `dok_map_glyph`, stride4: uint16 class_id,
uint8 marker, uint8 monster_state; state4 скрывает corpse. Door marker берётся
из принятого `dok_interactions_door.marker`, сохраняющего native NOT AL.

Исходный automap выставляет visited для MMap class1 во втором проходе, уже
после заливки FCMap. Поэтому render получает pre-paint visited snapshot;
после paint runtime сохраняет class1 writes в мир, а отдельный post-paint
snapshot фиксирует результат. Немедленно помечать героя до первого прохода
нельзя: это меняет первый кадр пола. Более частые map calls — явная адаптация
постоянной NDS-карты вместо DOS modal-карты.

## Раскрытие по source records и исправления

Disclosure восстанавливается отдельным `map_discovery.c` до DS guard band/GPU
clipping. Native render record хранит cell WORD в+4, flags BYTE в+6 и sort key
DWORD в+7. Посещение записывается только для bit0 после исходного gap-sort и
320-column coverage; bit1 задаёт occluder. Одно попадание в N07 gather/frustum
не раскрывает клетку. Horizontal records сохраняют source OR8; paired
floor/ceiling — один record. Двери имеют собственные source flags и не
подменяются остановкой raycast на закрытой двери.

Discovery имеет отдельный exact native gather с исходным overwritten-p1 max;
расширенный исправленный N07 visual gather не служит disclosure proof.
Входные world sprites берутся до synthetic fixture и combat projectile append.
Временный workspace35268 B заимствует общий scratch только синхронно, до
weapon overlay; persistent state на него не ссылается.

Явно исправляется native projectile corner leak: spawn PKR1068:2681 ставит
flags1000, а global object pass PKR1098:52B7…5335 берёт LOW WORD X/Z и ограничивает
их0…63 без деления на CellSide. Видимый снаряд в обычных world coordinates
мог пометить visited4095. NDS projectile pool не имеет исходной world-cell
provenance; он не выдаётся за клетку и не раскрывает этот угол. Это отдельное
исправление ошибки оригинала, одобренное root в рамках критерия скрытых
неизвестных клеток; whole-session disclosure equivalence не заявляется.

## Повтор и текущие проверки

```powershell
py -3.14 -B -X utf8 ports/nds/tools/check_map.py prepare
py -3.14 -B -X utf8 ports/nds/tools/check_map.py host
py -3.14 -B -X utf8 ports/nds/tools/check_map.py final-release-ru
py -3.14 -B -X utf8 ports/nds/tools/check_map.py final-release-en
```

Python3.14.6, Zig0.13.0, BlocksDS1.24.0 и общий GCC16.2.0; пакеты сверяются с
`ports/nds/toolchain_versions.json`. Runtime запускает root отдельно.

Controller:174391 проверка. Перебраны все размеры1…64 по обеим осям,
все пять масштабов и четыре предела; отдельно capture, re-touch без скачка,
drag, pause cancellation, center, saturation;6400 assertions anchor в обоих
follow modes, движение героя после zoom и сброс offset «Центром». Проверены
выходы x−1/256,y−1/192 из map/button capture, удерживаемый re-entry,
отпускание и новое касание без скачка. Windows host
исполнен, Linux static binary только собран. Отчёт:
`research/evidence/nds/n20/host-control.json`.

Fixture содержит неизменённые62 сценария N19 и case63/source_kind29 с1600
presentations для реального input/touch. Это диагностический сценарий на
живом мире, не заявление о полном прохождении.

[Disclosure source gate](N20-discovery-evidence.md) PASS:121776 geometry ctor
vectors,280 sprite ctor,1444 native bounds,88 column cases/26042 records,
54 combined actual update cases/6078 records,5 bad-source-ID и54 negative
projectile updates. [Raster gate](N20-raster-evidence.md):952 сравнения
native x86↔C,38993920 пикселей без различий и2 packed-schema проверки.
`original_map_result` дополнительно прошёл12 focused native post-visited
проверок;954-case gate сохраняет точный исторический helper prefix.

Оба полных root прогона прошли63 cases/12880 presentations/94 real input
stages. RU:631 map states и31014912 INDEX8/RGB pixel checks каждого вида;
EN:644 states и31653888 checks. Stable post-paint visited побайтно совпал
с original x86. Наследованный HUD прошёл665 stages: RU1064 rows/10350592
pixels, EN1060 rows/10311680 pixels. Audio24 captures на язык, fault0,
FAT неизменён после unload. Визуальная проверка8 финальных PNG обоих языков,
трёх уровней и всех масштабов PASS (`visual-review.json`).

Для N21P остаются измеряемые кандидаты: повторная native-shadow генерация
geometry records, nested cell×2×sprites lookup,64-bit projection divisions,
полный map raster и49KiB DMA при неизменном снимке. Отдельно измерить
диагностические memcmp(pixels,VRAM,49152), копии стабильных snapshots и
scan visited4096 каждый frame. В N20 frozen-артефакты
после source/runtime functional gates не оптимизируются. Общий performance
budget не пройден: normal RU15.4170380252/EN15.4692930629 virtual FPS,
min window11.9652196576, p99=5VB. N19 normal около35.77/35.72 FPS;
снижение около57%. RU histogram3/4/5VB=1034/5362/241,
EN=1119/5279/239. N21P проверяет общий бюджет совместно с audio/HUD/меню.

Первый full runtime был отвергнут по настоящему title audio underrun в
case42/frame2828. Одного512-byte ARM9 service на presentation не хватало при
новой CPU-нагрузке. Исправление добавляет bounded service после shadow
discovery, перед GPU submit и после map DMA, к прежнему service на entry.
512-byte cap каждого вызова, ARM7 producer и immutable banks не меняются.
Ранние artifacts сохранены в `rejected-starvation/` и `rejected-audio-starvation/`;
связь source/ROM — `audio-service-supersession.json`.

Focused single-case title42/1800 presentations прошёл: весь live PCM110,
474361 samples, численный коэффициент0.9924103056, source coverage full,
underrun/stale/fault0. При этом steady cadence14.9565245720 virtual FPS,
все интервалы4VB: функциональная исправность не означает принятия budget.
Проверка использует real WAV, точную AVrate32728.498046875 и исходный RES,
а не только флаги ARM7. В обоих полных RU/EN прогонах повторён численный
live title gate: coefficient0.9924338014084911, exact EOS474361 и полное
покрытие исходного RES110. Отчёты `audio/title-gate.json` обоих финальных
targets обязательны для compact acceptance наряду с focused gate.

Отдельный supplemental combat+touch case63/source_kind24 на позиции принятого
case49 использует реальные weapon/key pickups и прошёл94 input stages на
том же ROM. Actual attack callbacks растут при обоих R+drag stages и после
resume, на паузе стабильны (414→419, pause420, postresume475). Этот gate
не объявляет pixel acceptance: точная карта/HUD остаются в full RU/EN runs.
Ранний empty-inventory case63 с charge0 доказывал input/AI/clock, поэтому его
R-input не выдаётся за работающий бой.

Физическая DS/DS Lite, реальные аппаратные latency/FPS/IRQ/stacks
остаются N24. Игровых сохранений и записи gameplay на microSD нет.

## Вход N21 и N21P

Start уже имеет production consumer в `nds_map_input`: он вызывает clock
pause/resume, сбрасывает control latch и поглощает удерживаемые кнопки.
Frontend N21 должен иметь **одного владельца паузы** и согласовать этот путь;
два независимых переключения от одного Start недопустимы. Touch gesture
должен оставаться отменённым до отпускания и при возврате из static screens.

Сейчас карта каждый frame восстанавливает VRAM C и palette. N21 static
screens могут менять BG3 SUB/VRAM C; возврат обязан восстанавливать bitmap
режим, палитру и карту. Будущая dirty-оптимизация N21P должна иметь явный
invalidate после frontend screens даже при неизменном world snapshot.

Уточнение пользователя во время N20: карта не требует обновления60 раз/сек.
В N21P idle-карта должна обходиться без redraw; raster и DMA — dirty/on-change.
Ориентир для динамических маркеров10–15Hz, drag/zoom — немедленно, touch polling
сохраняется каждый frame. Одного throttle raster недостаточно: отдельно
профилировать и устранять повторные native geometry/320-column producer затраты.
Текущий full N20 ROM сохраняется как измеренный baseline P-этапа.

UI language меняет только private presentation descriptor N19. Simulation
descriptor и sprite/animation bank сохраняются. Сверены146 resource pairs,
12 различий описаны в `docs/originals/resources.md`; в live art отличается
только `POPAL`:3 пикселя последнего кадра. Это доказанная граница текущего presentation rebinding,
а не автоматическая готовность полного dynamic art-switch. Настройки языка
и frontend N21 должны учитывать эту границу без изменения RU-мира/баланса.

ARM7 resident banks остаются process-lifetime immutable. Stop ACK не является
доказательством завершения декодирования для освобождения этих банков;
frontend не должен переиспользовать их память при title/menu/level переходе.
