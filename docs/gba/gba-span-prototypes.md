# G20A: общие спаны и текстуры потолочных уступов

27 октября 2026 года. Продолжение [первого прохода](gba-quality-options.md).
Работа относится к GBA; исходные EXE здесь не анализировались, SDL2 не менялась.
Все производные таблицы получены из уже восстановленного G03 package.

**Цель 15+ FPS пока не достигнута.** Точный вариант сохраняет исходную картинку
и даёт **7,17 / 7,47 / 8,53 FPS** в живых стартовых сценах LEVEL1–3 с PCM.
Отдельный вариант с более крупной фактурой пола/потолка — **11,95 / 9,95 / 14,93 FPS**.
14,93 не округляется до выполненной цели 15+. Это шесть коротких RU/EN fixtures,
а не минимум FPS на всём протяжении уровней.

## Сборки для сравнения

| Вариант | Файл в `artifacts/gba/` | Размер, B | Качество |
|---|---|---:|---|
| `sides_exact` | `DoK-port_optimized_prototype.gba` | 9 954 008 | Все 488 контрольных кадров побайтно совпали с базой |
| `world_water`, второй проход | `DoK-port_world_spans_prototype.gba` | 9 885 056 | Исходные текстуры, построчные perspective world UV |
| `sides_coarse` | `DoK-port_floor_4x2_prototype.gba` | 9 888 624 | Выборка пола/потолка 4×2 экранных пикселя; стены/двери/акторы 120×144, HUD 240×160 |

SHA-256 соответственно:

- `b36472a0701f19a41fc009e003f0f78a4493921e1cf19f2b51c266cdfeb42501`;
- `abf8ac697514074466e7ff74ff96cfa14b51c3f3364e54aa889eb646c3fab809`;
- `04dfe358654cc865a22ec9e674217eb4cde7583bde4f61d57d361fa1e61b5d11`.

База — `build/gba/select-menu-fix/dok-diagnostic.gba`, SHA
`7babd40f913eb12f77c93c9df917cea432b0cbe3bd83e11da45ba08dec917ea8`.
Обычная сборка с отключёнными экспериментальными flags воспроизводит её побайтно.
Сохранённый `DoK-port_pre_alpha.gba` не заменён. Более крупные
пиксели пола не назначены release default. Прежние промежуточные результаты
сохранены как `DoK-port_exact_planes_v1.gba`, `DoK-port_exact_ui_v2.gba` и
`DoK-port_floor_4x2_v2.gba`; BSP остаётся отдельным ROM первого прохода.

Текущие доказательства — `analysis/gba/g20a-span-prototypes.json` и
`analysis/gba/g20a-span-validation.json`. Второй проход отдельно сохранён в
`analysis/g20a-second-{prototypes,validation}.json`. ELF/map, частные снимки
исходников и native PNG/индексы кадров находятся в `research/experiments/g20a-sides/`,
`research/experiments/g20a-second/`. ROM/игровые данные не включаются в Git.

## Что изменилось

**Общие горизонтальные спаны.** Строка пересекает плоскость пола либо одну из
различных высот потолка. Координаты Q16 сразу выражены в клетках: старшая часть
выбирает FCMap, младшая задаёт UV исходной текстуры. C вычисляет границы спана
одной клетки, ARM выбирает texel и публикует бит покрытия. Отдельные ARM-пути
обрабатывают исходный water crop/wrap и направления сдвига. Перенос на ARM
проверен на 128 полных кадрах относительно C-пути и 3072 независимых векторах.

Это перспективное world mapping вместо исходной affine-интерполяции отдельных
плиток: меняются многие индексы пола и потолка, хотя сохраняются материалы,
освещение и координатная привязка. Нельзя объявлять этот вариант pixel-exact.
Полная сумма world Q16 могла переполниться у края карты; координаты теперь
собираются по слагаемым уже после деления на 480. Ограничения текущего пути —
шесть существующих views с нулевой высотой пола и неотрицательным inset.

**Фактура 4×2.** Один texel используется для двух соседних пар и двух строк;
покрытие проверяется независимо для каждой пары/строки. Геометрия стен,
дверей и акторов сохраняет прежнее разрешение. Горизонтальные границы разных
материалов/высот могут сместиться примерно на строку; фактура заметно крупнее.
Это отдельный эксперимент с качеством, а не прежняя отвергнутая проба 120×72
для всей сцены. Исходные текстуры используются и здесь, процедурной замены нет.

**HUD без изменения изображения.** Offline-атлас точно воспроизводит исходную
выборку фасок панели для 14 размеров и двух языков; ARM копирует строки словами.
Стоимость HUD в frozen сценах уменьшилась примерно с 204 до 29 тысяч циклов.
768 случайных копирований проверяют полные кадры и ABI. Меню, текст, пауза,
клавиши и localization проверены отдельно.

**Потолочные уступы.** Их вертикальные текстуры раньше шли через подготовку
освещённой колонки на C, поскольку у временного изображения не было resource id.
Теперь отдельная ROM-таблица связывает FC object с ресурсом `+0x5e`, а производные
колонки содержат все 16 строк LIGHT и флаги прозрачности после освещения.
В тяжёлых frozen видах LEVEL2 экономится около 240 тысяч циклов полного кадра.
`sides_exact` прошёл 488 кадров относительно базы; `sides_coarse` — 488 кадров
относительно прежнего coarse-ROM. Последнее сравнение доказывает точность
нового ускорения уступов, а не совпадение coarse-картинки с оригиналом.

Свет остаётся включённым со всеми 16 исходными градациями. Offline prelit
упрощает работу CPU; дополнительное сокращение до пяти полос не выбрано.
Готовый банк масштабов и его raster path отключены в этих прототипах.

## Измерения

mGBA libretro `0.11-212-7a12d6d`, replacement BIOS, штатные частоты/WAITCNT.
Live fixtures включают поворот, стрельбу, работу AI и actual PCM22050; capture
hold выключен. Представленные FPS учитывают VBlank. Значения для RU/EN
у двух текущих кандидатов совпадают.

| Вариант | LEVEL1 | LEVEL2 | LEVEL3 |
|---|---:|---:|---:|
| База | 4,98 | 5,43 | 6,34–6,37 |
| Первый `exact_planes` | 6,64 | 6,64 | 7,47 |
| Второй `exact_ui` | 6,96 | 7,47 | 8,53 |
| Текущий `sides_exact` | 7,17 | 7,47 | 8,53 |
| Второй `world_water` | 8,01 | 7,92 | 9,95 |
| Второй `world_coarse` | 11,03 | 9,95 | 14,93 |
| Текущий `sides_coarse` | 11,95 | 9,95 | 14,93 |

Полный CPU кадра в девяти frozen камерах, млн циклов; ожидание показа исключено.
Камеры унаследованы из прежнего G20 sweep, новый поиск экстремумов не выполнялся.

| Камера | База | Точный | Пол/потолок 4×2 |
|---|---:|---:|---:|
| L1-best | 1,952 | 1,479 | 1,042 |
| L1-median | 2,853 | 2,178 | 1,225 |
| L1-worst | 3,365 | 2,546 | 1,477 |
| L2-best | 1,925 | 1,407 | 1,025 |
| L2-median | 2,627 | 1,786 | 1,290 |
| L2-worst | 2,931 | 1,967 | 1,372 |
| L3-best | 1,540 | 1,146 | 0,932 |
| L3-median | 2,189 | 1,732 | 0,997 |
| L3-worst | 2,721 | 2,051 | 1,219 |

Медианное парное ускорение точного варианта на полной выборке 488 кадров —
**1,324×** относительно базы. Ускорение уступов у coarse зависит от вида:
медиана всей выборки практически не изменилась, выигрыш сосредоточен в видах
с такими поверхностями. Нельзя переносить частный выигрыш LEVEL2 на все кадры.

Профиль `analysis/gba/g20a-span-attribution.json` — отдельный запуск с timestamp
overhead; он не подменяет приведённые FPS. В L2-median вертикальные уступы
занимают около 277 тысяч циклов, обход и выборка горизонтальных спанов —
около 308 тысяч; геометрия в обычном frozen замере — 247 тысяч. Остаток бюджета
не сводится к одной палитровой операции.

Просмотрены PNG родных 240×160: exact/coarse L1-median и coarse L2-worst.
SHA изображений привязаны к отчёту. Пример одной и той же frozen камеры:

| Точный вариант | Фактура пола/потолка 4×2 |
|---|---|
| Точный L1 (`../../research/experiments/g20a-sides/sides_exact/L1-median.png`, исторический файл удалён при очистке) | Пол 4×2 L1 (`../../research/experiments/g20a-sides/sides_coarse/L1-median.png`, исторический файл удалён при очистке) |

## Проверки и ограничения

У обоих текущих ROM прошли renderer/reset/guards, sprites/вода, interactions,
rational time, все 28 AI variants, UI/шрифты/сообщения и flow шести миров с
14 исходными текстами. У coarse interactions явно пропускает только affine
визуальный oracle: двери, коллизии, sensors, conveyor, finish и reset проверяются.
У точного варианта этот oracle включён.

ARM-проверки: sampler4096, coverage2048, cell30720, animation48 resources/
308 frames; дополнительно exact divide65536, panel768, world3072/skip1024
по наличию соответствующего ядра. Проверены ABI/SP, границы и error outputs.
Integrated normal/frozen/pool-capacity/all-awake выполнен для RU/EN всех миров;
audio underruns0. Самостоятельный correlation suite всех 23 звуков не повторялся.

| Память | Точный | Пол/потолок 4×2 |
|---|---:|---:|
| EWRAM занято / свободно | 254 704 / 7440 B | 255 828 / 6316 B |
| IWRAM code/data | 23 064 B | 22 396 B |
| IWRAM свободно до отдельного резерва стеков/BIOS 8192 B | 1512 B | 2180 B |

Main stack high water2368/margin5568, IRQ80/margin80; guards и linker bounds
прошли. All-awake остаётся отдельным тяжёлым AI overload: худшие кадры точного
варианта585 798 080 cycles, coarse468 682 048. Simulation debt и callbacks
не отбрасываются, радиусы/лимиты/правила не менялись. Низкие stress FPS не
маскируются стартовыми результатами. 22 originals SHA совпали с сохранённым
inventory. Физическая GBA, оригинальный BIOS и целый уровень не проверены.

## Дополнительные пробы

- Exact reciprocal для делений world/raster почти не меняет FPS: общий
  `ports/gba/platform/memory.c` уже имеет exact reciprocal path для делителей меньше65536.
  Дополнительные специализированные варианты не выбраны.
- Принудительная ранняя очистка кадра не дала выигрыша. Отдельный ARM sampler
  прозрачных колонок немного ускоряет exact LEVEL1, но ухудшает world-вариант;
  он не выбран. Эти пробы имеют девять frozen камер/шесть live fixtures,
  а не полный функциональный suite; сводка в `negative_experiments` отчёта.
- Thumb renderer и размещение всей geometry в IWRAM были медленнее; отдельный
  opaque-wall ray pass тоже не принят. Консервативное backface-отсечение
  прошло 488 точных кадров, но дало около4% только в одном live fixture.
- BSP первого прохода сохраняется для сравнения; повторный geometry prepass
  оказался дорогим. Процедурная замена текстур отложена: измеренный выигрыш
  не оправдал потерю фактуры. Предложение пользователя не служит её приоритетом.

## Воспроизведение и продолжение

Нужны общие Zig/Python/mGBA/Unicorn из [building.md](../building.md), G03 assets
и сохранённые объекты `build/gba/select-menu-fix/`. Собирать в отдельной папке;
готовые проверенные каталоги не пересобирать во время исполнения тестов.

```powershell
C:/Source/wasdify/.venv/Scripts/python.exe ports/gba/scripts/prototype_gba_bsp.py --out research/experiments/g20a-sides-rebuild --variants baseline sides_exact sides_coarse
C:/Source/wasdify/.venv/Scripts/python.exe ports/gba/scripts/benchmark_gba_prototypes.py --build-root research/experiments/g20a-sides --variants baseline sides_exact sides_world sides_coarse --report research/experiments/g20a-sides/comparison.json
C:/Source/wasdify/.venv/Scripts/python.exe ports/gba/scripts/verify_performance_gba.py --build-dir research/experiments/g20a-sides/sides_exact --reference-build build/gba/select-menu-fix --reference-rom-sha256 7babd40f913eb12f77c93c9df917cea432b0cbe3bd83e11da45ba08dec917ea8 --step 5 --samples 2 --report research/experiments/g20a-sides/exact-oracle.json
C:/Source/wasdify/.venv/Scripts/python.exe ports/gba/scripts/verify_performance_gba.py --build-dir research/experiments/g20a-sides/sides_coarse --reference-build research/experiments/g20a-second/world_coarse --reference-rom-sha256 b4bd63f11f6d2f193eb4dcbd0886be5e4eabf52b2e5ca7136404198f02bcc0bd --step 5 --samples 2 --report research/experiments/g20a-sides/coarse-oracle.json
C:/Source/wasdify/.venv/Scripts/python.exe ports/gba/scripts/profile_gba_world_spans.py --build-dir research/experiments/g20a-sides/sides_coarse --report research/experiments/g20a-sides/coarse-attribution.json
C:/Source/wasdify/.venv/Scripts/python.exe ports/gba/scripts/report_gba_spans.py
```

`verify_{renderer,sprites,interactions,time,ai_combat,ui,flow}_gba.py`,
`verify_performance_arm.py` и `profile_game_gba.py` запускаются для обоих ROM;
профиль: `--samples 24 --stress-samples 6`. Точные пути всех отчётов указаны
в aggregate. Только coarse interactions получает `--skip-visual-oracle`.
Чистая повторная сборка baseline/exact/coarse действительно выполнена и
воспроизвела все три ROM побайтно. Aggregate проверяет результаты/SHA,
но не заменяет запуск регрессий. Старые `report_gba_prototypes.py` и
`report_gba_second.py` теперь копируют архивные v1/v2 имена.

G20/G20A остаются в работе; G20B/G21 ожидают. Следующая существенная задача —
сокращение общего traversal/geometry и стоимости покрытия/спанов. Прежде чем
вводить новое приближение, сравнить его на всех мирах с текущим профилем:
ещё одно локальное ускорение деления не устраняет разрыв до целевого FPS.
