# N22: регрессия перераспределения EVENTS → ASSETS

Отдельная N22-проверка прошла после переноса28672байт из незанятого хвоста
events arena в assets arena. Приёмка полного прохождения и устранения
[CPU-cache fault](N22-cpu-cache-fault.md) сюда не входит.

| Arena | До | После |
|---|---:|---:|
| EVENTS | 65536 | 36864 |
| ASSETS | 1048576 | 1077248 |
| Общий managed slab | 2293760 | 2293760 |
| Emergency reserve | 655360 | 655360 |
| Минимум роста SDK heap | 196608 | 196608 |

Текущий `dok_event_bank` содержит все512owners с тремя слотами и занимает
ровно36864байта на ARM9. `events.h` требует точное совпадение с events arena
через static assertion. Алгоритм callbacks не менялся.

Драйвер `ports/nds/tools/flow/check_memory_rebalance.py` использовал свежие
сборки `Makefile.n03` и `Makefile.n08`, передав command-line `OUTDIR` в
`build/nds/n22/rebalance/{memory,time}`. Версии общего BlocksDS проверены
по `toolchain_versions.json`, SDK ARM7 Maxmod ELF отдельно закреплён хэшем.
Все187существовавших файлов в старых `build/nds/n03`, `build/nds/n08`,
`research/evidence/nds/n03` и `research/evidence/nds/n08` остались неизменны.
Старые entry points с N03/N08 accepted-pin gates не запускались и не
получали исключений.

Проверено на Windows host:

- исходные N03 memory tests:28459проверок границ, переполнений, alignment,
  транзакционного отказа и стека;
- исходные contracts tests: lifecycle, identities, input и отказ без мутаций;
- общий N08 `time_cases`:17974проверки,3clock cases,7event cases,
  включая регистрацию512owners; `sizeof(bank)=36864`;
- свежий N22 flow gate:655929проверок;
- свежий occupied-retry gate:160проверок,40реальных AI callbacks,
  три пересекающихся actor, два повторных retry и две corpse variants.

В эмуляторе обычного DS ARM9:

- N03 probe выполнил девять self-test groups и дал120coherent observations.
  Повторно проверены все2293760байт ARM9-written patterns с новыми границами
  arenas,38observer checks, реальная malloc backing и stack watermark.
  Получено171video frames; measured SDK heap growth671744байта.
- N08 probe повторил те же17974shared checks без failures. Физические
  Start press/release удержали world clock на15352ticks в течение126VBlanks,
  затем продолжили pending deadline. Проверены378точных rational clock
  transitions,62live callbacks и polling traces при15/30/60render FPS.
  Получено398video frames; SDK heap growth602112байт, events bank36864байта.
- В обоих probes сохранён managed slab2293760, reserve655360 и минимум
  SDK196608; полный бюджет ARM9 замыкается на3670016байт.

Для N03 используется существующий чистый `verify` с отдельным набором
globals, где только `CAPACITIES` заменён точным N22 tuple. Исходный модуль
и его исторические константы не меняются. Для N08 импортированы неизменённые
`fields`, `observe`, `verify_traces`, `verify_memory`; transport loop перенесён
в новый драйвер. Дополнительно строго требуется `events_bank_bytes==36864`.

Адаптация диагностического C-кода: в
`platform/time_probe/main.c:145` проверка хвоста теперь перебирает
`[sizeof(bank), EVENTS_BYTES)` от base arena. Предыдущая форма сравнивала
unsigned index с константой `EVENTS_BYTES-sizeof(bank)==0`, что корректно
отклонялось `-Werror=type-limits`. Для exact-fit банка хвост пуст; для
непустого хвоста остаётся та же проверка каждого байта на0xA5. Текстовая
строка probe также печатает текущий `DOK_NDS_EVENTS_BYTES`, поэтому экран
показывает `36864 / 36864` вместо устаревшего denominator65536.
Production runtime и core callback code этим драйвером не менялись.

Результаты и frozen source snapshots:
`research/evidence/nds/n22/rebalance/report.json`; компактная сводка
`analysis/nds-n22-memory-rebalance.json`. Команда:

```powershell
py -3.14 -B -X utf8 ports/nds/tools/flow/check_memory_rebalance.py
```

MSYS2 build требует обычного разрешения на Win32 signal pipes. Проверка
эмуляторная: это не замер пиков полной игры и не аппаратная приёмка N24.
