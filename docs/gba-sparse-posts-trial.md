# G20C: native sparse posts, 27 октября 2026 года

**Опыт завершён; новый путь отклонён.** Полный кадр стал медленнее,
картинка в проверенной выборке совпадает с контролем. Основным остаётся
ROM2775. Следующая задача — UI G20B; бюджет G20 остаётся открытым.
Это результат конкретной реализации, не доказательство невозможности
ускорить любую sparse-реализацию.

Итог: [analysis/g20c-sparse-posts.json](../analysis/g20c-sparse-posts.json).
Исходные отчёты, PNG, ROM/ELF/map и снимки исходников —
`research/experiments/g20c-sparse-posts/`; чистое повторение — `research/experiments/g20c-sparse-repeat/`.

## Реализация и контракт

Контроль `hw_v128_entity_rect` побайтно повторяет
`2775bdd0caaac64b4fd99e4c6d675587fad2da9c795468dc52efacb5d53edaab`.
Опыт `hw_v128_sparse_posts` сохраняет CPU120×128, LIGHT16, аппаратные базовые
плоскости, clipping, геометрию, дальность, consideration leases и игровую логику.
Он включается только отдельным define `HW_SPARSE_POSTS`; default не изменён.

`scripts/gba_sparse_posts.py` строит auxiliary банк из **post-LIGHT** колонок
для всех16 строк освещения. На изображение идут uint32 offsets колонок,
на колонку — uint16 count и пары uint16 `[begin,end)` исходных строк.
Одинаковые списки интервалов делят запись независимо от цветов.
Исходный банк цветов неизменён. Метаданные занимают498688B:
1072 bank images,96518 колонок,2901 уникальная запись маски. Прежняя offline
оценка1081206B дедуплицировала колонки по цветам; новый банк дедуплицирует маски.

`src/renderer_sparse_posts.inc` преобразует исходные интервалы в диапазоны
**той же Q16-последовательности** `v+n*step`. Двоичный lower_bound по числу
выходных строк сохраняет повторяющиеся texels, minification, дробное начало,
clipping и step0. Пустой экранный интервал не рисуется. Непрозрачные интервалы
идут в существующий ARM `dok_gba_tiled_opaque_gaps` с прежними перекрытиями
и coverage. Число доступных строк считается по исходному coverage; результат
`written | (transparent<<16)` сохраняется. Подключены general и rectangle paths.
Для ресурсов без prelit-колонки указатель posts сбрасывается, работает fallback.

Пропуск разрешён только masked материалу. Ноль непрозрачной текстуры выводится
через black alias; ARM rectangle-векторы проверяют оба контракта.
Анимации без prelit-банка сохраняют прежний sampler.

Оркестрация posts и поиск границ находятся в ROM. Даже при этом IWRAM
переполнилась на76B; для опыта `edge_start` возвращён из IWRAM в ROM.
Отдельный `hw_v128_sparse_control` имеет то же размещение с прежним sampler:
на heavy5 его полный CPU дороже2775 только на0,16–0,64%. Основное замедление
связано с новым путём. Цена отдельных операций posts отдельно не профилировалась;
влияние ROM вызовов и поиска границ — объяснение реализации, не измеренная
атрибуция стадий. Более специализированный ARM sparse kernel не исследовался.

## Парные native-clock замеры

mGBA libretro0.11-212-7a12d6d, CPU16777216Hz, WAITCNT4317, replacement BIOS,
без overclock. Core SHA-256
`5cac11c643d7f8dfa50c68855f2f2240ff7cc0b4cd0312dd9e17a639c85667e4`.
Измеряется полный CPU-кадр; FPS предъявления считается отдельно по VBlank.
Capture hold выключен во время timed samples; output сравнивается после них
на завершённом удержанном кадре. Heavy/spatial: frozen simulation,
PCM22050Hz активен во всех шести samples после шести прогревочных предъявлений.

|RU вид, world/cell/heading|CPU2775|CPU sparse|Изменение CPU|FPS2775 → sparse|
|---|---:|---:|---:|---:|
|L1/520/225|1457472|1707840|+17,18%|9,95 → 9,43|
|L1/3640/225|1390592|1525344|+9,69%|11,95 → 9,95|
|L2/1592/135|1476352|1894304|+28,31%|9,95 → 8,53|
|L2/2871/225|1524224|1793696|+17,68%|9,95 → 8,53|
|L3/1544/315|923040|1171776|+26,95%|14,93 → 12,80|

Пять видов повторены наEN: CPU+9,67–28,32%, те же FPS предъявления.
Небольшие отличия CPU от старых отчётов2775 относятся к новому парному прогону;
здесь сравниваются только новые контроль и кандидат.

Spatial440 повторён **на каждом языке**: всего880 парных поз.

|Язык/мир|Медиана изменения CPU|Быстрее/медленнее/равны|Минимум FPS2775 → sparse|
|---|---:|---:|---:|
|RU/L1|+4,51%|0/136/0|9,95 → 9,19|
|RU/L2|+1,03%|5/163/0|9,95 → 8,53|
|RU/L3|+5,64%|0/136/0|14,93 → 11,95|
|EN/L1|+4,57%|0/136/0|9,95 → 9,19|
|EN/L2|+1,05%|5/163/0|9,95 → 8,53|
|EN/L3|+5,58%|1/133/2|14,93 → 12,36|

Максимальная локальная экономия CPU около1,95%; общего выигрыша нет.
Все880 пар совпали по actual PPU RGB, foreground VRAM indices, coverage
и всем публичным renderer stats. Spatial — распределённые доступные клетки,
не минимум всех возможных игровых состояний.

Живые сценарии используют реальную камеру игрока, AI, движение и огонь,
по120 предъявлений на трёх уровнях и обоих языках.

|Маршрут/язык|L1, FPS2775 → sparse|L2|L3|
|---|---:|---:|---:|
|Поворот/огонь, RU иEN|19,85 → 18,76|16,86 → 15,25|21,79 → 20,48|
|Ходьба/поворот/огонь, RU|17,96 → 17,19|17,92 → 17,48|20,54 → 19,58|
|Ходьба/поворот/огонь, EN|18,10 → 17,19|17,92 → 17,31|20,54 → 19,58|

PCM underruns0, time-conservation error0, BG overflow0; simulation debt
сохраняется. Начальный PCM loop22050Hz может замещаться обычным звуком игры.
В turn120 звук активен120/120 предъявлений; в walking120 у кандидата
L1/L2/L3 —120/21/66. Это обычное звуковое поведение маршрута, а не непрерывный
forced-PCM во всех живых кадрах; оба active-count сохранены в отчётах.
Разное elapsed time меняет динамику, живые кадры не служат pixel oracle.

## Проверки и память

- Независимое восстановление10949206 texels из linked sparse records прошло
  для96518 колонок/all16 LIGHT. Байты sparse banks и prelit-банка в ROM exact.
- Unicorn:4096 sparse vectors со случайным coverage, пустыми, сплошными
  и перемежающимися масками, увеличением/уменьшением и step0;512 rectangle
  vectors для masked и opaque zero/black. Проверены буферы, guards,
  callee-saved registers и стек; остальные sampler/projection/division/
  geometry/animation vectors общего ARM suite также прошли.
- Помимо880 spatial и10 heavy пар,396 detail PPU кадров RU/EN полностью
  совпали: оружие, HUD, ключи, сообщения, масштабирование и шесть завершённых
  Pause/Mode4/Resume cycles; coverage exact. Контрольные PNG просмотрены.
- 48 парных mutation-состояний: движение актёра, TMAP/MMAP edits,
  закрытая/полуоткрытая/открытая дверь, cache invalidation и leases.
  Actual PPU RGB, geometry и leases exact.
- Suites sprites, interactions, AI/combat иUI прошли. Interaction suite
  сохраняет door/collision/sensor/conveyor/reset проверки; прежний bitmap
  oracle базовых плоскостей отключён для аппаратного renderer.
- 282 camera/weapon/message перехода,5002 actual PPU callbacks:0 mixed frames.
  Это не повтор294-case Pause/Resume oracle: его прежняя проблема промежуточных
  кадров у2775 остаётся ограничением. Конечный кадр после шести Mode4 cycles
  проверен отдельно в detail сравнении.

|Память|2775|Sparse trial|
|---|---:|---:|
|ROM bytes|15938936|16438720|
|EWRAM used/free|261292/852|261300/844|
|IWRAM used/free до стека|24408/168|24544/32|
|Отдельный резерв стеков|8192|8192|

Наблюдаемый main high-water2528/margin5408B, IRQ80/margin80B.
Это наблюдение проверенных маршрутов, не доказательство всех путей игры.

Опытный ROM SHA-256
`7dfe60cff69b2862cfef30c58bac43f64ef9e26d32a96aa9f77e66600d099e94`.
Копия: [DoK-port_sparse_posts_trial.gba](../artifacts/gba/DoK-port_sparse_posts_trial.gba).
Все три ROM в чистом повторении byte exact, включая2775 и placement control.
ELF/map сохранены в обоих деревьях; debug paths могут различаться.
Контрольные128/96/72 ROM сохранены. Объекты опытов удалены сборщиком по
протоколу CLEANUP; базовые объекты `select-menu-fix` сохранены.

Данные — G03 package SHA-256
`3efa5d3027d63ffd65e0ae8c0b45be23ee8394b3c5151c8e9f10e76fa8674351`.
Адреса метаданных — offsets package/банков; адреса кода и linked payload —
GBA ELF/ROM VMA из map. Это не NE-сегменты, Ghidra-адреса или DPMI-селекторы.
Нового анализа EXE не было, оригиналы не изменялись. Физическая GBA,
оригинальный BIOS и полное прохождение не проверены.

## Воспроизведение

Общие инструменты и сохранённая база — [building.md](building.md).
Оба дерева используют `build/select-menu-fix` и `build/gba/assets/assets.bin`.
Сборщик сохраняет provenance базовых объектов, package и исходников.

```powershell
$py = 'C:/Source/wasdify/.venv/Scripts/python.exe'
$env:ZIG_GLOBAL_CACHE_DIR = 'C:/Source/DoK-port/build/zig-cache'
$env:DOK_PROBE_FAST_VIDEO = '1'
& $py scripts/prototype_gba_bsp.py --out research/experiments/g20c-sparse-posts --variants hw_v128_entity_rect hw_v128_sparse_control hw_v128_sparse_posts
& $py scripts/prototype_gba_bsp.py --out research/experiments/g20c-sparse-repeat --variants hw_v128_entity_rect hw_v128_sparse_control hw_v128_sparse_posts
foreach ($mode in @('heavy','spatial','live')) {
    & $py scripts/benchmark_gba_sparse_posts.py --mode $mode
}
& $py scripts/verify_performance_arm.py --build-dir research/experiments/g20c-sparse-posts/hw_v128_sparse_posts --report research/experiments/g20c-sparse-posts/arm.json
& $py scripts/benchmark_gba_hardware.py --build-dir research/experiments/g20c-sparse-posts/hw_v128_sparse_posts --reference-build research/experiments/g20c-sparse-posts/hw_v128_entity_rect --report research/experiments/g20c-sparse-posts/ppu-details.json --languages 0 1 --details
& $py scripts/verify_gba_static_cache.py --build-dir research/experiments/g20c-sparse-posts/hw_v128_sparse_posts --reference-build research/experiments/g20c-sparse-posts/hw_v128_entity_rect --report research/experiments/g20c-sparse-posts/mutations.json
foreach ($suite in @('sprites','ui','ai_combat')) {
    $name = $suite.Replace('_','-')
    & $py "scripts/verify_${suite}_gba.py" --build-dir research/experiments/g20c-sparse-posts/hw_v128_sparse_posts --report "research/experiments/g20c-sparse-posts/$name.json"
}
& $py scripts/verify_interactions_gba.py --build-dir research/experiments/g20c-sparse-posts/hw_v128_sparse_posts --report research/experiments/g20c-sparse-posts/interactions.json --skip-visual-oracle
& $py scripts/verify_gba_hardware_transitions.py --build-dir research/experiments/g20c-sparse-posts/hw_v128_sparse_posts --report research/experiments/g20c-sparse-posts/transitions.json
& $py scripts/report_gba_sparse_posts.py --copy-rom
```

Benchmark driver возобновляет готовые отчёты с проверкой ROM/script SHA.
Для повторного замера использовать новый `--out` с новым build tree или
удалить только соответствующий старый JSON. Отрицательные full-frame данные
закрывают G20C. Локальная доводка этого варианта, новый перебор архитектур
и Performance в эту задачу не входят.
