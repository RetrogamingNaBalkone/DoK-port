# UI01: пропущенные попадания лазера вблизи, 29 октября 2026

После уточнения пользователя — лазер, новая игра, первые комнаты LEVEL1,
вблизи промахи, после отхода попадания — воспроизведён пропуск клетки цели
между дискретными проверками полёта. Исправлен `ports/gba/src/combat.c`.
Точную пользовательскую игровую сессию не записывали; ниже описаны контрольные
позиции, воспроизводящие указанный эффект.

## Причина и изменение

Снаряд двигался сначала на остаток дистанции, затем четвертями клетки
(120 единиц для исходных480). Контакт проверялся только после каждого
перемещения. Лазер на первом обновлении идёт на80, затем на120 единиц.
Диагональный отрезок может войти в угол клетки мыши и выйти из него раньше
следующей проверки. Смена расстояния меняет положение проверяемых точек,
поэтому отход может вернуть попадания. Проверки с героем ровно в одной
клетке восточнее мыши этот случай не покрывали.

Пример из ARM/mGBA: герой `(19918,14641)`, heading71, мышь в клетке `(40,31)`.
Лазер: HP24→24, попадание пропущено. Герой после отхода60 единиц
`(19975,14621)`, тот же heading: HP24→20. После правки обе позиции дают
HP24→20. Ещё один случай — исходная клетка снаряда: ненулевой остаток
лазера позволял выйти из неё до первой проверки. Он также покрыт.

Теперь начальная клетка проверяется до движения. При пересечении обеих
границ клетки за один подшаг проверяется промежуточная клетка в порядке
пересечений. Порядок получается точным сравнением целочисленных произведений,
без float, деления на каждом пересечении и изменения траектории. При точном
касании угла соседние боковые клетки не добавляются. Стена, встреченная
раньше цели, продолжает останавливать снаряд. Урон, темп стрельбы, скорость,
правила классов/прозрачности/смерти, события и структура projectile40 не менялись.
Отвод эффекта назад на четверть клетки сохраняется.

Это осознанное уточнение столкновений GBA, а не утверждение о восстановлении
нового поведения DOS. Прежняя последовательность остаток/четверти/lookup
зафиксирована в [combat.md](combat.md), сохранённом Ghidra-listing
`research/evidence/combat/ru/evidence.asm`, PKR NE loader addresses
`1068:294d–2986` (не файловые смещения). Нового анализа EXE и DOS-сессии нет.
Пропуск воспроизведён и на ROM до оптимизации бочек:
`c12fff7033d02c6bc6cd2ddfd3d0c65ec5880ce2419b6cf50ac497973ab86844`.

## Проверки

- `verify_projectile_cells_gba.py`:330 ARM/mGBA cases, RU/EN, оба оружия,
  направления, задержки416/832/3328 ticks, начальная клетка, угол, порядок
  стены/цели. Независимый oracle делит каждый целочисленный отрезок точными
  рациональными временами всех пересечений сетки.122 случаев ошибочны в
  старой ROM,0 — в исправленной. Набор намеренно насыщен короткими
  пересечениями;122/330 не является частотой ошибки в обычной игре.
- `verify_close_hits_gba.py --ordinary-pickup`:16 живых случаев на ROM,
  четыре исходные мыши, оба оружия/RU/EN. Оружие подбирается на исходной
  TMap-клетке через production pickup; исходный заряд и show-анимация
  сохранены. Позиция героя задаётся тестом, HP32767 защищает от смерти;
  затем работают исходные AI/карты и настоящее удержание A. Все мыши
  получают промежуточный урон и погибают. Прохождение маршрута до оружия
  этот тест не воспроизводит.
- Полный G13 ARM: свободный полёт, оба оружия/заряд, пул, переключение,
  стена/дверь/монстр, эффекты,20 выстрелов с cleanup и обычный ввод прошли.
  Windows host combat suite,28 AI/combat cases, ARM renderer vectors,
  flow/UI/story/ceiling и294 transitions/5002 callbacks прошли.
- Комната бочек:64 замороженных PPU/coverage/geometry cases совпали,
  CPU after/before0,99985/1,00000/1,00019 (min/median/max). В прежних
  шести живых сценариях15,35–15,96 FPS сохранились. Это сценарии без
  удержания героем Fire, не оценка предельного числа активных снарядов.

## Сборка и команды

ROM `artifacts/gba/DoK-port_UI01.gba`:16551604 B, SHA-256
`c996593f060e29eef7bee66823702ad9e2e5910c08ec61e68e59d776138097bc`.
Сохраняет оптимизацию бочек и исправления предыстории/потолка.
ELF/map/полные отчёты — `build/gba/ui01-laser-hits/`, сводка —
`analysis/gba/ui01-laser-hits-20261029.json`. EWRAM261444 B/free700;
IWRAM24324 B и отдельный резерв8192 B стеков/BIOS. Наблюдавшийся main stack
2576 B, запас5360 B; IRQ80 B, запас80 B в сценариях комнаты бочек.

Общая установка Zig0.13.0, Python3.14, mGBA0.11-212-7a12d6d.
Original-backed пакет предварительно готовится по `docs/building.md`.

```powershell
py -3.14 -X utf8 ports/gba/scripts/build_gba_frontend.py --out build/gba/ui01-laser-hits
$candidate = 'build/gba/ui01-laser-hits/hardware/hw_v128_entity_rect'
$before = 'build/gba/ui01-barrel-optimization/hardware/hw_v128_entity_rect'
$reports = 'build/gba/ui01-laser-hits'
py -3.14 -X utf8 ports/gba/scripts/verify_projectile_cells_gba.py --build-dir $candidate --reference-build build/gba/ui01-playtest-fixes/hardware/hw_v128_entity_rect --report "$reports/projectile-cells.json"
py -3.14 -X utf8 ports/gba/scripts/verify_close_hits_gba.py --ordinary-pickup --build-dir $candidate --reference-build $before --report "$reports/pickup-hits.json"
py -3.14 -X utf8 ports/gba/scripts/verify_combat_gba.py --build-dir $candidate --report "$reports/combat.json"
py -3.14 -X utf8 ports/gba/scripts/verify_ai_combat_gba.py --build-dir $candidate --report "$reports/ai-combat.json"
py -3.14 -X utf8 ports/gba/scripts/benchmark_barrels_gba.py --build-dir $candidate --reference-build $before --report "$reports/barrels.json"
```

Остальные проверки: `verify_performance_arm.py`, `verify_flow_gba.py`,
`verify_ui_gba.py`, `verify_story_gba.py`, `verify_ceiling_sides_gba.py`,
`verify_gba_hardware_transitions.py --mode4-transitions` с теми же
`--build-dir`/`--report`; ARM/x86 vectors используют общую Unicorn-установку.
SHA исходников и полных отчётов привязаны в сводке. Физическая GBA и полное
прохождение не проверены. G21 остаётся в работе, бюджет G20 открыт;
следующий ID — G21, затем G22. NDS/SDL2 не затронуты.
