# N21P: точное ускорение producer раскрытия карты

Этот этап сохраняет принятый N20 native gather, порядок constructor records,
Shell gap sort,320-column visibility и исправление ошибочной projectile-клетки.
Ни частота вызовов, ни результат раскрытия здесь не приближаются.
Изменения касаются только `ui/map_discovery.c` и его временного workspace.
Не используются GBA-код или готовые GPU-quads: N07 visual gather исправляет
исходный AABB и отбрасывает невидимые поверхности, поэтому его результат
не заменяет исходный producer карты.

## Signed32 near clip

Исходные constructor edge: PKR `10B0:06AA`, PKE `1040:06B6`.
Адресная система — Ghidra NE loader, не файловые смещения и не DPMI selectors.
Полные исходные ссылки и сохранённые exports — в
[N20 discovery evidence](N20-discovery-evidence.md).

`rotate` сначала сохраняет signed low32 результата умножений/сложения.
Затем арифметический SAR15 даёт глубину в диапазоне **−65536…65535**.
Вертикальный constructor сразу возвращает невидимую record, если обе
глубины ≤0. Поэтому ветка near clip имеет одну глубину строго отрицательную,
а вторую строго положительную. Разность положительной и отрицательной
глубин находится в **2…131071** и не переполняет signed32.

Числитель `n` уже приведён к signed low32 до деления. Старое выражение
заново расширяло этот signed32 до int64 и вызывало software `__aeabi_ldivmod`.
Новое делит те же значения как signed32: оба варианта отбрасывают дробную
часть к нулю. Особый overflow `INT_MIN / -1` невозможен, поскольку делитель
строго положительный. Частное помещается в signed32. Следующее сложение
с endpoint lateral по-прежнему выполняет прежний wrap low32.
Это замена реализации арифметики, не исправление исходного поведения.

Отдельный gate проверяет72 граничные пары числитель/делитель и2880 actual
native PKR/PKE constructor vectors: координаты у signed32 и SAR15 wrap-границ,
ширины1/2/32767, четыре ориентации и четыре камеры. Он не заменяет основной
набор121776 native geometry vectors. Shared ARM GCC16.2 в оптимизированном
ASM больше не вызывает `__aeabi_ldivmod`; signed32 `__aeabi_idiv` сохранён.

## Повторные sprites и projection factors

Раньше для каждой клетки producer дважды просматривал весь список sprites.
Временные heads по cell tag и next по индексу связывают только исходные
world-cell sprites внутри точного native AABB. Построение идёт с конца списка,
поэтому обход каждой цепочки сохраняет прежние возрастающие индексы.
Отдельные проходы plane1, затем plane0 сохраняют TMap-before-MMap; порядок
равных sort keys не меняется. Synthetic/pool IDs по-прежнему исключены.
Heads инициализируются только внутри текущего gather, вне его не читаются.

Projection factor `6553600 / denominator` запоминается в256-entry временной
таблице. Ключом служит точный signed denominator; collision только заменяет
entry. Нулевой denominator отклоняется до обращения к таблице, поэтому0
может обозначать пустой entry. Cache не меняет signed division или rounding.

Workspace вырос с35268 до**46532 B**, остаётся внутри принятого общего
scratch131072 B и не удерживает borrow между вызовами. Постоянная память0 B,
мир, scene и SDK reserve не меняются.

## Компактный geometry producer

Первый checkpoint всё ещё вызывал полный N07 pure geometry builder.
Совместный runtime показал mean producer9.301 ms и p9913.58 ms на EN;
бюджет оставался непройденным. До следующей правки exact source и отчёты
сохранены в `research/evidence/nds/n21p/pre-compact/` с `pins.json`.

Второй checkpoint строит только входы исходных constructor, которые читает
disclosure: X/Z endpoints, kind, cell, glyph и flags. Порядок ceiling bands,
walls либо floor, затем centered doors сохранён. Парная ceiling по-прежнему
не создаёт самостоятельную native record. Signed WORD ширины, отрицательный
center half, source flags и размеры горизонтального floor сохранены.
Дверной UV mirror не меняет геометрические endpoints и не читается disclosure.
Полный N07 builder продолжает обслуживать верхний renderer без изменений.

Сохранены все проверки определений/соседей/field54 и material/width/height
bindings, включая ceiling image, которая сама не попадает в записи карты.
Все shapes клетки валидируются до публикации её records. Исключены только
нечитаемые Y/UV/material inputs, их software divisions и полные
`memset`+copy1108-byte cells. Shared ARM GCC16.2 сообщает для
`dok_map_discovery_update`544 B static stack вместо2448 B; собственный stack
`record_geometry`112 B. Это локальные frame sizes, не измерение аппаратного
stack high-water.

## Точные modulo32 rotate и SAR

До третьего checkpoint compact producer и его доказательства сохранены
в `research/evidence/nds/n21p/pre-math/` с `pins.json`.
ARM ASM показал, что исходный переносимый SAR через int64 division разворачивался
в длинную отрицательную ветку, а rotation вычислял ненужную старшую половину
SMULL/SMLAL. Это отдельные затраты после удаления geometry Y/UV.

Новая rotation выполняет вычитания, умножения и сложения как uint32 modulo2³².
Преобразование signed operand в uint32 сохраняет его остаток modulo2³²;
операции в этом кольце дают тот же low32, что прежние int64 операции с
последующим wrap. Signed32 delta×sin/cos и их сумма помещаются в int64,
поэтому исходная форма не имела скрытого int64 overflow. Функция `bits`
возвращает ровно signed значение этих32 бит через прежнее безопасное
разложение; cast unsigned значения за пределами INT32_MAX не используется.

Для SAR используется unsigned logical shift с явным sign fill:
`(u >> b) | ((0u - (u >> 31)) << (32u - b))`.
При1≤b≤31 все shift counts определены. Верхний бит u даёт0 либо1;
unsigned отрицание превращает его в0 либоFFFFFFFFh. Его сдвиг заполняет
ровно верхние b бит. Получается signed floor(n/2ᵇ), включая INT_MIN,
который совпадает с прежним арифметическим SAR. Реализация не зависит
от implementation-defined signed right shift. В production helper получает
только b1/2/15/20; actual C gate дополнительно проверяет все b1…31.

Actual private helpers production-файла проверены на6855608 SAR и604800
rotation vectors против принятых int64 формул: полный диапазон глубин SAR15,
случайные full signed32 operands, крайние координаты/камеры и sin/cos.
Сравнение producer теперь также включает весь46532-byte workspace compact
checkpoint до math-замены. Сами compact cell branches не менялись.

## Удаление повторного clear в верхней geometry

Следующий узкий шаг изменяет только одну строку NDS `render/geometry.c`:
удалён `memset(quad,0,sizeof(*quad))` из translation-unit-local `append`.
Все пять call sites находятся после полного `memset(candidate,0,1108)`;
`count` после него0 и монотонно выделяет новый slot, максимум8 на клетку.
Следовательно92-byte clear каждого нового quad повторно записывал уже нули.
Вызовы builder до этого clear возвращаются без обращения к `append`.
Ни одна ветка geometry, UV/координата, validation или error status не менялась.

Exact старый source сохранён в `pre-geometry/` с pins. Новый host gate
проверяет все3×4096 клеток с тремя sentinel patterns:36864 базовых случаев.
Дополнительно491520 binding mutations меняют material/width/height на0 либо
неподдерживаемый32768. Actual old/new status, весь1108-byte candidate и
весь1108-byte out совпали, включая165690 error returns и неизменность out
на ошибке. Команда: `py -3.14 -B -X utf8 ports/nds/tools/map/check_geometry_clear.py`.

Доказательства — `geometry-clear-equivalence.json` и узкий
`geometry-source-supersession.json`. Последний связывает только старый pin
`render/geometry.c` в constructor/discovery reports с exact архивным source
и новой побайтно эквивалентной реализацией. Остальные pins проверяются обычно.
Прежние native reports не переписаны, их executable/constructor replay
не повторялся ради удаления доказанно повторной записи нулей.

## Воспроизведение и границы

```powershell
py -3.14 -B -X utf8 ports/nds/tools/map/check_discovery_optimization.py
py -3.14 -B -X utf8 ports/nds/tools/map/audit_discovery_clip_edges.py
```

`research/evidence/nds/n21p/initial/discovery-pins.json` связывает exact
принятые N20 исходники с архивными копиями до оптимизации. Windows host
дифференциально исполняет обе реализации в одном executable.1536 случаев
на трёх RU-уровнях покрывают0…512 sprites, произвольный порядок/дубликаты
cell/plane, runtime flags, synthetic IDs, камеры и начальный visited.
Records, sort order, columns, count, visited, status и statistics совпадают
побайтно: сравнивается общий35268-byte workspace prefix с принятым N20 и
весь46532-byte workspace с первым N21P checkpoint.
Ещё45736 validation/adversarial cases, включая661 actual error returns,
сверяют обе прежние реализации с compact producer. Они изменяют binding
dimensions/material, glyph lookup, field54 kind/count/value, signed widths,
language и settings.24576 poison vectors меняют все неиспользуемые поля
quad четырьмя patterns; actual projector сохраняет результат и совпадает
с принятым N20. Свежий полный native gate использует неизменённые N20 exports,
сверяет SHA оригиналов до/после и пишет новый `discovery-native.json`.
Принятый N20 report не перезаписывается.

Итоги — `discovery-equivalence.json`, `discovery-validation.json`, `discovery-math.json` и
`discovery-clip-edges.json` в N21P evidence.
Linux strict static executable только cross-linked, не запускался.
Host CPU ticks не являются производительностью Nintendo DS. Реальный вклад
в совместный бюджет подтверждается отдельными ARM9/melonDS измерениями
основного N21P этапа; физическая DS/DS Lite остаётся N24.
