# N17P: живой мир, бой и GPU

29 октября 2026 года. Готово (эмулятор), решение — продолжать.
Основа — N13P и N14–N17; учтено исследование
[N17P-GPU](gpu-offload-assessment.md). GBA/SDL2 не меняются.

## Совместный runtime

`platform/gameplay_probe/` продолжает принятый NDS effects renderer. Одна
N08-очередь обслуживает героя, двери, воду, base animation, AI, оружие и
снаряды. Class9 не получает второй base-animation callback. Данные и баланс —
общий RU-профиль; EN меняет presentation resources.

Используются настоящий N08 clock, elapsed N09 и исходные deadlines, без
catch-up или фиксированного шага 30 FPS. До движения: consideration AI/эффектов,
ordered controls, pickup, floor/Use, combat, collision/motion. Stats из floor/Use
обрабатываются после возврата sink, без reentry. Service героя — 416 ticks;
C2 вычисляется исходной формулой N11 по elapsed этого service. Рассмотрение
base animations/воды перенесено туда же, как N16 consideration policy.
Это явная адаптация NDS для независимости от presentations, не точное DOS
draw/event interleaving.

Render gather читает live flags/corpse bits, добавляет все подходящие projectile
billboards, не меняет мир/очередь. «Спрайты» мира уже являются GPU-полигонами.
Оружие выводится в 2D bitmap с исходным sampling: stage в RAM и синхронный DMA
исключают byte writes в VRAM. После compaction raw LRU все pixel pointers
разрешаются заново до effect/stream plan. При reload: release scene, clear
combat/AI, reset N08, новая world epoch, затем load/bind. READY приостанавливает
clock. Corpse owner остаётся до teardown для ранее выпущенных снарядов.

## Кандидаты и опыты

| Кандидат | Измерение/опыт | Решение |
|---|---|---|
| Чтение кадров в бою | Пики prepare 50–130 мс при смене оружия/death | Lossless RAM store всех 168 кадров |
| Статическая геометрия | 64-pattern cache, линейный и прямой поиск | Выигрыш недостаточен; production выключен |
| Циклическая вода | Два повтора 96 texels, UV shift | Включено; noncyclic CPU fallback сохранён |
| CPU overlay | Полный viewport → точный видимый прямоугольник | Включено, 6000 побитовых сравнений |
| Retained packed lists/DMA | В этой итерации не реализованы | Следующий кандидат при измеренном renderer bottleneck |
| Аппаратный overlay | Ortho textured quad/BG требует проверки sampling, depth и fog | Кандидат для N19/N21P |

PackBits store находится внутри прежнего 1 MiB assets slab. RU 283947 B,
EN 283943 B вместо 1249728 B POT payloads. Каждый оригинальный payload проходит
N04 load/SHA/schema, pack и побитовый unpack roundtrip при loading. В игре
LRU распаковывает необходимые кадры без disk reads. Store/raw cache не
перекрываются, pinned frame не вытесняется. Анимации, actors, AI cadence,
точность попаданий и баланс не сокращены.

GPU-вода сохраняет logical period 96. Два повтора дают sampling extent 192
в 256×128/128×256 без wrap через padding 128. Residency key содержит ресурс
и ось; phase меняет UV, а не payload. Все 18 фаз/направления: 864 actual
texture pairs; 36 GPU raster pairs в двух видах дают одинаковые RGB 256×154.
Noncyclic zero fill не заменён clamp или прозрачностью.

## Проверки и границы

59 cases, 7060 новых presentations на сборку: 41 накопленный renderer case,
live start/move/turn, weapon pickup/fire/switch, combat и doors на LEVEL1–3.
All-awake и управляемая death fixture выделены отдельно. Combat/door fixture
получает inventory через pickup-транзакции исходных клеток; это воспроизводимая
начальная конфигурация, не прохождение от начала. Death fixture использует
clock 100000 для cleanup с отрицательным WORD tail; ранний wrap сохранён в N17.

Общий host runtime: 178111 checks, одинаковые world hashes в RU/EN и двух
cadence наблюдения 166/333 native ticks при одинаковых source poll/input.
Все 2464 его AI-callback сверены с оригинальными PKR/PKE active, hero event/stat
и compiler memcpy. Clock/front/sight/motion/allocation/audio имеют явные
внешние границы N17, это не полная DOS-сессия. Адресация —
[N17](N17-ai-combat.md), доказательства — source-replay.json. Реальный elapsed
зависит от blocking renderer, как у оригинала; равенство разных UI observation
cadences не объявляется равенством траекторий при разных задержках CPU.

Дополнительно: 7969561 pack/texture/water checks, 145242 geometry-cache checks,
6000 overlay checks. Linux static linked, не исполнен. Audit проверяет live
identities/maps, каждый pinned raw frame, reference overlay и DMA в VRAM.
Его стоимость опубликована отдельно. Прежние evidence pins сохранены.

## Воспроизведение

`$PY = 'C:/Users/dspev/AppData/Local/Python/pythoncore-3.14-64/python.exe'`.
BlocksDS 1.24.0-1 / GCC 16.2.0, Zig 0.13.0, Python 3.14.6, Unicorn 2.1.4.
Core melonDS DS 1.4.0:
`c7e6b58c667075a2a9868f2c3ba7a98e24200410a6da3051a7c555f1f0eef8ef`.

```powershell
& $PY -B -X utf8 ports/nds/tools/check_gameplay_performance.py prepare
& $PY -B -X utf8 ports/nds/tools/check_gameplay_performance.py host
& $PY -B -X utf8 ports/nds/tools/check_gameplay_performance.py before-release-ru
& $PY -B -X utf8 ports/nds/tools/check_gameplay_performance.py final-release-ru
& $PY -B -X utf8 ports/nds/tools/check_gameplay_performance.py final-release-en
& $PY -B -X utf8 ports/nds/tools/check_gameplay_performance.py final-release-audit
```

Для каждого label выполнить runtime (пример RU):

```powershell
& $PY -B -X utf8 ports/nds/tools/run_gameplay_emulator.py --target build/nds/n17p/final-release-ru --packet build/nds/n17p/scene.dat
& $PY -B -X utf8 ports/nds/tools/check_gameplay_raster.py
& $PY -B -X utf8 ports/nds/tools/summarize_gameplay_performance.py
```

DS mode, builtin BIOS/direct boot, software renderer, JIT/threading off,
DLDI read-only, network/sync off. Частоты — из timestamps новых presentations
при развёртке 59.826098 Hz, не из скорости ПК/libretro. FAT неизменна.
Игровых сохранений нет. Физическая DS/DS Lite, flashcart, стеки/IRQ — N24.

## Следующие бюджеты

Принимаются только реализованные подсистемы на эмуляторе. Цель 60 FPS не
достигнута. Проектный лимит следующих ARM9 работ: audio command submission
0.3 мс, dirty HUD 0.2 мс, incremental map 0.5 мс на presentation; суммарная
цель ≤1 мс. Это требования N18–N21P, не измеренный звук или карта. P99 запас
публикуется отдельно от worst component sum: upload после presentation
относится к следующему интервалу. Постоянный дополнительный worst-case бюджет
не гарантирован; N21P обязан проверить его заново.

Audio slab 262144 B уже отведён, reserve 655360 B сохранён. Main VRAM E 64 KiB
используется overlay/HUD, sub BG C 128 KiB — нижним экраном. Будущая карта
заменяет диагностическую консоль в своём 2D engine. SDK heap headroom/world
remainder публикуются итоговым отчётом. Reserve не расходовать.

Следующий ID — N18. После N17P остановиться перед ним.


## Итоговый профиль

| Вариант | Virtual FPS | Минимум окна ≥1 с | Штатные >2 VB |
|---|---:|---:|---:|
| Before RU | 31.838 | 15.692 | 142/5077 |
| Final RU | 38.916 | 29.913049 | 0/5077 |
| Final EN | 38.182 | 29.913049 | 0/5077 |
| Expensive audit RU | 14.225 | 9.496 | 5051/5077 |

RU normal prepare mean/p99/max 14.022/20.910/27.822 мс;
simulation 0.624/2.963/5.214 мс, submit mean 3.444 мс,
upload mean 0.091 мс. Component sum p99 27.810, max 34.221 мс;
P99 margin до двух VBlank около 5.62 мс. Max суммы включает upload после
границы и не равен текущему presentation interval. Все measured intervals
новых кадров production ≤2 VBlank. Poll→hero-service max 34.102 мс;
это не аппаратная end-to-end latency. Нормальные high-water: 8 AI / 3 projectiles;
all-awake 100 AI, RU 28.140 FPS, до 6 VBlank — отдельно от normal gate.

World 383864/393216 B, spare 9352 B; static startup 335872 B,
managed 2293760 B, SDK headroom 385024 B, reserve 655360 B.
Raw decoded cache не меньше 245856 B, packed RU 283947 B.
Максимум 287 polygons / 1154 vertices, textures 309248 B / 393216 B,
palette 8192 B / 16384 B, GPU overflow/underflow 0. Это ёмкость, не процент
загрузки rasterizer. Compiler .su max individual render_one 4360 B,
scene prepare 2608 B, door prepare 2528 B; сумма полного SDK/IRQ call chain
не измерена, stack 12288 B требует аппаратного N24.

28 240 новых presentations четырёх release-вариантов и ещё 74 presentations
парного GPU phase gate. Контрольные sources/ROM/ELF/map/results архивированы.
SHA и машинные метрики — [compact](../../../analysis/nds-n17p-performance.json),
полные records — research/evidence/nds/n17p/release/.
