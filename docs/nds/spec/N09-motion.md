# N09: движение и представление камеры

Независимое strict C11 ядро NDS, 29 октября 2026 года. Архитектура согласована
до реализации. Код GBA не использован: его намеренное устранение поступательной
инерции и фиксированная сетка обновлений не являются поведением PKR/PKE.
Проверки и статус задачи — [паспорт N09](../tasks/N09.md).

## Источники и адресация

Образцы: PKR.EXE RU, SHA-256
`12f78afc0eba05f76b5dc0bb99da38d7daa20585024c0ba958fe828aa3864d28`,
PKE.EXE EN, SHA-256
`cce7bc5993d0734ad87e93db37a70b8b52faedfff14ac48d6e317e5ac996a37b`.
Оба SHA проверены перед чтением сохранённых экспортов. Первичный ASM:
`research/evidence/movement/{ru,en}/evidence.asm`; автоматический C находится
рядом отдельно и в движок не включён. Доказательства нового исполнения
оригиналов — `research/evidence/nds/n09/`.

Ниже — **NE-loader Ghidra segment:offset**, не файловые смещения, не
runtime-селекторы DPMI и не вставки WASDify.

| Роль | RU | EN |
|---|---|---|
| Base constructor | `1068:09C5` | `1020:0EC9` |
| Hero constructor | `1068:1657` | `1020:1B61` |
| Hero callback | `1068:2127` | `1020:26D7` |
| Scalar movement update | `1068:0A85` | `1020:0F8F` |
| Three-substep wrapper | `1068:0EB1` | `1020:13BB` |
| Trigonometric candidate | `1098:0E13` | `1030:0EF7` |
| Original collision boundary | `1098:0901` | `1030:09E5` |
| Q15 sin/cos tables | `1180:0000/05A4` | `10C0:0000/05A4` |

Native far-call передаёт actor offset/selector через 16-битный стек,
`LES ES:BX` читает поля, `DS=SS`. Stopwatch возвращает ticks в DX:AX.
Новая C-модель хранит обычные ссылки и отдельные значения; DOS pointers,
vtable и упакованный actor ABI не переносит.

## Владение и границы модулей

[motion.h](../../../ports/nds/include/dok/motion.h) и
[motion.c](../../../ports/nds/core/motion.c) содержат только scalar native
state и вычисление одного обновления. Caller владеет `dok_motion_state`,
services и report. Память не выделяется; clock, scheduler и GPU не вызываются.
`elapsed` — явный uint32 аргумент, а не кадр, не число шагов и не глобальный
период всей логики. Командные значения берутся из
[input.h](../../../ports/nds/include/dok/input.h); opposing commands
не нейтрализуются.

`dok_motion_calculate` возвращает отдельный candidate scalar state, factor и
выбранные movement flags. Он не читает тригонометрическую таблицу и допускает
повреждённый signed WORD heading для isolated arithmetic vectors.
`dok_motion_update` использует тот же расчёт, заранее проверяет все нужные
табличные индексы и только после этого выполняет ordered callbacks.
Аргументы, отсутствующие services и небезопасный индекс отвергаются до первого
movement callback; state и report сохраняются при отказе.

Trig service заимствует неизменную проверенную таблицу N07. Индекс вычисляется
как native signed WORD heading, для strafe прибавляется 90; затем выполняются
ровно одна коррекция `>=360` и одна `<0`. Полное modulo 360 для повреждённого
heading не подменяет исходную арифметику. Если результат остаётся за пределами
таблицы 0…360, NDS возвращает `INVALID_STATE` вместо out-of-bounds read.

Movement callback получает old XYZ, candidate X/Z, signed quarter distance,
strafe flag и ordinal 0…11. Он возвращает принятые X/Z. Столкновение может
принять прежнюю позицию: это не прекращает оставшиеся подшаги. Callback не
должен читать неопубликованный candidate через caller state или рекурсивно
обновлять движение. В N10 именно эта граница получит исходные collision,
occupancy и door-counter эффекты. Пока явный `dok_motion_free_flight`
принимает candidate и является **только диагностикой до N10**.

## Сохранённая исходная арифметика

Actor offsets исходного объекта: flags `+1C`, XYZ `+26/+2A/+2E`, heading `+32`,
angle parameter `+34`, speed `+38`, distance `+3C`, turn/linear step
`+7C/+7E`, last turn `+82`, accumulators `+90/+94`, acceleration sign `+98`,
remembered flags `+9A`. Это объяснение источника, не layout нового C struct.

1. Исходные flags копируются, actor flags очищаются маской `FBE7DFC3`.
   При любом поступательном вводе acceleration sign становится +1 и
   remembered flags заменяются целиком; без ввода sign становится −1.
2. Movement accumulator получает signed low32 `sign*elapsed`, затем
   ограничивается 0…10000. Turn accumulator получает low32 elapsed и имеет
   только исходное верхнее ограничение 10000. Signed wrap не заменяется
   насыщением до вычисления этих clamps.
3. Factor — unsigned `((elapsed<<8) mod2^32)/416`. При повороте одновременно
   с forward/back оба accumulators синхронизируются по большему, factor
   делится SAR1. Затем signed clamp 256…768, Run удваивает, slow-effect
   `04000000` делит SAR1.
4. Turn step: low32 `angle_parameter*factor*turn_accumulator`, signed IDIV10000,
   SAR8, signed WORD, только верхний clamp 18. При смене знака поворота первый
   step принудительно 1. Без поворота accumulator и turn step сбрасываются.
   Положительное направление (DS Y, поворот влево) прибавляет угол,
   отрицательное (DS A, вправо) вычитает. Heading — signed WORD с одной
   исходной коррекцией на 360.
5. Linear step: SAR8 от low32 `speed*factor`, затем low32 умножение на
   movement accumulator, IDIV10000 и signed WORD. При step≤0 обнуляются
   step и movement accumulator. При отпускании используются remembered
   movement flags и сохраняется исходное докатывание.
6. Команды исполняются в порядке forward, backward, strafe left, strafe right.
   Для forward/left полный signed distance отрицателен, для backward/right
   положителен. Каждый wrapper выполняет **три** подшага
   `sign_extend(SAR16(full_distance,2))`. X прибавляет SAR15 low32
   `sin[index]*quarter_distance`, Z вычитает SAR15 low32
   `cos[index]*quarter_distance`. Y не изменяется.
7. После трёх подшагов source wrapper `RU1068:108E…1096` восстанавливает
   `+3C` в **полный signed WORD аргумент**, а не оставляет quarter distance.
   Итоговый moved flag сравнивает начальные/конечные X/Z, а не число callbacks.

Low32 IMUL/ADD, signed WORD, SAR и signed IDIV выражены явно без signed
overflow C и без предположения об арифметическом `>>` отрицательного значения.
Противоположные команды сохраняют последовательные подшаги: например, step 6
при heading 90 даёт forward −6 world units, backward +3, итог −3. Это исходная
асимметрия отрицательного quarter-step, а не ошибка нейтрализации ввода.
Диагональ не нормализуется. Прыжка/вертикального интегратора в этих функциях нет.

Base constructor обнуляет distance, шаги и оба accumulators, задаёт sign 1 и
last-turn 0. Поле remembered flags `+9A` он не инициализирует. NDS явно задаёт
его нулём: безопасная адаптация неиспользуемого состояния до первой команды.

## Timeline и независимость от render FPS

Hero constructor регистрирует период 416 native ticks. Его base stopwatch
инициализирован остановленным с elapsed 0; `start` в constructor отсутствует.
Поэтому **первое обновление получает elapsed 0**. На выходе movement callback
stopwatch запускается заново, и следующее elapsed исключает стоимость самого
предыдущего движения. Scheduler отдельно назначает deadline от post-callback
clock, как принято в [N08-time](N08-time.md).

Caller обязан передавать это elapsed ровно один раз на фактически вызванный
hero callback. Модуль не вводит fixed grid, не догоняет пропущенные периоды и
не запускает callback ради presentation. Одинаковая service/input timeline
даёт одинаковую симуляцию независимо от количества render observations;
две разные service timelines исходно могут дать разные результаты. Проверки
30/60 FPS не должны скрывать это различие за новым фиксированным timestep.

## Чистая камера и намеренная адаптация

[camera.h](../../../ports/nds/include/dok/camera.h) и
[camera.c](../../../ports/nds/core/camera.c) отделяют presentation от игрока.
Caller хранит две committed snapshots с N08 world ticks. Каждая публикация
следует после завершённого motion update. При первом observation скорости
нет; повтор tick заменяет обе snapshots и сбрасывает prediction velocity.
Unsigned tick wrap поддерживается, но backward interval или interval≥2³¹
отвергается. При новом epoch, teleport/respawn нужен явный reset; это
обязанность владельца мира. Heading 360 сохраняется в simulation и
представляется камерой как 0.

`dok_camera_predict` берёт фактическое смещение между последними двумя
committed poses и экстраполирует от current pose на
`min(now-current.tick,horizon)/last_interval`. Допустимый horizon — 0…416 ticks.
Угол проходит кратчайшую дугу через 359/0. XYZ остаются world int32, угол —
Q16 degrees. Все промежуточные вычисления — int64; выход за int32 возвращает
`LIMIT_REACHED` без публикации. Не вызываются motion, collision, clock,
callback, input или scheduler, не меняются snapshots и simulation.
Предсказание на основе предыдущего смещения может потребовать коррекции на
новом callback; это намеренная плавность presentation, не свойство DOS.

`dok_camera_interpolate` даёт чистую интерполяцию двух committed poses
с alpha 0…65536. Она оставлена N10 для консервативного представления возле
стен. N09 free-flight prediction не доказывает корректность будущей камеры
при столкновении: N10 должен ограничить её или сбросить при discontinuity.
Пауза замораживает N08 world time, поэтому prediction не добавляет menu time.

Принятый N07 API принимает целые градусы. N09 adapter использует
`dok_camera_heading`: ближайший целый modulo 360, ошибка не больше 0,5°.
Q16 сохраняется в camera value для будущего использования; исходный gather,
тригонометрическая таблица и backend N07 из-за N09 не меняются.

## Проверки и границы доказательства

Общий [motion_cases.c](../../../ports/nds/tests/motion_cases.c) исполняется
на host и ARM9. Проверяет исходную инерцию, первый elapsed 0, opposing drift,
смену знака поворота, Run/slow, все 360 headings и ordered substeps,
принятие/отказ движения, fail-closed preflight, table endpoint 360,
clock wrap, bounded prediction, shortest-arc interpolation, overflow refusal,
discontinuity reset и неизменность state/callback count при 864 дополнительных
presentation observations. Host wrapper —
[test_motion.c](../../../ports/nds/tests/test_motion.c).

`motion_dump.c` — отдельный wire probe для проверки scalar state и каждой
candidate X/Z/distance/strafe по исполнению исходных PKR/PKE инструкций.
Original oracle [N09-evidence](N09-evidence.md) выполнил по **9960 isolated
vectors и 106 последовательных вызовов на каждый язык**. Новый host probe
совпал по всем 17 scalar fields и каждому из 29802 isolated +285 sequence
подшагов. Оригинальный wrapper действительно исполнялся; collision принимал
candidate, clock был перехвачен, карты occupancy контролировались fixture.
Это не полный запуск DOS-игры или проверка её коллизий. SHA оригиналов до/после
совпали. Первичный отчёт — `research/evidence/nds/n09/original-motion.json`.
Новый C-код сам по себе не является доказательством совпадения с DOS.
Общий host suite: **19001 checks, failures 0**, включая 864 дополнительных
presentation observations с неизменными motion state и callback count.

Дополнительный общий [movement_replay.c](../../../ports/nds/tests/movement_replay.c)
связывает реальные новые input buffer → controls → N08 clock/events → motion →
camera. Caller предоставляет active event bank и typed workspace **73520 байт**
в scratch; скрытого bank или большого массива на стеке нет. Workspace содержит
наблюдательную копию endpoint bank и baseline trace, не второй активный scheduler.
Host runner — [test_movement_replay.c](../../../ports/nds/tests/test_movement_replay.c).

Три прогона используют одну явную последовательность **31 физического sample и
128 service calls**, с 0/30/60 presentation observations. Fake monotonic counter
имеет частоту `1193182/119`, поэтому один counter count точно равен одному
native tick. Hero event по-прежнему 416 ticks; service samples в этом тесте
каждые 80 ticks — **инъецированная проверочная timeline**, не новый runtime cadence
и не изменение native scheduler. В каждом прогоне 20 hero callbacks. После
каждого service полностью сравниваются pointer-free trace с motion state,
обеими camera snapshots, native event owner, commands, clock tick/counter,
stopwatch и input state. На endpoint дополнительно сравниваются весь event bank,
semantic scheduler fields, clock tick/fraction/counter, input buffer и controls.
Поля указателей scheduler config не сравниваются; они не являются состоянием
игрового мира. Из input buffer сравниваются metadata и активные sample fields,
не неопределённый padding или мёртвые slot bytes.

Отдельные точные проверки подтверждают timeline: первый callback заканчивается
на world tick 487, elapsed 0, следующий deadline 903; второй заканчивается на 968,
elapsed 473 и deadline 1384. Это проверяет restart stopwatch **после** семи ticks
первого movement work. Menu занимает 500 counter ticks и не продвигает world time:
post-menu callback имеет counter 3848, world tick 3348 и elapsed 453. Pause callbacks 0.
Короткий Fire сохраняется, всего Fire pulses 4; Use pulses 3. Один из трёх старых
pending Uses намеренно отменён UI boundary до обработки, свежий post-menu X
исполняется. Held R/X/A после возврата блокируются до release, action leaks 0.
Перед каждым callback command mask `00182A3C` заменяется свежим native input.
Use передаётся только как потреблённый `use_triggered` после **отпускания**
X/B: исходный IRQ reader имеет held-state 3, который не запускает Use,
и state 2 после break, потребляемый чтением. Само удержание X/B не повторяет
native Use. Mapping raw held сам по себе ещё не является исполнением команды.
N11 должен сохранить эту границу и не восстанавливать press/held-repeat из
raw button levels. Короткий press/release до service сохраняет один trigger;
две раздельные последовательности сохраняют два.
Дополнительные 30/60 camera observations не вызывают callbacks и не меняют
motion/camera snapshots. Результат host integration: **472 checks, failures 0**;
trace hash 3237372460, endpoint hash 2034872508, final pose
`(15090,−300,22821), heading 88`, movement accumulator 471, turn accumulator 0.
Hashes — компактные указатели на контроль, а не замена полного сравнения trace.

Конкретные результаты original oracle, ARM9 runtime, manifests, SHA и команды
вносятся в паспорт после полного gate. Диагностика не выполняет DOS/DPMI,
полные взаимодействия героя, AI или исходные collision/occupancy эффекты.
Реальная DS/DS Lite остаётся на N24 по пользовательской политике проверки.
