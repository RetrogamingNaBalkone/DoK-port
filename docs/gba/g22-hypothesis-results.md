# G22: результаты проверки гипотез аудита

## Результат

1 ноября 2026 завершён ограниченный проход G22. Принят **H07**:
`g22_split_active` = `g20d_compact` + `HW_VISIBLE_SPLIT` +
`HW_PLANE_ACTIVE` + `HW_PLANE_ACTIVE_HOT`. Холодный legacy-путь перенесён
из IWRAM, активные рёбра потолка размещены в освободившейся памяти.
Геометрия, карты, AI, баланс, разрешение и точные диагностические счётчики
выбранной сборки сохранены. Нового Performance-продукта нет.

Выигрыш небольшой: медиана изменения CPU на 440 пространственных позах
**−0,180% RU / −0,173% EN**, лучшая поза **−2,05% / −2,06%**,
худшая **+0,219% / +0,192%**. Это не ускорение всей игры на 2%.
Минимум предъявления в этой выборке остался **9,95 FPS**. Выбор обоснован
сочетанием умеренного выигрыша, освобождения IWRAM и точной регрессии.

Пользовательская сборка: [DoK-port_G22.gba](../../artifacts/gba/DoK-port_G22.gba),
**26 649 252 байт**, SHA-256
`53fdf1b1b868effecfd68c8feb87ffb60bcc72190a4d01f38d8684a781860247`.
Она включает параллельную UI02 с QR-кодом. Измерительная версия до UI02:
26 648 748 байт, SHA
`3a8f3bd7e7ffeb77efff4c66a43fa78d698c220ea4cc3629b6f51269bfcae268`;
исторические результаты ниже относятся к этому pin, если не указано иначе.
Это приватный кандидат единственного порта; публикация и физическая приёмка
не выполнялись. Общий frontend-builder сохраняет исторический default для
старых воспроизводимых проверок; актуальный кандидат выбирается явно
`--variant g22_split_active`.

## База и методика H01

Контроль пересобран из сохранённых входов G20D и совпал с pin
`f40a13f56b70cfec5e673c7e0b80c659a48f35e30e7bad2e236ef7967a59a359`.
Шесть warm-медиан совпали с сохранённым G20D. Итоговый кандидат независимо
собран через полный frontend-builder и повторно связан из его base;
все три ROM совпали. Повторные heavy A/B дали те же медианы.

Использованы общий Zig 0.13.0, Python 3.14, mGBA libretro с replacement BIOS;
SHA core `5cac11c643d7f8dfa50c68855f2f2240ff7cc0b4cd0312dd9e17a639c85667e4`.
Образцы DOS EXE не анализировались и не изменялись. Здесь cell/depth —
координаты GBA-мира, адреса RAM/ROM — адресное пространство GBA, не NE/Ghidra.

Разделены четыре вида данных:

1. **Warm/frozen:** прежний пространственный sweep, шесть прогревов, PCM,
   фиксированные мир и камера. Для шести тяжёлых поз — 30 кадров;
   для 440 поз каждого языка — шесть. CPU без primitive instrumentation,
   presented FPS отдельно. Базовые stage timers остаются теми же, что в G20D.
2. **Cold geometry/frozen:** перед каждым завершённым draw сбрасывается
   `hw_static_active`; 12 образцов на позу. Это холодная геометрия при тёплых
   ресурсах/PCM, не cold boot. Hold нужен для точной инвалидации, поэтому
   presented FPS здесь намеренно `null`. Проверки последующего warm-прогона
   отделены от холодного CPU; их stage times нельзя приписывать cold draw.
3. **Moving/live:** по 600 эмулированных video frames на каждом уровне/языке,
   одинаковые события на кадрах 0/200/400: ходьба, поворот со стрельбой,
   ходьба с поворотом и стрельбой. PCM включён одинаково. Записаны median,
   p95, maximum CPU, FPS, состояния каждые 100 кадров и задержка наблюдения
   кнопок. Не используется остановка после одинакового числа предъявлений.
4. **Бои PERF01–03:** существующие LEVEL2 last-key и LEVEL3 key/witches
   fixtures с огнём, RU/EN, 120 предъявлений. Это нагрузочная регрессия,
   а не причинное сравнение идентичных живых состояний.

600 video frames задают одинаковое эмулированное время, но опубликованные
game ticks обновляются на границах draw и могут запаздывать. Даже при таком
расписании 5 из 36 координатных checkpoints H07 отличаются от контроля:
каденс опроса ввода и обновления мира различается. Это **не exact live trace**.
Поэтому прирост renderer выводится из frozen A/B; игровые правила проверяются
отдельной регрессией боя, часов и переходов. Задержка наблюдения трёх событий
H07 — 1–3 video frames; это не измерение физической кнопки до фотона.

Live FPS контроля → H07, LEVEL1/2/3:
RU **16,33/15,23/21,50 → 16,33/15,43/22,10**;
EN **16,33/15,33/21,60 → 16,33/15,43/21,80**.
Эти числа характеризуют маршруты, не общий процент ускорения.

## Решения по гипотезам

| ID | Проверенный объём и результат | Решение |
|---|---|---|
| H01 | Воспроизведение контроля, warm/cold separation, одинаковое video time и input schedule, явный учёт расхождения live states | Принять методику с ограничениями выше |
| H02 | Убраны attempted-sample increments в masked sampler; written count и coverage оставлены точными. Heavy median −0,077%, диапазон −0,164…−0,006%; FPS практически прежний | Отклонить этот вариант. Полное удаление written counting не реализовано и не опровергнуто |
| H03 | Только математически точная парная подготовка du/dv рёбер; row UV/span hashes сохранены. Heavy median +0,003%, диапазон −0,064…+0,057% | Отклонить этот вариант. Общий world-UV не реализован |
| H04 | Атрибуция открытых потолочных выборок по LIGHT и masked faces целиком за depth 3720 | Отложить полный вариант; продолжение G23 |
| H05 | 96 строк на текущей базе; исходный вариант превысил IWRAM на 224 B, поэтому сравнивался split и split+96 отдельно. Heavy median −13,066%, диапазон −14,946…−11,723% | Скорость подтверждена; влияние на игровой опыт ещё проверить в G25. Отличие пикселей само по себе не причина отказа |
| H06 | После атрибуции — enlarged column groups длиной ≥4 с source interval внутри одного нулевого aligned word; нынешний batch, без posts/binary searches/повторного coverage-прохода. Парный контроль split+H02 | Отклонить этот узкий вариант: median +0,621%, худший +1,263%; остальные sparse kernels не опровергнуты |
| H07 | Split отдельно и split+hot active edges; warm/cold, 880 spatial, moving, бои, Mode4/menu/fallback, полная регрессия | Принять split+active |
| H08 | Проверены доступная память и действующий geometry cache; нового raster-cache прототипа нет | Отложить до схемы размещения/композиции; G24 |
| H09 | Атрибуция потолка и сравнение с ценой/качеством 96 строк; собственного LOD-прототипа нет | Отложить до ограниченного визуального опыта; G25 |
| H10 | Нового способа убрать постоянную transport cost не получено; прежний feedback не повторялся | Условно отложить; модель расходов в G26 |
| H11 | Новая упаковка/admission не доказаны, native GPU-прототип не запускался | Условно отложить; offline replay в G27 |

«Отклонён вариант» не означает опровержение всех реализаций гипотезы.
«Отложено» означает незавершённое исследование с отдельной задачей и проверяемым
условием продолжения. По уточнению пользователя оставшаяся работа внесена
в G23–G27, а не объявлена исчерпанной завершением G22.

### Атрибуция H04/H06/H09

В шести позах `1:520:225`, `1:3640:225`, `1:2242:90`, `2:1592:135`,
`2:2871:225`, `3:1544:315` получено соответственно **0/32/0/4/2/0**
открытых потолочных samples LIGHT15. В первых двух — 4626/4675 и
5083/5533 открытых samples LIGHT0. Дальние masked faces целиком за 3720
дали 0/84/0/40/0/0 открытых samples, из них 0/64/0/40/0/0 ненулевых.
Это счёт операций, **не циклы**; непрозрачные колонки и пересекающие границу
faces не охвачены полной классификацией. Именно этот пробел адресует G23.

H06: в LEVEL2/2871 найдено 3974 samples длинных нулевых участков enlarged
колонок; в других пяти позах такие участки были minified либо отсутствовали.
Новый shortcut проверяет монотонный source interval в одном aligned word,
не меняет destination/coverage, но платит проверки на каждом подходящем
tile-row group. Парный опыт показал, что затраты не окупились.

H02/H06 меняют только экспериментальный контракт `transparent_pixels`:
masked transparency не считается. Low 16 bits written count остаётся точным,
`faces_drawn` сохраняется; feedback-варианты с изменённым контрактом не
собирались. Выбранная H07 сохраняет исходные диагностические счётчики.

96 строк дают 7288–22062 отличающихся RGB-пикселя в 18 контрольных кадрах;
это главным образом изменение выборки сцены, не UI. Просмотрены нативные
240×160 пары: текстура стены и потолка заметно меняется. Из этого не следует
доказанная потеря каждой цели, но оснований объявлять компромисс незаметным
нет. По уточнению пользователя **незначительное ухудшение допустимо, если
оно существенно не влияет на игровой опыт; полной незаметности не требуется**.
Пиксельное равенство — критерий exact-оптимизаций, а не quality-кандидатов.
Следовательно, 96 строк не отклонены по числу отличий: G25 должна проверить
читаемость целей/предметов, прицеливание, ориентацию и восприятие движения.
До этой проверки выбранная сборка остаётся 128-строчной.

## Проверки выбранного кандидата

- 880 spatial PPU: RGB, coverage и image/surface leases exact; 396 detail PPU
  exact; 48 cache/mutation cases exact, включая карты, двери и движение.
- Шесть ARM replay сохраняют coverage и все Q16 ceiling UV/spans.
- Frontend, one-frame taps/latency, UI, flow, credits, audio, time, ARM math,
  water, sprites и transitions прошли. Отдельно проверен Mode4 fallback.
- Combat, AI-combat, projectile cells, centered shots, midrange/close hits,
  ceiling sides, story и automap прошли. Известный BUG02 не закрывается.
- Сценарии PCM, artificial capacity/all-awake и PERF01–03 прошли без audio
  underruns; искусственные нагрузки не смешиваются с обычной игрой.
- Windows host suite прошла; Linux и ARM7TDMI Thumb cross-compilation прошли,
  cross-бинарники не исполнялись.

EWRAM **261476 B**, свободно **668 B**. IWRAM **22912 B**, свободно
**1664 B** до отдельного резерва **8192 B**; против контроля освобождено
**1660 B**. Main stack high-water **2584 B**, untouched **5352 B**;
IRQ **80 B**, untouched **80 B**. Это наблюдаемая выборка, не доказательство
всех возможных путей. ROM на 200 B меньше контроля.

Физическая GBA, original BIOS, полный проход игры и приёмка качества 96 строк
не выполнены. G21/BUG02 остаётся отдельной отложенной веткой. NDS/SDL2,
оригиналы и их handoff не менялись.

## Воспроизведение и доказательства

Порядок исходного измерительного прохода (до параллельной UI02):

```powershell
py -3.14 -X utf8 ports/gba/scripts/check_g22.py --stage build
py -3.14 -X utf8 ports/gba/scripts/check_g22.py --stage all
py -3.14 -X utf8 ports/gba/scripts/check_g22.py --stage spatial --variants control counts edges split rows96 active
py -3.14 -X utf8 ports/gba/scripts/check_g22.py --stage cold --variants control counts edges split rows96 active
py -3.14 -X utf8 ports/gba/scripts/check_g22.py --stage qualify
powershell -NoProfile -ExecutionPolicy Bypass -File ports/gba/scripts/build-native.ps1 -CrossChecks
py -3.14 -X utf8 ports/gba/scripts/report_g22.py
py -3.14 -X utf8 tools/reports/summarize_reports.py
```

`build` создаёт base штатным frontend-builder, затем отдельные семейства
таблиц 128/96 строк; `all` запускает heavy/PPU/live, а не весь workflow.
В текущем проходе первоначальные base objects взяты из G20D verified/base;
независимая полная сборка G22 verified и повторное связывание подтвердили pin.
Первые вызовы выполнялись напрямую; драйвер затем собран из тех же команд.
Отдельные profile-game/battles/latency для остальных кандидатов — дополнительные
нагрузочные проверки; обязательная полная qualify относится к выбранной H07.

### Интеграция UI02

После завершения измерений в рабочем дереве появилась отдельная выполненная
UI02. Её изменения сохранены; финальный ROM пересобран с текущими исходниками:

```powershell
py -3.14 -X utf8 ports/gba/scripts/build_gba_frontend.py --out build/gba/g22/integrated --variant g22_split_active --verify
py -3.14 -X utf8 ports/gba/scripts/prototype_gba_bsp.py --base build/gba/g22/integrated/base --out build/gba/g22/integrated-control --variants g20d_compact
py -3.14 -X utf8 ports/gba/scripts/benchmark_gba_hardware.py --build-dir build/gba/g22/integrated/hardware/g22_split_active --reference-build build/gba/g22/integrated-control/g20d_compact --languages 0 1 --details --report build/gba/g22/integrated-ppu.json
py -3.14 -X utf8 ports/gba/scripts/verify_gba_static_cache.py --build-dir build/gba/g22/integrated/hardware/g22_split_active --reference-build build/gba/g22/integrated-control/g20d_compact --report build/gba/g22/integrated-cache.json
py -3.14 -X utf8 ports/gba/scripts/verify_gba_hardware_transitions.py --build-dir build/gba/g22/integrated/hardware/g22_split_active --mode4-transitions --report build/gba/g22/integrated-mode4.json
```

Контроль интеграции совпал с UI02 pin
`e15e8cb2f9ea85cb15fd7509f26a9a3a2ddd873df8023e33e53d04081b2448d3`.
396 detail PPU/coverage, 48 cache mutations и Mode4 прошли; полный frontend
verify включает credits/QR, latency, flow, audio/time/ARM/water/sprites.
Повтор heavy A/B на этих двух ROM дал тот же диапазон −0,930…+0,082%
и median −0,174%. Память не изменилась. Предыдущие 880 spatial и игровая
регрессия не объявляются повторно выполненными на интеграционном SHA.

Для нового полного повторения опытов **на текущих исходниках с UI02**
использовать отдельную папку, не перезаписывая исторические доказательства:

```powershell
$g22Replay = 'build/gba/g22-ui02-replay'
$g22Control = 'e15e8cb2f9ea85cb15fd7509f26a9a3a2ddd873df8023e33e53d04081b2448d3'
py -3.14 -X utf8 ports/gba/scripts/check_g22.py --out $g22Replay --control-sha $g22Control --stage build
py -3.14 -X utf8 ports/gba/scripts/check_g22.py --out $g22Replay --control-sha $g22Control --stage all
py -3.14 -X utf8 ports/gba/scripts/check_g22.py --out $g22Replay --control-sha $g22Control --stage spatial --variants control counts edges split rows96 active
py -3.14 -X utf8 ports/gba/scripts/check_g22.py --out $g22Replay --control-sha $g22Control --stage cold --variants control counts edges split rows96 active
py -3.14 -X utf8 ports/gba/scripts/check_g22.py --out $g22Replay --control-sha $g22Control --stage qualify
```

Этот новый полный replay целиком не запускался: текущий проход использовал
описанные выше раздельные измерения и интеграционные проверки.
`report_g22.py` валидирует именно сохранённый проход `build/gba/g22/`,
включая интеграцию, и копирует проверенный итог в `artifacts/gba/`.

Локальные отчёты и журналы: `build/gba/g22/`. Долговременные копии итоговых
отчётов: `research/evidence/gba-g22/`; machine-итог:
`analysis/gba/g22-optimization-20261101.json` (Git-ignored).
[Компактная сводка](../../analysis/summaries/g22-optimization-20261101.json)
содержит решения, SHA, effective defines, paired CPU/FPS и ограничения;
полный отчёт остаётся необходимым для машинных проверок.

Следующий ID — **G23**. G24–G27 оформлены отдельными самодостаточными
паспортами для оставшихся исследований; это не зависимость NDS/SDL2.
