# N15: контракт оружия и снарядов

29 октября 2026. Готово (эмулятор).

Основа — новый Ghidra export N14 PKR/PKE, actor/world/math, SHA до/после
в manifests. Адреса здесь — Ghidra NE loader. RU1068:136A/2368/2669/26E8
(EN1020:1874/2960/2D0C/2D8B) задают allocation, clone, spawn и callback;
RU1068:2CCA/2D48/2D71/2DB9 — show/hide/readiness/frame state;
RU1068:20BF — polling завершения смены через416 ticks.
Проверяются WORD/DWORD wrap, IMUL/SAR/IDIV, cdecl16far и caller cleanup.
Far pointers заменяются N03 references, автоматический C не компилируется.

Принята модель:256 слотов в исходном порядке glyph0…255. Статический словарь
N05 не меняется; отдельные instances с поколениями сохраняют template glyph,
owner reference, pose, flight/effect cursor, damage и время. N08 получает
заранее зарегистрированные сервисные owners пулов: их lifetime — весь мир,
parameter события содержит generation конкретного instance. Поэтому spawn
внутри callback не требует запрещённой регистрации owner в dispatch. Старый
callback не может обслужить новое поколение. Текущий callback slot не
переиспользуется внутри собственного исполнения.

Нативная занятость представлена отдельными512 B bits; опубликован только
первый снаряд в свободной MMap-клетке. Снятие проверяет собственное владение.
N10 получает borrowed bits через явный bind/unbind; низкоуровневый view без
overlay сохраняет исходную арифметику. Старые collision inputs заархивированы
до правки в `research/evidence/nds/n15/baseline/`, причина и hashes в manifest.
Герой/AI, статические карты и renderer используют один RU world; вывод
динамических instances добавляется к N12 list без подмены shared frame.

Контакт: MMap → TMap при class0/8; same-class immunity до target handler.
Спящий class9 должен будиться без damage (N16 seam), активный получает signed
SUB16/clamp, hurt/death завершает N17. Hero damage использует N14 stats.
Начальный Y берётся из шаблона, не из владельца; initial callback через416.
Time factor clamp256…2048; остаток до четвертей; контакт до ADD16/SAR decay.
**Уточнение по ASM:** обычный полёт сохраняет исходный repeat interval416,
а не интервал анимационного кадра. В RU1068:2C53…2C7B и EN1020:32F6…331E
после animation/decay есть только clock_start/RETF. При попадании
RU1068:2AA7…2B15 устанавливает прежний flight interval; затем callbacks эффекта
устанавливают интервалы своих кадров. Таблицы400/1500 относятся к изображениям,
их нельзя автоматически подставлять в частоту обычного полёта. Это расходится
с описанием GBA combat; NDS следует исходным инструкциям, не GBA-модулю.
Снаряды не получают GBA-патч проверки промежуточных клеток или новый aim assist.

Готовность7/3→2; интервалы исходных кадров, hide/show shifts и смена selected
на pending в source time. Select использует безопасный N14 цикл9 слотов;
source небезопасное разыменование current weapon при пустом inventory не
воспроизводится. Игрового inventory на диске нет.

## Реализация и ограничения

`ports/nds/core/combat.c`, `include/dok/combat.h` и
`render/weapon_overlay.c`. Состояние combat12948 B на ARM9, включая256
instances/512 B occupancy/9 weapon states. Ничего не выделяется в callback.
Pool service IDs0x10000…100FF отличаются от instance IDs257…512; generation
передаётся в event parameter. Удаление владельца немедленно снимает phase и
занятость. Внутри dispatch оставшийся service event отменяет себя по tombstone;
повторное использование ждёт его удаления. При завершении мира clear удаляет
все service owners и снимает collision overlay до уничтожения заимствований.

Frame источника отображается независимо от shared template. Billboard instances
добавляются к N12 list; `dok_combat_weapon_view` даёт экранную позицию оригинала:
left=SAR(319-width,1), top=159-SAR(height-2,shift). Условия исходного viewport
проверены ранее в `docs/originals/rendering-reference.md`: k8, scales1/1.
Overlay переводит исходную сетку0.8/0.96 в256×154 с прозрачным index0, сохраняя
все38 строк HUD. Перерисовка и её GPU composition входят в будущую общую
игровую оболочку/N17P; логическая ROM здесь не выдаётся за игровой playthrough.

При быстром Select pending сохраняет последний безопасный запрос; если оружие
ещё занято, hide начинается по достижении готовности. Это явная NDS input policy.
Native unsafe empty-current dereference и произвольный выход за weapon array
исправлены отдельно от исходного state machine. Коррекции GBA hit sweep/aim
assist не перенесены. Class9 hit подключается через typed service: в N15
проверен fixture wake/damage, настоящие AI/hurt/death принадлежат N16/N17.

## Проверки и воспроизведение

PY — общий Python3.14.6 из пути, указанного в N14. SDK/core прежние:
Zig0.13.0, BlocksDS1.24.0-1/GCC16.2.0, Unicorn2.1.4, melonDS DS1.4.0.

```powershell
& $PY -B -X utf8 ports/nds/tools/check_combat.py
& $PY -B -X utf8 ports/nds/tools/build_gameplay.py n15
& $PY -B -X utf8 ports/nds/tools/run_gameplay_gate.py n15 combat 0x15434b44
```

Host и реальная ARM9 suite совпали:313608 checks,34 сценария,108 callbacks,
trace hash1888275799; failures0. RU/EN presentation использует один RU world
с профилем N05; результаты при30/60 observation cadence совпали побайтно.
Проверены все реально заданные projectile templates LEVEL1–3, пустой/единственный/
двойной inventory, show/hide/fire, low charge, отказ allocation, полный pool,
повторное поколение, cleanup и каждый пиксель overlay с сохранением HUD.
Нулевой charge не создаёт projectile; остаток1 разрешает выстрел.

Unicorn **на каждый язык**:600 weapon callbacks,12 readiness,2000 decay,
400 distance/quarter и36 native slot scans; дополнительно3 полных normal-flight
callback с контролируемыми пустым lookup/target, animation и timer services
доказали отсутствие установки нового repeat interval при полёте. Другие
неизвестные far services отвергаются. Итого6096 scalar/function vectors и6
normal-flight callbacks. Контрольные внешние сервисы явно перечислены в отчёте;
полная DOS-сессия/скрытый scheduler closure ABI не заявляются.

Null-overlay collision regression:61824 исходных geometry queries,
1152 wrappers/3456 substeps; все совпали с x86. Host:107 collision checks,
9909 routes и прежние1 302 672 item checks прошли. Linux musl static скомпилирован,
не исполнялся. На ARM9: DS mode, built-in BIOS, software renderer, JIT off;
3584 frontend frames до3 наблюдений, RAM только читалась. FAT неизменён до/после
unload. Это не измерение игрового FPS. Физическая DS/DS Lite — N24.

Полные reports/source pins — `research/evidence/nds/n15/`; исходная база
Ghidra N14 сохранена. Коллизионный oracle N10 не изменён: N15 wrapper меняет
только безопасную выходную директорию нового отчёта. Отчёт сохраняет taskN10
как происхождение oracle, а не как изменение статуса N10.
ROM/ELF/map hashes — в N15 паспорте, build-manifest и компактном
`analysis/nds-n15-combat.json`. Исходники collision перед расширением сохранены
в source-supersession; прежние артефакты N14/N13P не переписывались.

Следующий ID N16: pool service и hit seam готовы; до реализации AI взять
N14 original actor/math/world, проверить getter-triggered wake, N+1 и course
trial, не подменять их глобальным LOS или GBA AI.
