# N23: упрощение конвейера для трёх фиксированных миров

30 октября 2026 года. Продолжение [аудита](N23-pipeline-audit.md).
N23 ещё не принята; N24 не начата. Все времена ниже — DS-mode melonDS DS,
не измерения физической DS. Контроль C12 сохранён неизменным.

## Выбранное представление

Архитектурная оценка для этого порта: аппаратный 3D renderer сохранён;
повторное конструирование неизменных граней и поиск strip reuse оказались
лишними. Обычная таблица одного уровня решает задачу дешевле универсального
кеша. Полную CPU visibility/native disclosure пока сохраняем: она участвует
в gameplay getter/visited semantics, а опыт GPU-only frustum cull ниже
не дал выигрыша даже при одинаковой картинке. «Больше работы на GPU» само
по себе не критерий: оценивается стоимость всего кадра.

`render/static_geometry.c` строит данные одного загруженного уровня 64×64,
cell side480. У каждой клетки byte-индекс точного шаблона; шаблон содержит
48-byte quad records с local XYZ, целыми UV, material/flags/kind/glyph.
Дедупликация — простой полный `memcmp` при загрузке. Нет LRU, hash table,
версий, refill, журнала invalidation или общего API редактируемого мира.

Обычные pickup/corpse writers сохраняют базовую геометрию; doors, sprites
и water остаются в своих прежних динамических проходах. Таблица заимствует
assets slab перед compressed frames/raw cache: 19648/21056/13472 bytes
после alignment для LEVEL1/2/3. Slab и reserve655360 не увеличены и не урезаны.
Pointers сбрасываются при world stop и до следующей загрузки.

Renderer получает прежние полные quads. Native disclosure читает из той же
таблицы прежние 20-byte X/Z shape records; его projection, sorting, ties,
320 columns и visited semantics не меняются. Это убирает две повторные
подготовки одной статической информации без нового универсального слоя.

Ещё два независимых упрощения:

- `simplequads`: обычные GL_QUADS вместо поиска общих рёбер/полос. Удалён
  4096-byte `strip_draw`; порядок четырёх вершин сохранён. Консервативный
  vertex bound и аппаратный overflow admission остаются. Число переданных
  вершин может вырасти, поэтому полный SCN gate обязателен.
- `relmat`: первая клетка получает полную clip matrix, следующие — целый
  translate относительно предыдущего origin. Все абсолютные matrices и
  входы по-прежнему проверяются до первой записи FIFO. При выходе translate
  command за int32 используется прежняя полная matrix. Нет matrix cache.

Флаги экспериментальных пар по умолчанию0. C22 собрана с C12 feature selection
плюс `staticgeom,simplequads,relmat`; обычные RU/EN ROM не содержат fine/pipeline
диагностику. До приёмки интеграционных маршрутов это кандидат.

## Проверки смысла и памяти

`static17-host.json`: 536272 pickup/corpse substitutions; geometry comparisons
879548/717473/1108918 по трём мирам. Полные native disclosure records,
workspace и visited совпали на512 poses каждого мира с историческими
initial/pre-compact/pre-math реализациями; runtime flags/sprites менялись.
Проверены capacity failure/reset fallback и poison неарендованного suffix.
Это differential proof относительно принятых конструкторов, не новое
независимое доказательство DOS-эквивалентности.

`relative20-math.json`:681569 exact matrices,681568 integer translations,
57760 rejected ranges и отдельный full-load fallback. Diagnostic ARM/GPU run
`relative20-audit-scn-ru`:18186 clip matrix reads,18032 translations,
154 first loads,0 mismatches. Это проверка эмулятора, не физической GE.

`simplification22-review.json` закрепляет четыре ARM-пары: одинаковые source
manifests и generated inputs,154 matching camera/cell/quad rows на пару,
52 byte-identical PNG pairs суммарно. Fine timers добавляют overhead:
их CPU times можно сравнивать внутри пары, FPS по ним не принимается.

| Изменение / case14 | CPU до, мс | CPU после, мс |
| --- | ---: | ---: |
| Только renderer static geometry | 25,227 | 23,888 |
| Shared geometry + native disclosure | 25,227 | 22,880 |
| GL_QUADS вместо strip search | 22,880 | 22,378 |
| Relative matrices | 22,378 | 21,541 |

Пары `static16`, `static17`, `static18`, `relative20` отдельные; складывать
времена из разных instrumentation modes с обычным C12 профилем нельзя.
При GL_QUADS startup уменьшился512000→507904, SDK heap вырос208896→212992;
reserve655360 сохранён, sticky/error flags0.

## Обычный C22

Полный SCN RU/EN:63 cases и12880 profile frames на язык; renderer, audio,
HUD и map PASS. На одинаковом normal subset6637 intervals:

| Банк | C12 среднее FPS | C22 среднее FPS | C22 >1 VB | C22 >2 VB |
| --- | ---: | ---: | ---: | ---: |
| RU | 51,346931 | 56,003641 | 6,8254% | 0% |
| EN | 51,247524 | 56,027348 | 6,7802% | 0% |

Минимум любого проверенного секундного окна29,913049144; бюджет30 этого
поднабора PASS, цель60 FAIL. Synthetic all-awake и frame-state stress
учитываются отдельно; они не заменяют natural61.

RU/EN ordinary all3 live/LCD PASS: по1839 normal intervals, каждый1VB,
59,826098 FPS. Touch42 responses/max4VB на язык; это короткие live участки,
не замена тяжёлых маршрутов. LEVEL1 RU `c22-saved01` завершён штатным
FINISH→EINTRO→LEVELS.14862 normal intervals:47,069109 FPS, min29,913049,
все≤2VB. CPU mean14,730/p9522,758/max31,770 мс. Native discovery включён
в sprites; нельзя складывать эти два stage как независимые.

В этом полном LEVEL1 map stage p958,524/max10,188 мс; raster p955,462,
DMA p951,296. Полный software raster запускается при dirty update12Hz,
follow/visited/actor changes могут требовать его заново. Это оставшийся
периодический CPU пик; перенос верхнего 3D на GPU его не устраняет.
Причина всех пропусков60 этим одним наблюдением ещё не доказана.

Из4028 двухкадровых интервалов этого L1 2094 совпадают с redraw карты,
1934 идут без redraw. CPU mean для них22,076 и17,203 мс соответственно.
После арифметического вычитания map stage CPU p9517,080 мс,946 кадров
всё равно превышают один VBlank. Это корреляция/расчёт, не измеренная сборка
без карты: одной map-оптимизацией полную60-цель считать достижимой нельзя.

Natural AI tour22 достиг52; exact fresh-boot replay до VB9411 совпал во всех
scalar rows, native PAUSE/flow, pose и map bytes, затем ordinary continuation
достиг56. Никакие HP, inventory, RAM, AI или часы не записывались. Эти попытки
не завершили LEVEL3. ≥61 и полные текущие LEVEL2/3 остаются открытыми.
Доводка host player policy сама по себе порт не ускоряет и его готовность
не доказывает. Не подменять Game Over/abort прохождением.

## Исправление наблюдателя HUD

C19 SCN RU/EN остановились на source fade case36: INDEX8 oracle прошёл,
LCD RGB сравнивался с меняющейся palette при ошибочно объявленной stable3
числовой строке. В `joint_hud_oracle.py` stability key теперь включает точные
512 palette bytes, прочитанные для того же RGB expectation. Pixel assertions
не ослаблены. Четыре проверки: старый false failure воспроизводится,
fade→steady проходит, stable wrong RGB и wrong INDEX8 отклоняются.
C22 полная матрица с этим наблюдателем прошла, включая fade35–37.
Отрицательные C19 отчёты и прежний observer сохранены.

## Отдельно отклонено: GPU-only frustum cull

После выбора C22 проверено, нужен ли CPU frustum cull стен/пола вообще,
если GPU всё равно клипует грани. `cull29-cpu-scn-ru` и `cull29-gpu-scn-ru`
имеют одинаковые compiled sources/generated inputs, отличаются только
экспериментальным флагом. Fine diagnostic packet154 frames, audit0.
Все13 PNG pairs совпали, ошибок/overflow нет, но quad submissions выросли
42810→61410 (+43,45%). Case14 CPU21,541→25,000 мс,
geometry7,170→7,872, submit4,399→6,499; case15 CPU22,001→25,147,
case39 CPU19,265→21,230. CPU tests оказались дешевле дополнительной
подготовки/effects/передачи граней. Это не доказательство physical GPU limit.

CPU frustum cull сохранён. GPU-only prototype и build flag удалены из
текущего рабочего кода; frozen versions остались в candidates/cull29-*.
Предыдущая пара cull28 с audit1 тоже сохранена, но для изоляции стоимости
используется29. `review_gpu_cull.py` проверяет paired manifests, poses,
PNG и отрицательный выигрыш; `cull29-review.json` — PASS/rejected.

## Текущий архив

`review26/manifest.json`:1773 frozen files, SHA
`b6fa75deac09cc0503222016575351f718749de7a3304f17ebd1f827b3f2546c`.
`c22-review27.json` перепроверил их и выбранные host/ARM proofs,
LEVEL1 complete и exact-prefix negative archive; n23_accepted=false.
LEVEL1 archive — `route-profiles/c22-level1-complete01/manifest.json`,
SHA`f4753f5d5fdd420a1600f379487e4c5b7950bbb84c94a46787486e0160eb9f7a`.
RU live49 files — `review28-ru-live/`; cull28/29 negative104 files —
`review30-cull/`. Имена review26/28/30 — номера экспериментов N23, не ID задач.

## Воспроизводимые команды

```powershell
py -3.14 -B -X utf8 ports/nds/tools/performance/check_static_geometry.py --label static17
py -3.14 -B -X utf8 ports/nds/tools/performance/check_relative_matrices.py
py -3.14 -B -X utf8 ports/nds/tools/performance/review_simplification.py --output research/evidence/nds/n23/simplification22-review.json
py -3.14 -B -X utf8 ports/nds/tools/performance/test_hud_palette_observer.py
py -3.14 -B -X utf8 ports/nds/tools/performance/run_logged.py build/nds/n23/candidate22-scn-ru/matrix.log ports/nds/tools/run_joint_emulator.py --target build/nds/n23/candidate22-scn-ru --language 1 --packet build/nds/n23/scene.dat
py -3.14 -B -X utf8 ports/nds/tools/performance/run_logged.py build/nds/n23/candidate22-scn-en/matrix.log ports/nds/tools/run_joint_emulator.py --target build/nds/n23/candidate22-scn-en --language 2 --packet build/nds/n23/scene.dat
py -3.14 -B -X utf8 ports/nds/tools/performance/run_logged.py build/nds/n23/candidate22-en/live.log ports/nds/tools/run_joint_frontend_emulator.py --target build/nds/n23/candidate22-en --language 2 --mode live --levels 1 2 3 --steady-presentations 180
py -3.14 -B -X utf8 ports/nds/tools/performance/run_logged.py build/nds/n23/candidate22-ru/full-l1.log ports/nds/tools/performance/run_saved_routes.py --record research/evidence/nds/n22/route-profiles/level1-ru-current02-complete01/run/report.json --target build/nds/n23/candidate22-ru --language 1 --level 1 --run-id c22-saved01
```

Для повторов создавать новые immutable labels/run IDs/output paths: отчёты
выше не перезаписывать. Полные команды SDK, source snapshots, feature flags,
ROM/ELF SHA — в `research/evidence/nds/n23/candidates/<label>/build-manifest.json`.
Физическая DS/DS Lite, прослушивание и stack high-water остаются N24.
