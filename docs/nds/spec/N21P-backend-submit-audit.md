# N21P: независимый аудит backend submit и размещения ARM9 кода

29 октября 2026 года. Проверены frozen C13 ARM object/ELF, linker map и
соответствующий архив исходника до C14 strip packing. Исходники продукта
и ROM не изменялись; эмулятор не запускался. C13 остановился на startup
memory guard, поэтому приведённые 6,85 мс submit относятся к C12 heavy,
а не к несуществующим runtime кадрам C13.

Машинные доказательства:

- `research/evidence/nds/n21p/resource-audit/backend-submit-object-review.json`;
- `C13-backend-object-disassembly.txt`, `C13-backend-sdk-disassembly.txt` рядом;
- `C13-backend-layout-review.json` и воспроизводимый `collect_backend_layout.py`.

ELF SHA-256 `9b6c4171209e09742c6454fcf8e1608b4d48ebb863f98349adfa88ade82659da`
совпал с frozen manifest. Архив `pre-strip-packed/.../render_backend.c`
SHA-256 `27e9e3fc9f81e41223dbfd2406d73ff868631a71d10e28311d5d83f6d03c98b8`
совпал с source pin того же manifest. Object SHA-256
`27a0f244d57ff1549eee30ae87e6540aa2d751379322df1db552ac1ef13c3c34`.

## Что доказано в реальном ARM object

`dok_nds_render_submit` сначала полностью проверяет вход до первой FIFO
записи, затем повторно обходит клетки для отправки. `planned_cell_clip`
работает **дважды на клетку**: один раз при admission, один при emission.
Три статических call sites в object соответствуют двум runtime проходам,
а не трём вызовам на каждую клетку. Каждый вызов копирует 64 байта basis
через четыре пары LDM/STM, вычисляет три wide shifts и двенадцать широких
произведений в циклах. Функция занимает 420 байт, submit — 3952 байта.

Runtime division helper в submit, planned clip и camera plan отсутствует:
постоянные `/512`, `/4096` и `/8` скомпилированы в сдвиги с поправками знака.
`nearest_div` не вызывает программное деление для каждой UV. В submit нет
вызовов `memcpy`/`memcmp`; очистка stats вызывает один `memset` за кадр.
`glLoadMatrix4x4`, UV/vertex stores, begin/end inline. `glBindTexture` —
реальный SDK вызов со своими lookup, но только при изменении texture slot;
автоматически заменить его direct register store нельзя без сохранения
SDK state/lifetime контракта. Wide division в другом `stream_commit`
относится к fog setup при изменении darkness, не к каждому steady submit.

При 285 quads работает существующий capacity fast proof: 285 меньше 614,
поэтому для клеток с `clip_range_bounded=true` не выполняются все CPU clip
transforms каждого vertex. Доля клеток, для которых bounded proof проходит,
в имеющемся таймере отдельно не измерена. Снимать vertex/UV/material checks
или подменять их предположением о корректном мире не предлагается.

Emission отправляет по 16 matrix parameters на клетку, а также vertex/UV
MMIO. Текущий `geometry_wait_ticks` измеряет лишь финальный `fence`.
Его 41–42 ticks **не доказывают отсутствия FIFO stall** во время предыдущих
MMIO writes. Общий submit timer смешивает admission, CPU preparation,
SDK binding и возможные bus/FIFO stalls. Распределить 6,85 мс между ними
без дополнительного замера нельзя. Число quads не равно числу клеток;
стоимость matrix loads нельзя умножать на 285 без actual cell count.

## Разумные следующие варианты

**Сначала representation/memory, без нового cache.** Четыре индекса 0..3
и reuse bit помещаются в uint16: четыре двухбитных поля и bit8, default
`0x00b4`. При сохранении capacity2048 это сокращает `strip_draw`
с 10240 до 4096 байт. Primary уже реализовал это в C14: прочитаны его host
proof (819200 helper cases, 16384 alias cases, все 512 decode states)
и ARM symbol proof, экономия 6144 байта подтверждена. Проверки самостоятельно
не перезапускались; runtime memory/budget acceptance пока не заявляется.
Четырёхбитный код паттерна потребовал бы дополнительного decoder и proof;
при достаточной экономии uint16 такая сложность не оправдана.

**CPU-only simplification, если понадобится:** оба origin и anchor
знаковые int32, кратные512. Поэтому `origin/512 - anchor/512` лежит в
[-8388607,8388607] и точно равен нынешнему wide difference/512.
int32 shift позволяет выразить произведение как signed32×signed32→64,
вместо текущего int64×int64 sequence. Alignment и итоговый range guard
сохраняются. Это доказанная арифметическая возможность, не измеренный
существенный выигрыш; отдельно накапливать мелкие правки ради неё не стоит.

**Предложение root MTX_TRANS — кандидат на более крупное упрощение.**
Первый clip кадра загружается полностью; для следующей клетки отправляется
translation по разности origin/512, в Q12. GE применяет MTX_TRANS к выбранной
projection как `C=M*C`; это полный 4×4 случай. Семантика команды и сохранение
24 fractional bits до финального shift описаны в
[GBATEK](https://mgba-emu.github.io/gbatek/#ds-3d-matrix-loadmultiply).
Реальная реализация `MatrixTranslate` в
[melonDS GPU3D.cpp](https://github.com/melonDS-emu/melonDS/blob/master/src/GPU3D.cpp)
обновляет m12..m15, включая W, с 64-битной суммой и shift12.

При integer translation×4096 каждый product кратен4096. Нет нового
fractional rounding: translation телескопируется к абсолютному clip,
а первые двенадцать basis coefficients не меняются. Это не обычное повторное
умножение quantized projection/modelview, ранее давшее расхождение на clip LSB.
Достаточные условия для дальнейшей проверки:

- Все исходные first-pass matrix/vertex/capacity проверки остаются до FIFO.
- На каждом frame первый clip загружается полностью, position — identity;
  диагностика между кадрами может менять GE state.
- Delta вычисляется без signed overflow; если Q12 translation не помещается
  в int32, применяется прежний full-load fallback. Полный публичный диапазон
  aligned int32 origin шире диапазона signed Q12 команды.
- Bounds actual basis/accumulator проверяются; не полагаться только на
  безошибочное переполнение C в emulator. Для текущей ограниченной projection
  и sin/cos база мала, но proof должен закрепить именно этот домен.
- Независимые CLIPMTX/POS_TEST проверки сравнивают old full loads и новую
  последовательность: отрицательные offsets, row wrap, W, длинные смешанные
  цепочки, общие endpoints и fallback. Audit здесь их не выполнял.
- Затем измеряется тот же heavy packet. Таблица GBATEK и современный melonDS
  расходятся в стоимости translation; меньше CPU writes само по себе
  не доказывает меньший суммарный CPU/FIFO/GE срок.

Без этих проверок MTX_TRANS не одобрен для acceptance. Большой matrix cache,
сортировка материалов и renderer downgrade не предлагаются.

## Доступный DS-specific путь: ITCM

Frozen C13 `.text` расположен в main RAM: 0x02004b08, 197168 байт.
`submit` =0x02018dac; `planned_cell_clip` =0x020182e8;
`dok_sprites_gather_live` =0x020173f4;
`dok_map_discovery_update` =0x02026e38. Все эти symbols находятся в `.text`.

`.itcm` =0x01000000..0x01000470: занято 1136 из 32768 байт, свободно
31632 до новых alignment/veneers. Уже размещены IRQ dispatcher, cache
maintenance, threading/SWI/system-counter и clock helpers. SDK имеет готовый
`ITCM_CODE` с section и long_call, штатный linker собирает `.itcm.*`;
новый toolchain слой не требуется. Перенос небольшой выбранной hot функции
возможен как отдельный измеряемый опыт, без заявления ускорения заранее.

Важный lifetime: linker фиксирует `__bss_vma` **до** TCM load sections.
В C13 `__itcm_lma` и `.bss` start оба =0x020377a0; main-RAM staging
переиспользуется BSS после startup relocation. Постоянной второй копии
ITCM image не требуется. Следовательно перенос `.text` может также уменьшить
resident main RAM, но фактические ELF/end/heap, veneers и budget проверяются
новой сборкой. Нельзя использовать staging pointers после startup или
перезаписывать IRQ helpers. DTCM и пользовательский stack этим не расширяются.
Данные и scratch останутся в своём размещении; MMIO/FIFO ожидания ITCM
не устраняет. Сравнение code footprint с I-cache также требует замера.

## Уточнение предыдущей архитектурной гипотезы

`field54-call-counts.json` опроверг длинные scans в heavy LEVEL2: scene
1495 calls/1495 entries для case14/15 и 1522/1522 для case39; native1440/1440.
Поле0x54 первое. После подготовки —117 calls/1011 entries на каждый путь:
основное изменение — число вызовов/interworking, а не устранение длинного
поиска19полей. C13 не дал runtime timing из-за startup guard FAIL.
Исторический C12 audit дополнен явным superseding уточнением с pins.

Измеренные C12 31–32 мс ARM9 против 16,715 мс на 59,826 Гц объясняют,
почему около30 не превращается в60 одной сменой частоты touch-карты.
Это свойство текущего CPU конвейера, не доказанный аппаратный предел DS;
данный read-only аудит не даёт обещания60 или принятия бюджета30.
