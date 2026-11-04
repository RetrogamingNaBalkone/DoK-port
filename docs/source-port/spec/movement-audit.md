# SP10: независимая проверка движения трёх оригиналов

4 ноября 2026 года. Программная граница движения и коллизий принята:
**35 399** сравнений C++ с исходными инструкциями OG/PKR/PKE, расхождений0.
Это не семь полных прохождений и не замер DOS FPS. Маршруты выполняет SP16.

## Происхождение

Свежий read-only экспорт Ghidra 12.1.2 трёх образцов SP01 находится в
`research/evidence/source-port/sp10-movement/ghidra/{ru,en,old-gold}/`.
Manifest каждого содержит проект, диапазоны, SHA-256 EXE до/после и хеши
экспортированных инструкций. Драйвер: `ports/sdl2/tools/export_movement_audit.py`;
скрипт экспорта: `tools/ghidra/ExportSourcePortScheduler.java`.
Все EXE и RES до/после проверок неизменны. Старое исследование
[G08 collision](../../gba/collision.md) использовано только как указатель на
адреса и ABI Кремля; его движок, oracle и ROM не импортируются.

| Функция | PKR, Ghidra NE | PKE, Ghidra NE | OG, Ghidra MZ |
|---|---|---|---|
| Footprint | 1098:0448 | 1030:052c | 21b8:0412 |
| Resolver/скольжение | 1098:0901 | 1030:09e5 | 21b8:068f |
| Q15-кандидат | 1098:0e13 | 1030:0ef7 | 21b8:0cd6 |
| Три подшага/занятость | 1068:0eb1 | 1020:13bb | 1ae1:0dc0 |
| Elapsed motion | 1068:0a85 | 1020:0f8f | 1ae1:0908 |
| OG prepare/bob bounds | — | — | 21b8:0d90 |
| OG player bob rate | — | — | 1ae1:128f |

Это сегментированные адреса Ghidra, не file offsets и не runtime-селекторы.
Линкер OG использует эквивалентные MZ-алиасы, например far-цель
`1000:ec9a` соответствует `1ae1:3e8a`. Oracle перенаправляет их к тому же
набору исходных инструкций, сохраняя near/far return и 16-битный стек.

## Граница oracle и ABI

`check_movement_oracle.py` загружает свежие Ghidra байты actor/world в Unicorn.
Footprint, lookup, resolver, Q15-кандидат, изменение MMap и счётчиков дверей,
все три подшага и elapsed motion исполняют исходные инструкции. Их callees
не подменяются. Неизвестный внешний callee вызывает ошибку проверки.
Единственная подмена motion — контролируемые stopwatch stop/read/start,
возвращающие заданный elapsed. Это отдельная граница от принятого clock SP05.

Shared input тригонометрии — принятая ранее таблица исходной DOS RAM,
зафиксированная в `ports/sdl2/src/trig.inc`. Проверка исполняет исходное
Q15 умножение/SAR, но не запускает заново x87-инициализатор этой таблицы.
Host `sin/cos` не используются.

Footprint ABI: world far pointer, signed int32 X/Z, signed WORD radius.
Resolver: world far pointer, far pointers на signed int32 X/Z и два signed
int32 кандидата. Обёртка: actor far pointer, signed WORD distance, WORD strafe.
Все вызовы проверяют возврат SP на исходное значение+4. NE DS берётся из SS;
для OG допустимая real-mode раскладка заменяет DOS pointers. LES, RETF,
IDIV/CDQ, signed WORD narrowing и SAR2/SAR15 проверяются выполнением кода.
DOS pointers не становятся указателями C++: порт использует владельцев/индексы.

## Подтверждённые общие правила

Min Dis задаёт квадратный footprint, а не круг. Четыре угла проверяются в
MMap и TMap; проход разрешает bit10 flags объекта, а не class/sprite_flags.
FCMap и вертикальный Y не участвуют в этой проверке. Lookup делит signed
int32 с усечением к нулю, сужает индекс в signed WORD и прижимает его к карте.
Включены INT32_MIN/MAX и границы WORD-индекса, в том числе wrap X±radius.

Resolver сравнивает модули dx/dz; равенство выбирает ветвь Z. При блокировке
пробует центр клетки и край исходной клетки с отступом Min Dis и−1 на
положительной границе. Необычная асимметрия выбора центра Z сохранена и у OG.
Не заявляется эквивалентность resolver при переполнении разности координат
повреждённого мира: проверяем корректные settings и короткие signed подшаги.

Обёртка выполняет ровно три signed quarter-step, `SAR distance,2`, затем
Q15 sine/cosine и SAR15. На каждом подшаге исходный MMap очищается,
door occupancy уменьшается/увеличивается, новый MMap получает glyph актёра.
Сравниваются не только позы, но хеш MMap и все256 door counters.

## Исправления OG motion

Ранее порт назначал OG два ограничения Кремля и не переносил bobbing.
Исходный OG имеет cap elapsed scale **0x600**, Kremlin — **0x300**.
У OG нет Kremlin ограничения turn_step18. Эти различия теперь явные в
`update_actor_motion`. На обычном player interval416 cap не достигается,
но длинные elapsed и ускоренный поворот раньше давали неверный результат.

Native OG 1ae1:0908 меняет Y только при фактическом изменении X/Z.
Player ctor задаёт bob rate4, prepare фиксирует границы spawn Y±26.
Шаг bob — `(rate*scale) SAR8`, знак задаёт native+80; при пересечении
границы Y прижимается к ней и направление переключается. Стоя у стены,
герой не раскачивается. Порт теперь восстанавливает этот алгоритм;
интерполяция GL уже учитывает полный XYZ через ViewMotion.
Для остальных OG актёров rate0 и обе границы равны исходной высоте.

`MotionState::bob_direction` — новое сохраняемое поле **S** для SP17,
native+80, исходно1. Оно не выводится из текущей позы на точной границе.
Rate и bounds восстанавливаются из class/height_parameter; отдельное
изменяемое значение для них в текущих восстановленных путях отсутствует.
Повтор/загрузка должны согласованно восстанавливать это поле с MotionState.

## Проверки

| Группа | Объём | Фактическая граница |
|---|---:|---|
| Footprint/resolver | 32 641 | Все4096 центров каждой из7 карт;512 random short moves,35 явных edge/slide cases и20 экстремальных lookup на карту |
| Three-quarter wrapper | 700 | По100 исходных свободных поз на карту; signed distance −240…240, включая−121/−1, разные heading и strafe; pose/MMap/256 counters |
| Elapsed motion | 2058 | По294 на карту: elapsed0/100/416/832/1248/2496/20000, оси/turn/run, разгон и торможение, оба bob направления,40 последовательных ticks |
| Итого | **35 399** | **0 расхождений** |

Непрерывные последовательности сохраняют native состояние между40 вызовами,
проверяя смену направления bob на обеих границах. До исправлений OG дал
210 несовпадений из280 одношаговых motion-векторов, тогда как Kremlin
совпал полностью. После исправлений вся расширенная матрица совпала.

В C++ имеются отдельные tests независимых осей, инерции вкл./выкл., аналоговых
величин, смены направления, отпускания и30/60/144 FPS. Отключённая инерция,
дробная мышь и аналоговые оси — адаптации порта, не native DOS event bits.
Ручной slow InputActions для OG также остаётся опцией порта; native OG
не потребляет Kremlin surface slow flag. Нельзя выдавать этот режим за
доказательство наличия slow4000000 в OG.

## Защитные отличия и оставшиеся границы

`move_actor_to` не выпускает footprint за размеры карты, не стирает чужого
живого актёра и отклоняет занятую конечную позицию после resolver. Native
lookup лишь clamp-ит индексы и способен выпускать координаты за проходимый
край. Неизвестный glyph в порте блокирует проход; native может разыменовать
невалидный pointer. Эти защиты сохраняются отдельно от точного resolver.
Дополнительный portable projectile occupancy принадлежит SP11.

Clock/scheduler SP05 задаёт восстановленную временную основу. Фиксированные
30 фаз/334 ticks дверей и Kremlin class12 value/30 — ранее принятые адаптации;
эта проверка их не превращает в измеренный FPS DOS. Полные7 маршрутов,
включая реальные исходы, остаются SP16/SP18. Акустика — SP13, физический
контроллер — SP09. Эти работы не дублировались.

## Воспроизведение

```powershell
py -3.14 -B ports/sdl2/tools/export_movement_audit.py
powershell -NoProfile -ExecutionPolicy Bypass -File ports/sdl2/tools/build.ps1 -Configuration Release
py -3.14 -B ports/sdl2/tools/check_movement_oracle.py
build/sdl2/windows/dok_movement_tests.exe
```

Probe `dok_movement_probe RES LEVEL` получает stdin-векторы из драйвера.
Полные отчёты: `research/evidence/source-port/sp10-movement/report.json`
и отдельные `<profile>-<LEVEL>.json`; manifest/ASM/bin — в `ghidra/`.
Компактный итог: `analysis/source-port/sp10-movement-summary.json`.
