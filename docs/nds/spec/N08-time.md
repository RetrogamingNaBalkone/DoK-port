# N08: исходные часы и события

Независимая реализация NDS, 29 октября 2026 года. Ядро компилируется strict C11,
не включает SDK и не использует malloc. Это новый код по PKR/PKE ASM;
GBA scheduler, его фиксированная сетка и таймерный адаптер не копировались.
Платформенный счётчик и runtime-проверку описывает [паспорт N08](../tasks/N08.md).

## Платформа DS и границы шкалы

Адаптер `ports/nds/platform/time/clock.c` запускает `systemCounterSetup()`
один раз и возвращает N03 PlatformServices с `systemCounterGetTicks()`.
Повторный initialize не перезапускает счётчик. Timer2 принадлежит этому
SDK-сервису; timers0/1 остаются доступны измерителю CPU, timer3 здесь не занят.
Номинальная частота counter — **33513982/64 counts/s**, а native ticks —
**1193182/119 ticks/s**. Поэтому точное сокращённое отношение составляет
`38181824/1994081929` native ticks на counter count. Частота не задаёт
единого периода логики или polling.

Установленные первичные SDK header/ELF objects сохранены и разобраны
`audit_time_sdk.py`: `research/evidence/nds/n08/sdk-counter.json` и `sdk/`.
`system_counter.c.o` выбирает timer2, divider64, reload0. Handler прибавляет
65536 к 64-битному accumulator с переносом; GetTicks сохраняет/восстанавливает
CPSR, читает low16 дважды и учитывает pending IRQ/rollover. Это анализ SDK,
не исходного игрового EXE. При блокировке IRQ на полный период переполнения
timer2 (около125 мс) несколько пропущенных rollover не восстанавливаются;
контракт платформы запрещает такую блокировку.

DS clock сохраняет дробный остаток и монотонную платформенную шкалу. Он не
эмулирует DOS PIT I/O, потерю двух DIV-остатков или фазовый скачок при сбросе
IRQ-count после10000. Это явная платформенная адаптация. Native uint32 ticks
и unsigned deadlines по-прежнему заворачиваются. Реальный короткий прогон
проверяет многократный low16 carry SDK; полный uint32/uint64 uptime rollover
не ожидается в эмуляторе. Большие delta и native wrap проверяются инъекцией,
а не объявляются длительным аппаратным измерением.

## Источники и адреса

Адреса ниже — сегмент:смещение **NE-loader Ghidra**, не файловые смещения,
не runtime-селекторы DPMI и не вставки WASDify.

| Образец | SHA-256 |
|---|---|
| PKR.EXE RU | `12f78afc0eba05f76b5dc0bb99da38d7daa20585024c0ba958fe828aa3864d28` |
| PKE.EXE EN | `cce7bc5993d0734ad87e93db37a70b8b52faedfff14ac48d6e317e5ac996a37b` |

| Роль | RU | EN |
|---|---|---|
| Owner init / destroy | `1038:0000/005F` | `1010:13AA/140F` |
| Schedule | `1038:0108` | `1010:14BE` |
| Cancel / clear / interval | `1038:0214/0234/0265` | `1010:15CA/15EA/161B` |
| Dispatch | `1038:0284` | `1010:163A` |
| PIT reader | `1130:015D` | `1078:17F3` |
| Stopwatch init/start/read/stop | `1130:01F5/0239/026F/02B6` | `1078:188B/18CF/1905/194C` |

Первичные сохранённые экспорты находятся в `research/evidence/time/{ru,en}/`;
проверяемая структура описана в [native-state](../../originals/native-state.md).
В N08 оригинальный код scheduler исполняется из сохранённых Ghidra exports в
изолированной x86-модели с управляемыми clock/callback boundaries. В частности,
направление исходного compiler memcpy helper проверяется исполнением, а не
выводится из названий переменных автоматического C. Первичные оригинальные
векторы — `research/evidence/nds/n08/original-vectors.json`; метод и ограничения
зафиксированы в [аудите N08](N08-evidence.md).

Период героя 416 native ticks — отдельное событие, около 24,1 Гц при номинальном
PIT. Он не задаёт общую частоту логики. Исходный main loop вызывает dispatch(0)
перед сценой, поэтому реальное запаздывание DOS зависит от частоты опроса;
см. [original-cadence](../../originals/original-cadence.md).

## Владение и память

Новые интерфейсы: [time.h](../../../ports/nds/include/dok/time.h),
[events.h](../../../ports/nds/include/dok/events.h). Реализации:
[time.c](../../../ports/nds/core/time.c),
[events.c](../../../ports/nds/core/events.c).

Caller предоставляет правильно типизированные clock, scheduler и один bank.
`dok_event_bank` содержит 512 owners по 72 байта, всего **36864 байта**;
compile-time assertion требует размещения в принятом events slab **65536 байт**
N03. Scheduler/config и clock — отдельные небольшие native объекты; bank не
является packed DOS layout, файлом или IPC ABI. Лимит 512 — ёмкость NDS-хранилища,
не доказанный предел игрового оригинала и не результат измерения полного мира.

Owner содержит N03 `dok_object_ref`: ненулевые epoch, object ID, generation.
Object ID уникален среди зарегистрированных owners. Ссылки другого epoch,
поколения либо уничтоженного owner отклоняются. Caller мира отвечает за выдачу
нового generation при новом lifetime; scheduler не является аллокатором object ID.
Reset требует строго большего ненулевого world epoch и не допускает wrap.
Объекты и callbacks не сохраняются на microSD.

Owner регистрируется до обработки событий. Native constructor capacity
поддерживается в диапазоне 0…4; условие signed16 `count < capacity-1` сохраняется:
0/1 не принимают событий, 2/3/4 принимают 1/2/3 соответственно. Четвёртый slot
оригинального allocation не был активным; новый bank хранит три slot records.
Отказ превышения ёмкости возвращает `NO_SPACE` без чтения часов и публикации
неполного события. Native молча отказывал; явный статус — контракт NDS.

Callback ID uint16 заменяет DOS far pointer, ID 0 запрещён. Портовый adapter
получает owner reference, ID, opaque parameter uint32 и context. В исходном
callback передавался **только far owner**; дополнительные аргументы — решение
NDS, не восстановленная native calling convention. Callback/clock descriptors
заимствуют caller-owned context на весь lifetime scheduler. Эти callbacks
исполняются на одном потоке и не должны реентерабельно вызывать scheduler из
самого `read_ticks`; это не IRQ API.

## Точная шкала и монотонный источник

N03 `dok_platform_services` сообщает монотонный uint64 counter и частоту
`counter_hz_numerator/counter_hz_denominator`. N08 принимает любые ненулевые
uint32 numerator/denominator, включая крайние значения. Native world tick
остается uint32 и заворачивается намеренно.

После сокращения дроби на НОД:

```text
scale_numerator   = 1193182 * counter_hz_denominator
scale_denominator = 119 * counter_hz_numerator
value = previous_fraction + elapsed_counts * scale_numerator
ticks += floor(value / scale_denominator) modulo 2^32
fraction = value modulo scale_denominator
```

Для принятого NDS counter `33513982/64` это `38181824/1994081929` ticks/count.
Остаток сохраняется: разбиение одного elapsed interval на выборки не изменяет
tick и fraction. Обычно произведение помещается в uint64 и вычисляется прямо.
Если нет, код выполняет ограниченный 64-битный проход по битам elapsed с точным
частным modulo 2^32 и остатком. Промежуточный remainder меньше трёх denominator;
denominator не больше `UINT32_MAX*119`, поэтому сложение/удвоение безопасны.
Не применяется unchecked multiplication, floating point, потеря elapsed или
цикл по числу пропущенных ticks. Деление GCC/SDK может быть программным;
оптимизация этого пути не является целью N08.

Init читает counter один раз и принимает исходный uint32 tick (в тестах нужен
для wrap). Чтение меньшего uint64 counter возвращает `INVALID_STATE`, сохраняя
clock и output; оно не трактуется как полный uint64 rollover. Platform adapter
обязан предоставлять монотонное расширение аппаратного counter, как установлено
контрактом N03. Потеря более одного аппаратного периода между необходимыми
выборками определяется отдельно платформенным адаптером.

Это адаптация исходной номинальной шкалы, а не эмуляция PIT-reader по инструкциям.
DOS делит IRQ и phase отдельно, теряет остатки, после 10001 IRQ сбрасывает count
и может иметь фазовый скачок/гонку. Новый монотонный counter этих дефектов не
воспроизводит. Native stopwatch start после stop начинает новый elapsed interval,
не накапливая прежнее время; world pause ниже имеет иной, явно принятый смысл.

## Pause и resume

`dok_world_clock_pause` вне dispatch сначала учитывает последний active delta,
затем замораживает ticks и fraction. Во время меню чтение обновляет counter
baseline, не продвигая мир. Resume берёт свежий baseline без добавления menu delta.
Fraction до паузы сохраняется; он не накапливается по wall time меню. Повторный
pause/resume того же состояния идемпотентен и не делает дополнительного чтения.

Callbacks не вызываются ни pause/resume, ни обычным clock read. Смена pause
должна происходить между логическими service calls; отдельный clock не знает,
исполняется ли чужой scheduler callback. Это обязанность владельца жизненного
цикла. Событие с deadline 100 при остановке на tick99 остаётся недосрочным
после сколь угодно долгого меню и срабатывает после одного active tick; повторы
назначаются от resumed времени после callback, без burst пропущенных периодов.

## Сохранённый порядок dispatch

Scheduler делает **один sweep dispatch(0)**, не busy wait. При пустой очереди
он не читает часы. При непустой сохраняются чтения native начала и конца sweep,
даже когда они не влияют на due. Для каждого slot:

1. Record копируется из source/read в destination/write до проверки срока.
2. Injected clock читается перед unsigned сравнением `now >= source.deadline`.
   Равенство due. Знаковая разность deadlines не используется.
3. При due вызывается adapter callback. Затем часы читаются заново, а
   **source/read** deadline становится `now_after_callback + source.interval`.
4. Cancel обнуляет global keep текущего slot. Read продвигается всегда;
   write — только при keep. Живое count разрешает append в текущий непустой
   owner и обработку добавленного события в том же sweep.
5. Set interval изменяет **array[0].interval**, а не source/read. После удаления
   первого slot копия survivor может остаться со старым deadline: обновление
   source уже не попадает в compacted count. Обе странности сохраняются.
6. Пустой owner удаляется из кольца. Visit counter сравнивается с уменьшившимся
   owner_count, поэтому часть оставшихся соседей переносится на следующий sweep.

Новый непустой owner вне callback вставляется после cursor и сам становится
cursor. При регистрации событий A, B, C исходный проход — **C, A, B**.
Внутри owner сохраняется порядок вставки; глобальной сортировки по deadline нет.
Interval — uint32 битовый шаблон. Исходный sign-extended int16 −1 передаётся как
FFFFFFFF: при now0 deadlineFFFFFFFF ждёт, при now10 deadline9 сразу due.
Deadline, завернувшийся вперёд в малое число, может сработать до rollover;
старый большой deadline после rollover может ждать почти полный цикл.
Эти native unsigned особенности намеренно не исправлены.

Один просроченный период не порождает несколько callbacks: новый deadline
назначается после callback, старый deadline не увеличивается циклом догоняния.
Даже при interval0 каждый slot рассматривается один раз за sweep, если callback
не добавил новый slot; ёмкость ограничивает такой live append тремя slots.

## Явные безопасные отличия

- Dispatch выполняет ровно один sweep даже при rollover часов внутри callback.
  В DOS `end=begin+0` и unsigned финальное `now<end` могут вернуть исполнение к
  новому sweep: например, begin=FFFFFFF0, callback прибавляет 32 ticks и отменяет
  последнее событие, final now=00000010 меньше end. Native проверяет event_count
  только при входе, поэтому может продолжить busy loop даже после опустошения
  кольца, почти до полного uint32 периода. NDS не воспроизводит этот busy loop;
  это адаптация границы dispatch, а unsigned due каждого события сохранён.
- Destroy текущего owner помечает его к удалению, прекращает оставшиеся callbacks
  этого owner после возврата и только затем удаляет events/ring/registration.
  Source post-callback read для уже уничтожаемого owner не выполняется.
  Native освобождал array, пока dispatch мог продолжать обращаться к нему.
- Активация заранее зарегистрированного пустого owner из callback помещается
  в pending list. После sweep owners публикуются в порядке вызовов; новый owner
  начинает исполняться со следующего dispatch. Pending owner можно уничтожить
  до публикации. Native менял global cursor при прежнем global array.
- Register, recursive dispatch, clear и reset во время dispatch возвращают
  `INVALID_STATE`. Cancel/interval разрешены только внутри callback.
- Destroy другого активного owner сохраняет безопасный native unlink и shrinking
  bound. Destroy несуществующей/устаревшей ссылки возвращает `STALE_REFERENCE`.
- Clock failure фиксируется sticky fault. Уже выполненные callbacks не отменяются;
  текущая compaction заканчивается структурно корректно без новых callbacks,
  unread slots сохраняются. Следующий dispatch/schedule/clear отвергается.
  Только lifecycle reset с новым epoch снимает fault. Inspect остаётся доступен.
- Clear вне dispatch сохраняет registrations/identity/capacity и очищает все
  события/кольцо. Reset дополнительно удаляет registrations и меняет world epoch.

При пустом кольце NDS устанавливает cursor в sentinel NONE. Native мог оставлять
старый far pointer; он не обозначает активный owner и исключён из сравнения при
owner_count0. Байты native owner после destroy также находятся вне lifetime и
не сравниваются с очищенным NDS slot. Живые owners, события и cursor непустого
кольца сравниваются полностью.

Это выбранные правила NDS, а не утверждение, что исходные игровые callbacks
обязательно используют небезопасные пути. Будущие N09–N18 проверяют фактические
callbacks по оригиналам и обязаны учитывать эти границы.

## Независимость логики от вывода

В N08 **нет** update60, фиксированной сетки GBA, частоты 416 для всего мира,
догоняющей симуляции или автоматического dispatch внутри render/interpolation.
Native dispatch получает явную последовательность service/clock samples.
Presentation может читать world clock, но не исполняет события.

Проверка FPS separation означает: при одной и той же service timeline и входах
добавление presentation observations с частотами 15/30/60 не меняет callback trace,
deadlines и порядок owners. Она не утверждает, что две разные native poll
timelines должны давать одинаковые callbacks. Исходный post-callback schedule
намеренно зависит от конкретных clock samples, а callback cost отодвигает повтор.
Это различие также проверяется отдельными original vectors.

Полный gameplay main loop NDS ещё не интегрирован. Будущая интеграция обязана
сохранить отдельный логический service contract и явно проверить влияние затрат
подготовки/вывода сцены; N08 не объявляет будущий IRQ или детерминированный шаг
уже реализованным решением. В callback не исполняется GPU code и не берётся
заимствованный presentation lease.

## Общие host/ARM9 проверки

[time_cases.c](../../../ports/nds/tests/time_cases.c) исполняется одним кодом на
host и ARM9. Caller передаёт один bank из events slab; отдельного скрытого bank
для тестов нет. Suite разрушает его содержимое и требует нового init после теста.
Host wrapper — [test_time.c](../../../ports/nds/tests/test_time.c).

Проверены точная дробь после 4096 разных delta, полный uint64 elapsed и крайние
частоты (ожидания заранее вычислены целочисленной рациональной арифметикой),
backward counter, пауза с fraction и uint32 wrap; ёмкость всех 512 owners и 3 slots;
ring order, simultaneous due, shrinking bound, native compaction/interval;
repeat от post-callback clock, wrap и negative interval; live append,
deferred self-destroy/activation/cancel; reentrancy, lifecycle и clock fault;
mid-dispatch rollover с отменой последнего события и возвратом после одного sweep;
одинаковая service timeline с 15/30/60 observations и отсутствие menu burst.

Эти тесты проверяют новое ядро и сами по себе не доказывают DOS-эквивалентность.
Отдельно сравниваются native x86 vectors обоих EXE и actual ARM9 runtime export.
Окончательные команды, числа проверок, manifests, хеши и непройденные аппаратные
пункты фиксируются в [результате N08](../tasks/N08.md). Физическая DS/DS Lite
остаётся на N24; эмулятор не доказывает аппаратную частоту/FPS.
