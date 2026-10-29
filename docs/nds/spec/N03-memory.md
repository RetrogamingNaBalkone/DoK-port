# N03: владение памятью и бюджет DS mode

Состояние: принятый архитектурный бюджет, host-проверки и ARM9 ROM проверены
в эмуляторе 29 октября 2026 года.
Это пределы для будущей игры, а не измерение её максимального потребления.
Общие границы модулей — [N03-architecture.md](N03-architecture.md), интерфейсы —
[N03-contracts.md](N03-contracts.md). Аппаратная проверка отложена до N24 по
[политике проверки](validation-policy.md).

## Источники и адреса

Проверена фактически установленная BlocksDS `1.24.0-1`; версии всех пакетов
закреплены в [toolchain_versions.json](../../../ports/nds/toolchain_versions.json).
Общий SDK: `C:/Users/dspev/.local/msys64/opt/wonderful/thirdparty/blocksds/core`.
Пути ниже даны относительно него. Адреса — виртуальные адреса ARM9/ARM7 или
LMA в ELF, не файловые смещения DOS и не адреса Ghidra.

Основные локальные первичные источники:

- `sys/crts/ds_arm9.mem`: ARM9 main RAM `[0x02000000,0x02380000)`, DTCM
  `[0x02ff4000,0x02ff8000)`, ITCM `[0x01000000,0x01008000)`.
- `sys/crts/ds_arm9.ld`: `__end__` после статических секций/TLS,
  `__eheap_end`, размещение загрузочных образов TCM, DLDI, стеков.
- `sys/crts/ds_arm7.ld` и `sys/arm7/main_core/arm7_maxmod.elf`: ARM7 code/data,
  загрузочные адреса, выделение всей shared WRAM ARM7.
- `libs/libnds/include/nds/system.h`: `getHeapStart/End/Limit`,
  `reduceHeapSize`; `nds/arm9/cp15.h`: фактическая конфигурация TCM.
- `nds/dma.h`, `nds/arm9/cache.h`, `nds/asminc.h`, `nds/arm9/video.h`:
  DMA, границы cache line, операции coherency и размеры VRAM.

Снимок SHA-256 этих файлов, ELF program headers и значимых ARM7 symbols:
`research/evidence/nds/n03/sdk-memory-layout.json`. Образ ARM7 Maxmod имеет SHA-256
`1e10a1483b33f978e5c2587f188709811ce8da821490caa1da18dc986c1dcb7d`.
Выбор этого SDK ARM7 соответствует N02; выбор будущей звуковой архитектуры
принадлежит N18 и потребует повторной сверки бюджета.

В DS mode ARM7 CRT загружается по `0x02380000`, длина 544 байта. Следующий
PT_LOAD: LMA `0x02380220`, VMA `0x037f8000`, длина 32484 байта. ARM7 использует
32 КиБ shared WRAM и 64 КиБ собственной WRAM; `__end__=0x038038bc`,
`__sp_usr=0x0380fdc0`. Отдельный PT_LOAD с флагом DSi имеет LMA `0x02e80000`,
VMA `0x03000000`, длину 7824 байта и не включается в DS mode.
Размер ARM7 загрузочного образа не доказывает доступность остальных верхних
512 КиБ: ARM9 linker исключает весь этот диапазон из статического образа.
N03 принимает такой же консервативный предел для runtime game/SDK heap.

**Обнаруженное различие linker и SDK runtime:** в обычном DS mode реальный
`getHeapLimit()` при входе в `main()` равен `0x023f4000`, а не `__eheap_end`.
Проверка в эмуляторе обнаружила это расхождение; независимый анализ установленных
SDK objects подтвердил причину. `mpu_setup.s.o` в ветке обычного DS устанавливает
`r8=0x02400000`; `ds_arm9_crt0.o` вычитает `0xc000` и записывает результат в
`fake_heap_end`. `heapfuncs.c.o` читает/изменяет именно эту переменную. Это
поведение CRT; предположение о возврате ARM7 staging не используется как факт.
Дизассемблирование объектов SDK, их SHA-256 и принадлежность `libnds9.a` сохранены
в `research/evidence/nds/n03/sdk-runtime-heap-evidence.json` и соседних
`*.disassembly.txt`; исходные SDK C/ASM sources в установленном бинарном пакете
отсутствуют. Исследовались SDK ELF objects, исходные игровые EXE не затрагивались.

Исторический пример SDK `tests/system/heap_bounds` содержит DTCM-константу
`0x02ff0000`, расходящуюся с текущим linker. N03 использует linker symbols и
runtime CP15, а не переносит эту константу из примера.

## Принятые пределы main RAM

Физическая main RAM обычной DS — 4194304 байта. Статический ARM9 linker region и
принятый N03 runtime envelope — 3670016 байт (3584 КиБ); верхние 524288 байт
полностью исключены из бюджета N03, даже если SDK runtime heap limit выше.
Знаменатель обязательного резерва — весь управляемый ARM9 диапазон, а не
пустой хвост конкретной диагностической программы.

| Владелец / назначение | КиБ | Байты |
|---|---:|---:|
| Code, read-only data, SDK/static, DLDI, startup allocator overhead | 512 | 524288 |
| Мир и состояние уровня | 384 | 393216 |
| События | 64 | 65536 |
| Рабочая арена / явные алгоритмические стеки | 128 | 131072 |
| Scene snapshots / main-RAM presentation buffers | 256 | 262144 |
| Кэш ресурсов | 1024 | 1048576 |
| Звуковые staging / mixing buffers | 256 | 262144 |
| Platform I/O staging | 128 | 131072 |
| Минимум доступного SDK dynamic heap tail | 192 | 196608 |
| Аварийный резерв, исключённый из heap | 640 | 655360 |
| **Всего ARM9** | **3584** | **3670016** |

Сумма семи управляемых game slabs — **2293760 байт (2240 КиБ)**. Резерв —
**655360 байт, 17,857142…%** управляемого ARM9 RAM; при номинальном code envelope
512 КиБ это 20,833333…% оставшихся 3072 КиБ. По runtime-данным ниже также вычислена
доля после фактических статических секций. Это одно и то же резервирование с
разными явно названными знаменателями.

Целочисленные пределы и compile-time проверки суммы находятся в
[memory_budget.h](../../../ports/nds/include/dok/memory_budget.h). Это ceilings,
а не требование заранее реализовать мир, события, рендерер и звуковой движок.
Изменение любого потолка должно сопровождаться новой сводкой, link map и
runtime gate. Статический запас простого probe не заменяет измерение полной игры.

## Реальное исключение резерва и владение

Независимая [ARM9-программа](../../../ports/nds/platform/memory/main.c) выполняет:

1. До console/stdio и собственного malloc снимает SDK heap start/current break/limit,
   `__end__`, TCM symbols и CP15. SDK startup до `main()` уже произошёл и входит
   в наблюдаемое состояние.
2. Сначала уменьшает SDK runtime heap limit до `0x02380000`; в текущем обычном
   DS mode дополнительное исключение равно 475136 байт. Затем вызывает
   `reduceHeapSize(655360)` и требует итоговый limit **`0x022e0000`**, то есть
   резерв находится внутри принятого envelope, а не отсчитывается от расширения
   SDK runtime. Break должен остаться ниже нового limit. Таким образом верхний
   SDK диапазон и отдельный аварийный резерв исключены из последующего
   `sbrk`/malloc программы. При более низком исходном limit gate завершается
   отказом; дополнительная доступная память эмулятора не расширяет бюджет.
3. Один раз выделяет `2293760+31` байт через платформенный malloc и выравнивает
   slab base на 32 байта. Платформа владеет исходным указателем; ядро получает
   только семь ограниченных диапазонов. Это startup allocation, не per-frame malloc.
4. Сверяет всю выделенную область записью и чтением 2293760 байт, а затем
   заполняет каждую арену до границы, проверяет отказ на дополнительный байт,
   сохранение данных/cursor/output/high-water и reset.
5. После console/stdio warmup повторно снимает heap break/limit. Реальный
   startup heap usage за вычетом game slab входит в тот же envelope 524288 байт
   вместе со статическими секциями. Отдельно проверяет минимум 196608 байт между
   heap break и уменьшенным limit. Выравнивание, malloc metadata и SDK startup
   расход не теряются за пределами таблицы.

`getHeapLimit()-getHeapEnd()` — доступное пространство для роста break, не
сумма свободных malloc chunks и не гарантия отсутствия фрагментации. Неиспользованная
часть envelope code/static/startup показывается как `code_envelope_headroom`;
это плановый запас на развитие кода, не второй аварийный резерв.

Загрузочные образы ITCM/DTCM в main RAM уже учитывает `__end__`; повторно
прибавлять их к main RAM нельзя. ELF/ROM файлы содержат debug/заголовки и другие
данные, поэтому их файловый размер также не равен resident RAM.

## Переносимые арены и рабочие стеки

[memory.h](../../../ports/nds/include/dok/memory.h) и
[memory.c](../../../ports/nds/core/memory.c) компилируются строгим C11 без NDS/SDL2.
Core не вызывает malloc и не выбирает физический адрес backing storage.
Ареной владеет вызывающий модуль: capacity, current used, lifetime high-water
и saturating счётчик отказов. Выделение учитывает выравнивание абсолютного
адреса, проверяет арифметику и пространство до изменения cursor/output.

`dok_work_stack` — явный LIFO стек копируемых элементов фиксированного размера.
Capacity выражена в элементах, high-water — максимальная depth; предел в байтах
равен `capacity*element_bytes`. Push до заполнения успешен, следующий возвращает
`NO_SPACE`; pop из пустого стека возвращает `INVALID_STATE`, не меняя output.
Reset арен/стеков сохраняет high-water и счётчики. Он завершает lifetime всех
выданных указателей; автоматическое обнаружение stale C pointers не обещается.

Backing должен реально существовать и быть доступен на запись в заявленном
размере; проверка integer range не доказывает доступность произвольного адреса.
Metadata и output не должны накладываться на metadata allocator. Byte backing
даёт byte storage; типизированные объекты требуют соблюдения effective type,
alignment и lifetime C11. В probe backing получен через malloc и не имеет
объявленного типа char-array; native GameState создаётся отдельным typed object.
Никакая arena-разметка не является packed DOS layout.

## TCM и настоящий C call stack

ITCM 32768 байт и DTCM 16384 байта считаются отдельно от main RAM и не используются
как запас DMA/ARM7 buffers. SDK помещает в ITCM vectors/свои функции; probe
измеряет занятые байты по `__itcm_start/end`. Принудительного размещения игровых
hot paths в N03 нет.

В DTCM SDK оставляет верхние 64 байта служебными. Ниже располагаются SVC stack
256 байт, IRQ stack 256 байт; `__sp_usr=0x02ff7dc0`. N03 устанавливает linker
`__usr_stack_size=12288`, чтобы наличие этих 12288 байт дополнительно проверялось
против DTCM data/sbss. Нельзя суммировать 16 КиБ DTCM и эти стеки как разные
пулы: стеки находятся внутри того же DTCM.

Pattern probe красит только `[user_stack_top-12288,currentSP-256)`, выровненный
на 4 байта, после проверки нижней границы относительно `__sbss_end`. Живые
frames, IRQ/SVC и SDK data исключены. Затем выполняется ограниченное упражнение
из шести C frames с volatile локальными массивами. Сканирование показывает
водяной знак **только окрашенного диапазона**, а 64 байта нижнего guard должны
остаться целыми. Это не точное измерение пика всей игры; неизменившийся pattern
случайно совпавших значений может недооценить использование. Host-проверки
не притворяются измерителем C call stack: они проверяют explicit work stack.

## VRAM, DMA и cache coherency

VRAM имеет отдельные 671744 байта (656 КиБ): A/B/C/D по 128 КиБ, E 64 КиБ,
F/G по 16 КиБ, H 32 КиБ, I 16 КиБ. N03 использует A и C для двух SDK text
консолей. Назначение игровых banks и выбор рендерера принадлежат N06; VRAM
не добавляется ни к main RAM budget, ни к его аварийному резерву.

Платформа единолично управляет DMA и передачами ARM7. NDS DMA API предупреждает:
включать/выключать DMA прямыми ARM9 register writes небезопасно; следует применять
SDK `dmaSetParams`/`dmaStopSafe` и соответствующие helpers. Из четырёх каналов
каждого CPU N03 не назначает игровые каналы заранее.

Планируемый контракт staging buffers:

- Только main-RAM диапазоны, владеющие целыми 32-байтными cache lines; TCM
  исключается из transfer pools. Native game pointers не становятся IPC ABI.
- Перед чтением DMA/ARM7 платформа выполняет `DC_FlushRange` и передаёт ownership.
  CPU не меняет данные, пока внешнее устройство их использует.
- Перед внешней записью CPU relinquishes диапазон после flush; после подтверждённого
  завершения платформа инвалидирует целые выровненные линии и возвращает ownership.
  `DC_InvalidateRange` для частичных линий может уничтожить соседние dirty data,
  что прямо отмечено в SDK header.
- Переиспользование арены, звукового буфера или snapshot lease ждёт завершения
  соответствующей передачи. Per-frame reset во время in-flight передачи запрещён.

Это правила владения, а не реализация ещё не выбранного renderer/audio pipeline.

## Команды и доказательства

Из корня репозитория:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File ports/nds/tools/build_memory.ps1 -Rebuild
```

Driver проверяет pin всех 20 SDK-пакетов, использует общий MSYS2/Wonderful,
не меняет parent PATH и не устанавливает пакеты. `-Clean` удаляет только фиксированные
артефакты N03 с проверкой путей. [Makefile.n03](../../../ports/nds/Makefile.n03)
собирает core и host-test translation units для ARM9 с `-std=c11 -pedantic-errors
-Wall -Wextra -Werror`; только платформенный source требует GNU C11 из-за SDK inline ASM.
Компиляция host-tests для ARM9 доказывает совместимость заголовков, но их исполнение
там не заявляется: ROM выполняет собственные boundary/lifecycle сценарии.

Текущие `build/nds/n03/dok_nds_memory.{nds,elf,map}` и `build-report.json` связаны
SHA-256. Probe наблюдается через `g_nds_memory_probe`: ABI 1, 344 байта, только
little-endian uint32; seqlock odd/even и явный cache flush. Поля и битовые проверки
определены в [memory_probe.h](../../../ports/nds/platform/memory/memory_probe.h)
и main.c; успешная self-check маска — `0x1ff`, status 1, error bits 0.
Наблюдатель может читать RAM, но не подменяет результаты, PC или allocator state.

Полная host/ARM9 проверка и отдельное исполнение ROM:

```powershell
C:/Source/wasdify/.venv/Scripts/python.exe ports/nds/tools/check_core.py
C:/Source/wasdify/.venv/Scripts/python.exe ports/nds/tools/run_memory_emulator.py --core C:/Users/dspev/AppData/Local/Programs/melonDS-DS/1.4.0/melondsds_libretro-win32-x86_64-Release/cores/melondsds_libretro.dll
```

Host toolchain — общий Zig 0.13.0, ARM9 — установленный GCC 16.2.0 / BlocksDS
1.24.0-1, Python 3.14.6. Полная команда `check_core.py` также выполняет SDK clean
build и Linux cross-compilation; Linux execution не заявляется. В эмуляторе
использован именно **melonDS DS libretro core 1.4.0**, DS mode, software renderer,
builtin BIOS/firmware replacements, direct boot, SD отключена. Это отдельный
проект melonDS DS; называть его standalone melonDS 1.4.0 нельзя. Точные опции
и хеш core находятся в runtime report.

Итоговый runtime прошёл 42 независимые проверки, 120 согласованных snapshots,
171 frontend frames. Чтение всего managed диапазона в физической main RAM
подтвердило ожидаемые байты, включая семь границ и LIFO значения 1…8.
После self-tests программа явно вызывает `DC_FlushRange` для всего этого
диапазона; наблюдение не зависит от случайных cache effects SDK-консоли.
Верхний и нижний итоговые PNG визуально проверены отдельно.

| Фактическое измерение принятого ROM | Байты |
|---|---:|
| Статические секции до `__end__` | 42048 |
| Startup heap overhead за вычетом managed slabs | 7104 |
| Статика + startup overhead | 49152 |
| Managed slabs, реально записаны/прочитаны | 2293760 |
| Доступный хвост роста SDK heap break | 671744 |
| Исключённый аварийный резерв | 655360 |
| **Сумма принятого ARM9 envelope** | **3670016** |

Все семь арен достигли своего capacity, отклонили дополнительный байт и после
reset имеют used 0, сохранённый high-water=capacity, failures=1. Work stack:
capacity/high-water 8 элементов, depth 0, один отклонённый push; pop из пустого
сохранил output. DTCM pattern watermark ограниченного упражнения вырос с 0 до
544 байт, нижний guard цел. ITCM занято 1136 из 32768 байт.

Реальный начальный SDK heap limit `0x023f4000` уменьшен на 475136 байт до
policy limit `0x02380000`, затем на резерв 655360 байт до `0x022e0000`.
Начало heap совпало с `__end__`, скрытого gap в этой сборке нет. Арифметическое
замыкание таблицы независимо проверено наблюдателем. Code envelope headroom
равен 475136 байтам, а резерв — 17,857142…% ARM9 region и примерно 18,0641%
диапазона после фактических статических секций. Headroom не является пиком полной
игры или ещё одним резервом. Если SDK/argv изменят gap перед heap, повторная
проверка должна заново учесть его; принимать незамкнувшийся бюджет нельзя.

Контрольные SHA-256 текущих артефактов:

- ROM, 97792 байта:
  `e2d75b0d1ed158f4f2523cb3cce781b679585bfad0c37b7da9c4745d3ff6d431`.
- ELF, 603800 байт:
  `886a8e707f261024c4993cb4c32594171785ecfa46eb42720a166e5874fd9d07`.
- Link map, 619036 байт:
  `2dd5b7d39e6bdad12bb27158f26f6e490f394f0f88103c0caabf97702ef6cab7`.

Полные отчёты: `research/evidence/nds/n03/host-check.json`,
`build/nds/n03/build-report.json`, `build/nds/n03/emulator/result.json` и
сохранённые root в `research/evidence/nds/n03/` отчёт/контрольные PNG.
Компактный итог, окончательные пути архива и ограничения передаются через
[N03 паспорт](../tasks/N03.md). Реальная DS/DS Lite, полная игровая нагрузка,
совместимость картриджа/loader и аппаратные FPS этой проверкой не доказаны.
