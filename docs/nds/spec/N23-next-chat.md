# Передача N23 в следующий чат — 30 октября 2026

## Актуализация после продолжения

Сначала читать [N23-pipeline-audit](N23-pipeline-audit.md). По последнему
указанию пользователя оценивать крупные решения и оверинжиниринг для этой
конкретной NDS-игры до точечных правок. Замер154 тяжёлых кадров разделил
submit: validation2,37–2,50 мс, emission2,69–3,15 мс. Предварительный путь —
статические данные при загрузке и отдельные doors/sprites/water; общего
cache/invalidation framework не нужно без доказанной необходимости.
Текущие pickup/corpse writers сохраняют базовую геометрию (host proof PASS).
Current C12 equivalence, ARM-выигрыш и память нового представления ещё открыты.

C12 EN и C12 SCN RU/EN теперь готовы:63 cases/12880 presentations на банк,
normal6637 intervals все≤2VB/min29,913049; RU51,346931/EN51,247524 FPS.
EN all3 live/LCD PASS, touch42/max6VB. Natural61 и полные ordinary маршруты
по-прежнему не приняты; новые tours13/14/15 дали52/19/15 AI без FINISH.
Production C12 не перезаписана, оптимизаций продукта в продолжении нет.
Новая диагностика `pipeline14-scn-ru` не является product/FPS candidate.
Все эти запуски завершены; frozen graph — `research/evidence/nds/n23/review13/`.

Следующий ID — **N23**, следующий шаг — изолированная ARM-пара для простого
статического представления. После выбора вернуться к natural61 и полным
приёмочным маршрутам. **N24 не начинать.** Старые EN/SCN «открыто» и запрет
новых запусков ниже описывают момент прежней передачи, не текущий запрет.

## Исходная передача до продолжения

Пользователь предложил передать работу в новый чат. **N22 принята на эмуляторе;
N23 выполняется и не принята. N24 не начата.** Новые сборки и прогоны остановлены;
на момент передачи живых emulator cores и SDK-сборок не осталось.

Начать с `AGENTS.md`, [плана](../plan.md), [handoff](../handoff.md),
[паспорта N23](../tasks/N23.md) и [результатов измерений](N23-performance.md).
Другие направления разработки не менять. Изменения GBA и обслуживания
хранилища в рабочем дереве существовали отдельно от этой NDS-итерации.

## Контрольная сборка

`build/nds/n23/candidate12-ru/`, durable-копия —
`research/evidence/nds/n23/candidates/candidate12-ru/`.

- ROM SHA-256: `f7049e2c7ec1900d9d644503ea359abbd3d7c622a4aae01de0e503aecaf36bdc`.
- ELF SHA-256: `91ce5a3fc646651bb2802a79784779bb4339049ff2ddd4c30c2728cc9f010dc3`.
- Манифест привязывает реальные ROM/ELF к frozen исходникам и флагам; SDK-сборка
  проверяет неизменность C/H/inc/Makefile до и после компиляции.
- Производственные fine/audit-таймеры экспериментов выключены. Лёгкие штатные
  профили, GPU/audio/error/latency-наблюдение сохранены. Эмуляторные результаты
  не подтверждают физические DS/DS Lite; аппарат и прослушивание отложены до N24.

Оригиналы и игровые сохранения не изменялись. На microSD разрешены только настройки.

## Что изменено и доказано

Помимо более дешёвой подготовки геометрии, декодирования и пакетного вытеснения,
независимый аудитор нашёл лишнюю работу, которая действительно выполнялась:

- Повторный одинаковый weapon overlay: сохранение точного изображения в VRAM,
  затем пропуск ненужной raw-admission при сохранённом изображении. Новый view,
  смена мира/восстановление экрана и audit используют полный путь. Scratch/raw
  pointers не сохраняются; demand pointers заново разрешаются после requires.
- DWORD SAR через software int64 division заменён точной 32-битной формулой.
  Проверены 34 114 739 значений/сдвигов; actual ARM9 object лишился двух
  `__aeabi_ldivmod` callsites. Значимые 64-битные cull/UV-вычисления оставлены.
- При равном размере одной жертвы и нового payload используется её физический
  слот. Логические LRU, порядок records, pins/ages/counters и каждый живой texel
  сохранены; offsets могут переставляться. Guard проверяет непересечение всего
  packed store с raw cache. Alias-контрпример аудитора закрыт.
- В карте убраны повторные `seen()` вызовы с уже известными границами и class1
  prefix-правилом. Без дополнительной RAM, с теми же native pixels. Dirty key
  угла использует точный сектор трёхпиксельной стрелки.
- На уже принятом решении перерисовать HUD одна передача 9728 bytes заменяет
  чтение VRAM и множество коротких DMA. Решение о перерисовке, HUD/overlay
  граница 154/38 строк и publication lifetime сохранены.

Контролируемые ARM9 пары: equal-slot request 4,333→0,908 мс;
map raster example 10,690→8,303 мс; HUD 2,106→1,191 мс.
Это отдельные измерения одного и того же входа. Средние FPS разных обычных
прохождений имеют разные poses/длительности и не являются чистыми причинными парами.

## Фактические обычные прогоны

Порог: ≥99% интервалов ≤2 VBlank и **каждое** окно ≥1 с имеет cadence
не ниже половины развёртки, здесь 29,913049 FPS. Среднего FPS недостаточно.

| Сборка/маршрут | Результат |
|---|---|
| C12 LEVEL2, 220 cells | 5115 intervals; 1788×1 VB + 3327×2 VB; 0×>2; min 29,913049; avg 36,248578. PASS этого частичного маршрута. |
| C12 LEVEL1, `root-full-exit12` | Все keys7, 20 kills, два retry, затем Game Over; **не FINISH**. 8149 intervals; 1935×1 VB + 6214×2 VB; min 29,913049; avg 33,942970. FPS sample PASS, прохождение не завершено. |
| C10 LEVEL1, V18 | Все keys7, 31 kills, затем Game Over. 12653 intervals, 13×3 VB, min 27,918846: отрицательный контроль. Его плотная карта/HUD послужили входом последних исправлений. |
| C08 LEVEL3 | 47 естественных AI, частичный sample budget PASS, затем Game Over. |
| C03 LEVEL3 | 56 естественных AI, 2802 кадра с ≥50 AI; min 24,518893: отрицательный контроль старой сборки. |

Западное скопление с первым ключом LEVEL1 включено в C10/C12. Его не обходили
как необязательную нагрузку. Natural61 исходного N22 ещё не повторена на C12.
Artificial all-awake считается отдельно и не заменяет этот критерий.

C12 RU live LCD всех трёх миров также завершён: 5 701 632 INDEX8/RGB checks,
2 555 904 map checks, 3736 text checks. Подробные paths и latency — в основном
паспорте/спецификации. Touch: 42 responses, максимум 6 VB /100,291 мс.
Static RAM507904, доступный SDK heap212992, reserve655360; GPU sticky0.
Цель60 не принята: в live sample 1,2527% интервалов >1 VB. C12 EN и итоговая
накопленная RU/EN-матрица ещё открыты.

## Доказательства и состояние инструментов

Полные материалы находятся локально в `research/evidence/nds/n23/`:

- `root12-ordinary-review.json` независимо пересчитывает все 8119 секундных окон
  C12 LEVEL1 и проверяет 42 frozen pins. Archive:
  `route-profiles/root12-level1-exit-negative01/manifest.json`, SHA
  `b4e39e09ed2379ebb21f96e1b3d2effdde4cab459fb602b95c859c331e079132`.
- `root-cache-review.json`: flag0 — 64 697 случаев, прежний физический layout
  побитово совпадает с N22. `root-cache-logical-review.json`: flag1 —
  64 825 cases, 30 000 ordered sequences, 128 mixed вариантов с перестановками,
  counter wrap, tied LRU и capacity; 61 882 532 logical assertions.
  Guard bytes считаются отдельно uint64. Старый guard-heavy переполненный
  счётчик checks исправлен и не используется как доказательство объёма проверки.
- `root-hud-dma-review.json`: 6000 сценариев, 294 912 000 display bytes;
  DMA читает модель уже flushed RAM, проверяются scene/HUD guards, UI state,
  publication sequence и actual upload telemetry.
- `root-review-inputs.json` и `root-review-inputs/` сохраняют точные версии входов
  root-проверок. `auditor-summary.md`, `auditor-findings.md`,
  `auditor-inputs.json` и `auditor-inputs/` — отдельный граф независимого аудита.
  Аудитор закончил для handoff: 12 reports / 164 exact input versions.

Не менять frozen N22-архивы. Крупные отчёты/ROM/RES не добавлять в Git;
обновить компактные summaries по `analysis/report-policy.json` при завершении N23.

## Следующий шаг

1. Проверить C12 hashes и текущие положительные/отрицательные отчёты, затем
   закрывать оставшийся набор приёмки: поздний ordinary LEVEL3 с фактическими
   ≥61 AI, итоговые RU/EN, audio, LCD/touch latency, memory/reserve/stack/GPU.
2. Для LEVEL3 исходное успешное N22-прохождение сохранено в
   `research/evidence/nds/n22/route-profiles/level3-ru-current02-complete01/`.
   Key masks/cells: 2 — (52,52), 1 — (51,8), 4 — (23,25). Нужны живые input
   решения, учитывающие бой, HP и retry; новый CPU timing меняет trajectory.
3. `root-perimeter-plan.json` — только host-план по actual C bounds/N05 seeds:
   cardinal outer-cell centres покрывают59 исходных AI, восемь направлений60.
   Старое предположение «59+3 за один диагональный взгляд» не подтверждено
   strict activation distance. Это не доказательство невозможности61 при
   subcell/inner poses и не результат эмулятора.
4. Для повторного LEVEL1 имеется `ports/nds/tools/performance/run_saved_routes.py`
   с неизменной V18-политикой и metadata-only N23 derivation. Экспериментальный
   `run_exit_routes.py` пока ненадёжен: после всех ключей он выбрал140-cell exit
   path при низком HP и пропустил нужную еду. Не считать его готовым автопрохождением;
   разумный следующий вариант ограничивает exit preference близким выходом.
5. Новые оптимизации выбирать по новому фактическому bottleneck. AI/course/
   motion/prebound-animation идеи аудитора ещё не имеют ARM-пары и не приняты.
   Point/clip caches ранее измеренно ухудшили результат и выключены.

После программной приёмки N23 обновить паспорт/план/handoff и остановиться
перед N24. Сейчас следующий чат продолжает **N23**, а не начинает N24.
