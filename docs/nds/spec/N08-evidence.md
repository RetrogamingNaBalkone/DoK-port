# N08: независимые доказательства времени и очереди PKR/PKE

Проверено 29 октября 2026 года. Этот документ описывает **оригинал** и
изолированное исполнение его инструкций. Контракт NDS и выбранные изменения —
в [N08-time.md](N08-time.md). Код GBA не используется как реализация или oracle.
Исследование [G06](../../gba/time-events.md) послужило указателем на адреса;
векторы N08 получены заново из бинарных экспортов сохранённых Ghidra-проектов.

## Образцы, адреса и происхождение

| Образец | SHA-256 до и после N08 |
|---|---|
| `originals/pkr/RUS/PKR.EXE` | `12f78afc0eba05f76b5dc0bb99da38d7daa20585024c0ba958fe828aa3864d28` |
| `originals/pkr/ENG/PKE.EXE` | `cce7bc5993d0734ad87e93db37a70b8b52faedfff14ac48d6e317e5ac996a37b` |

Адреса ниже — NE-loader Ghidra. Это не файловые смещения, индексы NE-сегментов
или runtime-селекторы DPMI. Сохранённые проекты:
`research/ghidra/projects/{ru,en}/Kremlin.gpr`; выбранные прежние C/ASM:
`research/evidence/time/{ru,en}/{automatic.c,evidence.asm}`.
Автоматический C используется для навигации, его сигнатуры не считаются
проверенными. Указанные ниже свойства сверены по ASM и исходным инструкциям.

[ExportNdsTime.java](../../../tools/ghidra/ExportNdsTime.java) открывается через
Ghidra 12.1.2 **read-only**, проверяет `currentProgram.getExecutableSHA256()`
против конкретного образца и экспортирует relocation-applied memory blocks.
Исходные EXE не патчатся. Журналы, `program-source.tsv`, размеры сегментов,
манифесты SHA до/после и бинарные блоки находятся в
`research/evidence/nds/n08/original/{ru,en}/`.

| Язык / блок | SHA-256 Ghidra-экспорта |
|---|---|
| RU `1000` | `ae4dedf4032ffbc89468d96e7e2479a048a5eb2bb2b236e83fb0bde563f6fa5c` |
| RU `1038` | `27de2e9671dc15354b4484f25b4c0845e3b80238b2661544305bb222e728b8cb` |
| RU `1130` | `65680e595d34c2009d56512c21ad7a45198ddb5a0b1d5e9d75f6e3010a80baf8` |
| EN `1000` | `44f36c87bd19022734ae64fd824d6d064b04b941c998c7c8578962ee1ef8174c` |
| EN `1010` | `5495bb96715c6c86dfae18da593f93a43ec1e31de4679719707d642f2d07bae5` |
| EN `1078` | `483bd6fffad4d811291a3f55ef908c29eb0fb28cf5bb6810ae742be2387627d3` |

## ABI и очередь

| Назначение | RU | EN |
|---|---|---|
| owner init / destroy | `1038:0000 / 005F` | `1010:13AA / 140F` |
| schedule | `1038:0108` | `1010:14BE` |
| cancel текущей записи | `1038:0214` | `1010:15CA` |
| clear | `1038:0234` | `1010:15EA` |
| set interval | `1038:0265` | `1010:161B` |
| dispatch | `1038:0284` | `1010:163A` |
| globals: owners/events/cursor/array/keep | data `03E8/03EA/03EC/03F0/03F4` | data `03F0/03F2/03F4/03F8/03FC` |

Owner — 16 байт: count и capacity по 16 бит, far array+4, next+8,
previous+12. Event — 16 байт: deadline uint32+0, interval 32-битный
битовый шаблон+4, far callback+8, opaque parameter+12. Сегментные указатели
не преобразуются в обычные near C pointers. Функции используют far C stack ABI,
возврат через `RETF`, аргументы снимает caller; 32-битный результат часов
возвращается в DX:AX. Прологи устанавливают DS=SS/DGROUP.

Native callback получает **один far owner**: перед `CALLF` виден только
`PUSH dword [cursor]`, после него `ADD SP,4`. Stored parameter не передаётся.
У `CALLF [BX+8]` есть префикс ES (`26 FF 5F 08` в RU `1038:0332`): нельзя
выводить default DS только из сокращённого текста Ghidra.

Capacity проверяется signed16: `capacity-1 > count`. При обычном аргументе4
доступны три события, четвёртое молча теряется. Schedule читает часы один раз
и пишет `uint32(now+interval)`. Отрицательные интервалы не ограничиваются;
вызовы анимации используют `MOVSX` исходного int16.

Первый event включает owner в кольцо: вставка после cursor, затем cursor
становится новым owner. Поэтому первоначальная активация A,B,C даёт обход
**C,A,B**. Внутри owner сохраняется порядок вставки, сортировки сроков нет.
Dispatch(0) читает часы в начале непустого dispatch, перед каждой due-проверкой,
после каждого callback и в конце sweep. Пустой dispatch часов не читает.
Ненулевой duration в DOS может повторять sweeps в busy loop; основной игровой
путь вызывает dispatch(0). Этот busy wait не задаёт частоту всей логики.

Есть дополнительный крайний случай даже для **duration0**. RU `1038:02B1`
сохраняет `end=uint32(now_begin+duration)`, а `1038:03F9/03FD` выполняет
`CMP now_end,end; JC 02B9`; EN `1010:17AF/17B3` эквивалентен. Если begin
равен `FFFFFFF0`, callback продвинул часы на32 до `00000010` и отменил
последний event, final unsigned10<FFFFFFF0 запускает новый sweep.
`event_count` проверяется только при входе в функцию (RU `1038:0293`), поэтому
последующие пустые sweeps могут ждать возвращения часов к `FFFFFFF0`.
Проверка wrap deadlines не доказывает эквивалентность такого rollover **внутри
dispatch**. Однопроходный NDS dispatch намеренно исключает этот busy loop;
это отдельная safety adaptation, а не точное исходное поведение. Основные
16 frozen native vectors не изменялись; контрпример установлен по ASM, опасное
длительное ожидание не исполнялось как критерий соответствия.

Порядок одного элемента:

1. Compiler helper копирует все16 байт из read в write. Его направление
   подтверждено `1000:5CB9 LDS SI,[BP+6]`, `5CBC LES DI,[BP+A]`, `CLD`,
   `REP MOVSW/MOVSB`, `RETF8`; длина передаётся в CX, не стеком.
2. Глобальный keep=1. Unsigned `CMP now,deadline; JC skip` проверяет исходный
   read-slot. Равенство исполняет callback.
3. После callback часы читаются заново, **read-slot** получает
   `uint32(now_after+read.interval)`. Просроченные периоды не догоняются.
4. Cancel пишет глобальный keep=0, если переданный owner непустой. Write
   продвигается только при keep!=0, read — всегда. Count в условии живой:
   append в текущий непустой owner может исполниться в том же sweep.
5. Set interval пишет **array[0].interval**, даже при callback второго события.
   После compaction новое deadline в source-slot может остаться вне итогового
   count: survivor сохраняет старый срок, скопированный перед callback.
6. В конце count=write; пустой owner исключается. Число visits сравнивается
   с уже уменьшенным owner_count, поэтому часть соседей ждёт следующий sweep.

Unsigned wrap — поведение оригинала, а не обычное signed-difference сравнение.
При now=`FFFFFFF0`, interval32 получается deadline16 и callback исполняется
сразу, до rollover. Старый deadline=`FFFFFFF0` при now5 ещё не due.
Interval−1 при now0 даёт `FFFFFFFF` и ожидание; при now10 — deadline9 и callback.
Замена на `(int32)(now-deadline)>=0` меняет эти проверенные исходные случаи.

## PIT, шкала и stopwatch

| Назначение | RU | EN |
|---|---|---|
| IRQ handler | `1130:0000` | `1078:168A` |
| PIT reader | `1130:015D` | `1078:17F3` |
| stopwatch init/start/read/stop | `1130:01F5/0239/026F/02B6` | `1078:188B/18CF/1905/194C` |
| irq/base/period/calibration | data `0E70/0E74/0E78/0E7C` | data `0F20/0F24/0F28/0F2C` |

Установка PIT: channel0 mode2, `34h` в43h, два нуля в40h; period65536.
Reader делает CLI, latch command0, два IN40h, затем STI; прежний IF не сохраняет.
Инструкция DIV unsigned, знаменатель119 (`77h`), два отдельных деления:

```text
phase = uint16(calibration - latched_counter)
ticks = uint32(uint32(irq_count * period) / 119 + phase / 119 + base)
if irq_count > 10000:
    irq_count = 0
    base = ticks
```

Младшее произведение IMUL и финальное сложение заворачиваются в32 бит.
Дробные остатки обоих DIV теряются независимо. Reset после10001 IRQ не
перезапускает PIT phase: следующее чтение может добавить фазу ещё раз;
между STI и reset также возможна гонка с IRQ. Изолированный oracle задаёт
входной counter и не эмулирует реальные PC interrupts или длительность IN.

При принятой инженерной частоте PIT1193182Гц номинальная шкала равна
`1193182/119 ≈ 10026,7395 ticks/s`. Это вывод из аппаратной константы и ASM,
не точное измерение PC-кварца. Сохранённые DOS HTTP-наблюдения RU/EN дают
грубую согласованную частоту; они не заменяют исходные instruction vectors.
Период героя416ticks из [original-cadence](../../originals/original-cadence.md)
не является частотой всей логики.

Stopwatch —10 байт: running16, start32, elapsed32. Init очищает running/elapsed;
start при running0 записывает абсолютное now, при running1 ничего не делает;
read при running1 вычисляет uint32(now−start), иначе возвращает frozen elapsed;
stop делает read и затем running0. Новый start **не накапливает** elapsed
предыдущего интервала. Поэтому удобные накопительные игровые pause/resume
NDS нельзя объявлять дословным переносом этой структуры.

## Исполнение и проверки

[audit_time.py](../../../ports/nds/tools/audit_time.py) использует общую
Unicorn2.1.4 (`C:/Source/wasdify/build/research-deps`), Python3.14.6.
Исходные scheduler instructions, CLD/REP copy helper и PIT/stopwatch
исполняются в16-битном x86. Фикстура размещает scheduler/clock/helper в
сегментах5000/6000/7000, DGROUP/stack3000, owners/arrays9000.
Эти адреса — лабораторное размещение, не адреса оригинального процесса.
Far clock service scheduler подменяется детерминированным DX:AX sample;
PIT-тесты исполняют настоящий reader с контролируемыми IN-байтами.

Callbacks — явно искусственные короткие machinecode bodies. Они получают
native owner, записывают trace и вызывают **оригинальные** cancel/interval/
schedule/destroy. Callback code перенаправляется в свежую область до исполнения,
чтобы не зависеть от кешированного translated block Unicorn. Hand-checked
assertions ловят, в частности, ситуацию, когда callback action не исполнился.
Free-hook только записывает вызов: fixture allocations не освобождаются.

Результат — `research/evidence/nds/n08/original-vectors.json`: **PASS**,
16 scheduler cases,24 callbacks,8 PIT cases и11 stopwatch steps **на язык**.
RU/EN native traces и результаты совпали. Проверены:

- Три event slots, ring C,A,B, равные сроки, вставка без deadline sort.
- Повтор от post-callback time10→17→deadline27; следующий callback27→34→44.
- Compaction survivor со старым deadline20; first-slot interval77 при callback2.
- Live append callback2 в текущем sweep; shrinking visits C,A, cursorB.
- Ранний wrap, старый deadline после wrap, interval−1 при now0/10.
- Clear, удаление другого owner в callback, полное опустошение кольца.
- PIT phase/двойной DIV, reset10001, calibration, uint32 product/add wrap, STI.
- Stopwatch повторный start, frozen stop, restart без накопления, subtraction wrap.

Схема `.samples.ru.cases[]`: `operations`, `actions`, `clock_reads`, `callbacks`,
`final`. Operations задают schedule/now/dispatch/clear/destroy; actions —
изменение now и native API из callback. Clock trace включает service reads
schedule, start/end sweep, due и post-callback. Final хранит globals и сырые
native slots. **В `destroy_other_active` owner1 уже вне lifetime**: оригинал
не обнуляет count после free. Его оставшиеся байты не являются допустимым
состоянием объекта; NDS-сравнение должно исключать owner1, сохраняя сравнение
живых owners2/3, globals и callback order C,B. Это не UAF-conformance тест.

Повторяемые команды из корня репозитория:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File ports/nds/tools/export_time_evidence.ps1
C:/Source/wasdify/.venv/Scripts/python.exe -X utf8 ports/nds/tools/audit_time.py
```

Экспорт не требуется повторять при каждом gate: oracle сверяет hashes бинарных
блоков и exporter/driver с сохранёнными manifests и SHA оригиналов до/после.
Полные бинарные экспорты/отчёт остаются локально в ignored research; это не
payload поставки. Проверка NDS-реализации независимо сравнивает её результаты
с этими vectors, а не с новой Python-моделью scheduler.

## Опасные исходные пути и границы доказательства

Destroy текущего owner в callback освобождает его array и изменяет cursor,
а dispatcher затем продолжает обращаться к global array. Немедленная активация
пустого owner из callback также меняет cursor при старом global array.
Рекурсивный dispatch перезаписывает global array/keep/cursor. Helpers вне
dispatch могут использовать устаревшее состояние. Это выводы по конкретным
ASM accesses; наличие намеренного использования этих путей игровой логикой
не установлено. Исходный UAF/порча не исполняются как нормы соответствия NDS.

Отложенное уничтожение/публикация, отказ от recursion и lifetime проверки NDS
должны быть записаны как явные safety adaptations и проверены отдельно.
К ним относится и запрет повторного sweep после mid-dispatch uint32 rollover:
в этом случае безопасная одноразовая обработка NDS отличается от native busy wait.
Сравнение безопасных векторов не доказывает поведение всех реальных игровых
callbacks. Их интервалы, owner lifetime и side effects проверяются в N09–N23.
Новая сетка polling, рациональные монотонные часы NDS, pause/resume и физическая
DS/DS Lite этим original-instruction отчётом не проверены; аппаратура — N24.
