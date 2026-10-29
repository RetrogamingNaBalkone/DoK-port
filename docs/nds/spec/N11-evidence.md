# N11: исходные взаимодействия PKR/PKE

Свежая проверка 29 октября 2026 года. Это независимое восстановление по Ghidra,
ASM и исполнению исходных инструкций. GBA-материалы служили указателями на
адреса; GBA-код, фиксированный FPS30 и виртуальная сетка времени не использованы
как архитектурная основа NDS.

## Образцы и адресная система

Все адреса ниже — **Ghidra NE loader**, не файловое смещение, номер NE-сегмента
или runtime-селектор DPMI. Вставки WASDify не являются входами оригинала.

| Образец | SHA-256 до и после анализа |
|---|---|
| `originals/pkr/RUS/PKR.EXE` | `12f78afc0eba05f76b5dc0bb99da38d7daa20585024c0ba958fe828aa3864d28` |
| `originals/pkr/ENG/PKE.EXE` | `cce7bc5993d0734ad87e93db37a70b8b52faedfff14ac48d6e317e5ac996a37b` |

Сохранённые проекты: `research/ghidra/projects/{ru,en}/Kremlin.gpr`.
Новый `tools/ghidra/ExportNdsInteractions.java` запускается с
`-readOnly -noanalysis`, проверяет provenance SHA-256 самой программы Ghidra и
сохраняет исходные сегменты, целевой ASM, автоматический C, TSV и журнал.
Драйвер `ports/nds/tools/export_interaction_evidence.ps1` проверяет EXE до и после,
сохраняет SHA-256 скрипта, драйвера и каждого бинарного сегмента в
`research/evidence/nds/n11/original/{ru,en}/analysis-manifest.json`.
Автоматический C не является редактируемым движком или проверенной сигнатурой.

| Поведение | PKR/RU | PKE/EN |
|---|---|---|
| Базовый event handler, в том числе class6 | `1068:0521` | `1020:0A01` |
| Герой: исходный Use | `1068:1B10` | `1020:20C4` |
| Проекция одной точки и TMap lookup | `1098:0309` | `1030:03ED` |
| Door ctor | `1068:30FF` | `1020:37AE` |
| Door event handler | `1068:347E` | `1020:3B33` |
| OPEN / CLOSE / AUTO | `1068:3262 / 334E / 3427` | `1020:3917 / 3A03 / 3ADC` |
| Sensor routing | `1068:091E` | `1020:0E1C` |
| Hero: FCMap до движения | `1068:2127` | `1020:26D7` |
| Stat / finish predicate | `1068:1CC2` | `1020:2276` |
| Прямое абсолютное перемещение conveyor | `1068:10A4` | `1020:15AE` |
| Настройка crop / преобразование pixels | `10F8:0592 / 05C9` | `1060:06A2 / 06D9` |
| Центральная плоскость и отражение | `10B0:0000 / 03EE` | `1040:0000 / 03FA` |
| Измеренный C2: расчёт | `1008:0FD1…0FF3` | `1008:14AE…14D0` |

## Типы, ownership и мутации

Проверены `LES`, far calls/`RETF`, caller cleanup, `CBW`, `MOVSX`, `IDIV`,
низшие WORD/DWORD и порядок `PUSH`. Event handler получает this в BP+6,
изменяемый far DWORD event pointer в BP+0A, far source в BP+0E.
Native callback получает только owner; callback ID и parameter/context в N08 —
явная адаптация указателей, а не утверждение о DOS-сигнатуре.

Дверь — общий объект glyph. Несколько клеток разделяют состояние, flags,
фазу, lock и occupancy; у каждой клетки остаётся своя плоскость с исходным
аргументом отражения. Карты принадлежат миру N05, mutable flags и door occupancy —
N10. N11 не создаёт вторую авторитетную копию этих полей.

| Native field | Проверенный смысл |
|---|---|
| `+15` WORD | Flags: bit8 при открывании; bit10h разрешает проход |
| `+1B` byte | Marker; инвертируется в конце OPEN и CLOSE |
| `+50` signed WORD | Фаза |
| `+52` signed WORD | Новый repeat interval, sign-extended DWORD |
| `+54 / +55` byte | Состояние2/1 и ready |
| `+56` signed WORD | Общая занятость; при движении арифметика WORD |
| `+58 / +5A` WORD | В этих обработчиках не используются |
| `+5C` WORD | Требуемый key mask |
| `+5E` signed WORD | Mode1=AUTO, mode0=manual close |
| `+60` signed WORD | Extent/divisor crop |
| `+66` DWORD | AUTO delay и последующая задержка CLOSE |
| `+6A` WORD | Исходные flags после полного CLOSE |

Базовый ctor `1068:02F5` регистрирует native capacity4. По принятому N08 это
**три используемых слота**: capacity−1. Door ctor копирует image header,
оставляет pixels общими, задаёт state2, ready1, extent1, occupancy0, lock0,
interval0, `+58/+5A=0`, сохраняет исходные flags. Phase0 задаёт базовый ctor
в обычном неанимированном варианте исходных дверей. Marker приходит из определения.
Неиспользуемые `+58/+5A` не получили придуманного смысла.

## Use и door state machine

Hero Use пробует одну точку на signed distance **−CellSide**: X получает
low32(sin×distance) SAR15 со знаком плюс; Z вычитает low32(cos×distance) SAR15.
Затем lookup TMap делит координаты на CellSide знаковым IDIV, сужает quotient
до WORD и clamps. Это не raycast и не поиск ближайшей стены. Hero всегда
потребляет Use после вызова target handler, включая пустую цель/no-op.

Door убирает event40000h, если flags bit8 отсутствует. Use800h обрабатывается
только при ready!=0; extent обновляется и Use очищается **до** проверки ключа.
Достаточно любого совпадения `(lock & keys)!=0`; source class13 использует FFFFh.
Ключи не расходуются. Отказ ключа вызывает исходный звук/уведомление, в NDS —
типизированный выход для последующей интеграции звука и текста.

Из state2 Use планирует OPEN через416 ticks; при mode1 также AUTO через delay.
Из state1 только mode0 планирует CLOSE через416 ticks. Ready остаётся1 до первого
callback: ранние Use действительно могут добавлять дубликаты до лимита очереди.

OPEN при state2 и phase<extent−1 выставляет flags|8, ready0, меняет repeat interval
**первого слота owner** и увеличивает phase. Проход включается строго при
`phase > trunc(extent/4)`. Следующий callback завершает переход: state1,
flags|10h, marker NOT, ready1, cancel current.

CLOSE при signed occupancy>0 ничего не меняет. Иначе при state1 и phase>0 он
уменьшает phase, ставит ready0 и repeat interval. Только следующий callback после
phase0 задаёт state2/phase0, восстанавливает первоначальные flags, marker NOT,
ready1 и отменяет текущий слот. До этого проход остаётся разрешённым.

AUTO ждёт occupancy<1, state1 и ready. Он планирует CLOSE **ещё через delay**,
затем отменяет себя. Если условие не достигнуто, AUTO остаётся с исходным period.
Ни AUTO delay, ни исходный signed interval не заменяются удобной константой.
Сама обработка N08 сохраняет уже принятую семантику первого слота, keep,
компактации и добавления callback во время sweep.

## FCMap, conveyor, sensor и finish

Hero callback сначала рассматривает MMap pickups (полная механика — N14),
затем FCMap. Class12 ставит04000000h и передаёт category `+66` и
`WORD(trunc(signedWORD(+68)/signedDWORD(C2)))` в stat path. После проверки
remove predicate, который в исходном class12 возвращает0, клетка остаётся.
Class13 вызывает свой virtual+28 только при изменении **identity объекта**, затем
hero сохраняет far pointer FCMap объекта в `+DA` и запускает обычное движение.
Конструктор героя задаёт previous FC pointer=0. Переход на соседнюю клетку того же
glyph не является повторным входом; выход на другой объект и возврат являются.

Sensor берёт target glyph из byte+66 и DWORD event из невыравненного+67;
передаёт target handler **локальную копию** event и source=this sensor.
Исходное поле sensor не меняется. Source class13 обходит door lock.
LEVEL2 f→h и LEVEL3 f→k отправляют800h; LEVEL1 sensor не имеет.
Class6 использует базовый no-op; названия «switch» недостаточно для изобретения toggle.

Conveyor category100/101 прибавляет signed WORD к X, category102 — к Z, low32.
Затем выполняется **одна** прямая wrapper-транзакция1068:10A4/1020:15AE,
не три четвертных шага обычной кинематики. При C2=4 реальные±1400 дают±350,
при30 —±46; оба варианта обязательны для source-domain и проверены.
N11 использует добавленную public `dok_collision_displace`, которая сохраняет
прежние N10 пути и их защиту конечной позиции. Изменение принятого N10 API
additive и отдельно обосновано N11; старые файлы архивированы root до изменения.

Category1000 проверяет signed WORD score **+D8**,
world total+710; при равенстве оригинал записывает health(+5E)=−1.
NDS выдаёт FINISH при том же условии. До N22 мир автоматически не переключается.
Прочие stat categories передаются типизированным выходом, полная система
характеристик и ключевые pickups относятся к N13/N14.

Slow flag не должен оставаться навсегда. В RU1068:0AA9 движение копирует flags
в локальный DWORD, затем0AB2 применяет AND FBE7DFC3 к живому полю. Расчёт движения
использует сохранённые flags, то есть только что поставленный FCMap slowbit.
Не следует очищать его между floor и motion; обычный N09 update уже его потребляет.

## C2 и порядок сервисов

C2 — изменяемый измеренный FPS, исходно4, а не константа30. Native main выполняет
scheduler dispatch(0) (RU1008:0DAF), строит и показывает кадр, читает raw commands
(0F8F…0FA4), останавливает/читает frame clock, затем обновляет C2:
unsigned DIV10000/frame_ticks, DWORD ADD с wrap, logical SHR1, signed minimum4.
Hero event handler/Use вызывается после этого; FCMap и motion происходят в следующем
scheduler sweep. Door читает только signed LOWWORD C2 и минимум8; floor использует
signed полный DWORD C2. Это два разных ABI-случая.

NDS policy задаёт C2 из фактических часов service sweep независимо от решения
показывать кадр30/60. Это явное отделение симуляции от presentation, не утверждение
о воспроизведении DOS renderer cadence. Zero frame ticks отвергается вместо DIV fault.
В source/C scalar suite проверены168 комбинаций на язык, включая overflow и оба знака.

## Crop, отражение и принятая коррекция

Native image flags48h сдвигают pixels вправо с нулевым заполнением,108h — вниз.
Shift точно равен `(dimension / extent) * phase`, сначала целочисленное деление.
Размеры image header не меняются. Нулевая область пропускается; pixels остаются
исходными. Повторный Use даже открытой двери заново берёт extent из C2, поэтому
phase может стать больше extent без повреждения состояния. В исходном
image_flags_crop_configure фаза image header ограничивается сверху extent;
само door.phase сохраняется. Закреплённый source/C vector444 (нумерация с нуля)
даёт phase58/extent59 → phase58/extent30. NDS обязан скрывать такие полностью
открытые панели по условию phase>=extent−1, а не отвергать их как неверный аргумент.
Ctor extent1/phase0 остаётся видимым закрытым состоянием. Аргумент отражения центральной плоскости независим от этого crop:
RU1098:49E7/4A77/4E5D/4EED передают его в10B0:03EE; EN1030:5405/5508/5938/5A3B —
в1040:03FA. Каждый placed panel отражается отдельно при общем состоянии glyph.

При width96/extent30/phase29 исходный shift87 оставляет9pixels. N11 явно
скрывает полностью открытую фазу extent−1: это требуемое исправление остаточной
полосы, **не** совпадение с native pixels. Промежуточные crop соответствуют исходным
pixel shift. XYZ/UV clipping с nearest rounding — адаптация к аппаратным quad NDS;
математическая связь проверена отдельно от исполнения x86 crop.

## Исполнение оригинала и границы доказательства

`ports/nds/tools/audit_interactions.py` исполняет свежие Ghidra bytes в Unicorn2.1.4.
Для door перехватываются только scheduler requests, sound/messages и ABI argument copy.
Callbacks обрабатываются по одному; исходный код door/base/sensor выполняется.
Для Use lookup и прямой wrapper использована принятая N10 instrumentation, но
исполняемые actor/world bytes загружаются из свежего N11 экспорта и побайтно
сверяются с неизменёнными N10 экспортами. GBA engine не вычисляет ожидания.

Для floor исполняется полный исходный hero FCMap path; MMap в контролируемой
позиции содержит героя, чтобы отделить N11 от будущих pickups. Сам исходный stat
path и direct resolver выполняются. Ordinary motion перехвачен после FCMap,
remove/sensor virtual callback наблюдается отдельно; routing sensor→door
независимо исполнен в door/sensor suite. Native health−1 сопоставляется с NDS FINISH,
а не выдаётся за тождество представления здоровья.

Полный отчёт — `research/evidence/nds/n11/original-interactions.json`.
Он локальный; payload и тысячи повторяющихся векторов не включаются в Git.
Все карты и256 door counters сравниваются полностью во время проверки; JSON
хранит SHA-256 MMap и только ненулевые counters.

| Независимая проверка | Число |
|---|---:|
| Native/C door, base, sensor scalar state и queue requests |14400|
| Native/C C2 scalar |336|
| Native/C Use target projection |2166|
| Native/C direct occupancy wrapper, включая±350 |768|
| Native/C floor, включая slow, conveyor, finish, sensor identity |6180|
| Контролируемое отличие N10 final-footprint guard |6|
| Exact native crop pixels |3440|
| Native paired mirror constructor vectors |16|
| NDS geometry:153panels×30phases |4590|

Все1003 placed class12 клетки на язык и10 class13 клеток LEVEL2/3 включены в source
floor cases при C2=4/30/60; дополнены unequal-score finish и repeated-identity sensor.
Итого3093 floor records на язык. Три записи на язык — искусственная позиция героя
внутри исходной закрытой двериK, LEVEL2 cell168=(40,2). Native resolver возвращает
по-прежнему blocked pose; N10 OCCUPIED guard сохраняет старую позицию и occupancy.
Причина4, maps/counters/flags отдельно проверены. Эти6 записей **не** включены в6180
совпадений. Это не допустимый маршрут героя в закрытую дверь; расхождение защиты
зафиксировано, а не скрыто пропуском. Все реально допустимые conveyor350 совпали.

`verify_live_interaction(row, original=None)` — read-only helper для ABI1 runtime:
17 inputs,18 before/after fields, raw queue и native keep. Hero outerUse consumes800h;
sensor outer source-event остаётся неизменным. Queue сравнивается до N08 compaction:
set_interval меняет первый слот, append учитывает usable capacity3, cancel проверяется
через keep. Deadlines и полные snapshots проверяет отдельный emulator harness.
Helper не пишет RAM, CPU, часы или итоговые проверки игры.

Это изолированное исполнение исходных инструкций с контролируемыми relocated far
pointers, не целая DOS-сессия. Проверки физических DS/DS Lite отложены до N24.
Смысл неиспользуемых door fields и произвольных stat side effects вне принятого scope
не объявляется восстановленным. Звуки, уведомления, automap и gameplay lifecycle
остаются дальнейшей интеграцией, а не отсутствующими исходными возможностями.

## Воспроизведение

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File ports/nds/tools/export_interaction_evidence.ps1
C:/Source/wasdify/.venv/Scripts/python.exe ports/nds/tools/audit_interactions.py --dump build/nds/n11/interaction_dump.exe --world-dump build/nds/n11/interaction_world_dump.exe --render-dump build/nds/n11/door_render_dump.exe
```

Предварительно host gate собирает все три dump из текущих N11 sources.
`interaction_dump.c` — текстовый17→18scalar transport; callback-local scheduler
fixture намеренно не выполняет appended events. `interaction_world_dump.c` —
явный LE binary transport, source maps/fields и actual core API; он не вычисляет
ожидаемую геометрию. Renderer transport отдаёт actual NDS quads; oracle отдельно
проверяет их связь с native pixel shift и принятой N07 geometry.

Источники, driver manifests, EXE SHA до/после и exact binaries закреплены полным
отчётом. Host/SDK/runtime команды, финальные hashes и независимый review
публикуются в паспорте N11 и компактном итоге после полного gate.
