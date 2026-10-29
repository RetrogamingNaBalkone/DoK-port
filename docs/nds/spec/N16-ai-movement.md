# N16: пробуждение и движение AI

29 октября 2026. Готово (эмулятор).

Исходники — read-only Ghidra N14 export PKR/PKE, SHA/ABI в N14/N15.
Адреса Ghidra NE loader: wake1068:3747/1020:3EB0,
getter1068:3ECA/1020:4633, active1068:3845/1020:3FAE,
course compare/increment1068:3B86/3D2A и1020:42EF/4493;
heading10C8:01C5/1058:01C5. WORD/DWORD signedness, far pointers,
RETF/caller cleanup проверяются по listing, автоматический C не компилируется.

Модель:128 class9 owners с логическими glyph-index links; лимит проверяется
явно, исходные объекты не сокращаются. Движение использует NDS N09 motion и
N10 collision. Immutable N05 RU definitions задают HP/speed/activation/course;
язык меняет только presentation. Случайных вызовов в восстановленных ветвях нет.

Getter будит только рассмотренный объект при max(abs(dx),abs(dz))<activation.
Первое попадание будит без SUB. Course counter сбрасывается только при равенстве
периоду; иначе INC16. После сброса интервал N+1 проходов, не графических кадров.
Front lookup выполняет один шаг по старому heading в MMap, при class0 второй
шаг в TMap. Course trial временно снимает только собственную MMap-клетку,
выполняет до10 collision-resolved полушагов до смены клетки, затем восстанавливает
карту. Никаких occupancy commit/door counter изменений на пробе нет.

NDS sampling policy: consideration IDs формируются по принятым N12 bounds и
порядку MMap/TMap в simulation time. Предполагаемый production consumer —
service героя, не render callback. Это сохраняет детерминизм при30/60 outputs,
но не заявляет точного совпадения DOS render/event interleaving. Условие getter
и отсутствие LOS внутри него сохраняются; all-awake не штатный алгоритм.

Heading использует новую таблицу, выведенную из math ctor с масштабом1000;
не Q15, не atan2. FPU initializer не объявляется исполненным. Ограничения
signed32 overflow явно отклоняются, легальные координаты уровней поддержаны.

В N16 CONTACT пока передаётся наружу, атаки/hurt/death завершаются N17.
Модуль не выдаёт это промежуточное состояние за полный бой. Сохранений нет.

## Часы и совместимость N15

Movement stopwatch снимается после course/animation, перед N09 update;
clock_start фиксируется после движения. У projectile N15 аналогичный source
clock_start находится в конце callback (RU1068:2C61…2C70). N16 уточнила его
sample: last_ticks читается после animation/decay, не переносится из начального
замера. Постоянные clock fixtures N15 не меняются; исправлено поведение при
затраченном CPU-времени внутри callback. Прежний combat.c сохранён до правки,
source-supersession в `research/evidence/nds/n16/` содержит hashes и причину.
Makefile N15 допускает внешний OUTDIR для наследования N16, прежний default
и флаги сохранены. Другие направления не затронуты.

## Проверки, команды и передача

Общие Python3.14.6/Zig0.13.0, BlocksDS1.24.0-1/GCC16.2.0,
Unicorn2.1.4 и melonDS DS1.4.0. PY — путь из N14.

```powershell
& $PY -B -X utf8 ports/nds/tools/generate_ai_table.py
& $PY -B -X utf8 ports/nds/tools/check_ai.py
& $PY -B -X utf8 ports/nds/tools/build_gameplay.py n16
& $PY -B -X utf8 ports/nds/tools/run_gameplay_gate.py n16 ai 0x16414b44
```

Host/ARM9:94532 checks,18 cases=12 normal routes+6 diagnostic all-awake,
failures0; normal2152 callbacks/2980 considerations/1620 trial steps,
stress4374 callbacks. Trace hash3234181331 совпал между host/ARM9, RU/EN
и двумя частотами вывода. Герой двигается и поворачивается через N09/N10,
pickups проходят N14, AI использует текущие MMap/дверные flags/counters.
N16 routes не активируют doors через Use и не заменяют будущий боевой playthrough.
Групповые stress проверяют занятость каждой клетки и границы карты; в штатную
нагрузку они не включены. Модель N+1, профиль N05 и первый hit проверены отдельно.

Native **на язык**:2049 heading vectors,200 getter boundaries и42 course
branches, всего4582. Абсолютное значение и wake являются контролируемыми
сервисами; неизвестные source ABI не объявляются выполненными. Integer angle,
strict perception comparison и INC16/equality выполняются оригинальными
инструкциями. Полный active callback и x87 table ctor не исполнялись целиком.
N09/N10 primitive evidence сохраняется; не выдавать два новых C за DOS oracle.

В той же ARM9 ROM повторена N15 suite313608 checks/34 cases, trace1888275799;
host повтор тоже прошёл после уточнения clock_start. Linux musl static
скомпилирован, не исполнялся. DS mode, built-in BIOS, software renderer, JIT off;
5985 frontend frames; RAM только читалась, FAT неизменён до/после unload.

CPU в модели ARM9 (cpuGetTiming,33513982 Hz): normal34651901 ticks суммарно,
max43942 ticks≈1,311 мс на callback; stress60776643 ticks/max40111≈1,197 мс.
Диагностика включена, loading/renderer/audio отсутствуют. Это не физический
DS FPS, не совместный бюджет N17P и не обещание запаса для полного боя.
ARM9 AI state9812 B; maps/definitions/scheduler/bank заимствуются.

Доказательства: `research/evidence/nds/n16/{host-check,original-report,emulator}.json`,
source/build manifests; компактный итог `analysis/nds-n16-ai-movement.json`.
Текущая логическая ROM — `build/nds/n16/arm9/dok_nds_ai.nds`, hashes в паспорте.
Физическая DS/DS Lite — N24. Следующий ID N17: attacks, burst/sight, hurt/death,
corpse placement и lifecycle, затем общая игровая оболочка/профиль N17P.
