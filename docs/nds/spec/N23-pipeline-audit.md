# N23: крупные расходы CPU и границы архитектурного решения

30 октября 2026 года. Пользователь попросил продолжать N23, не начинать N24
и не переходить к точечным правкам до проверки общего подхода. Затем поставил
вопрос, почему простой старый мир с аппаратным renderer не держит 60 FPS.
**Аудит подтвердил существенную повторную CPU-работу. Предел DS не доказан;
новая архитектура ещё не принята.** Продуктовых оптимизаций в этой итерации нет.

## Измеренный бюджет

Контроль — immutable C12 RU, ROM
`f7049e2c7ec1900d9d644503ea359abbd3d7c622a4aae01de0e503aecaf36bdc`.
Независимый повтор `root13-c12-ordinary-review.json` подтвердил 42 archive pins
и все 8119 секундных окон прежнего LEVEL1. Маршрут завершился Game Over,
а не FINISH. Его 8149 штатных интервалов используются для оценки стоимости,
не для заявления о полном прохождении.

| Стадия | Среднее, мс | p95, мс |
|---|---:|---:|
| Симуляция | 1,431 | 3,618 |
| Построение геометрии сцены, включая двери | 4,987 | 5,982 |
| Sprites вместе с раскрытием карты | 5,305 | 6,580 |
| Из них раскрытие карты | 3,920 | 4,707 |
| Отправка геометрии, включая измеренные ожидания | 3,624 | 4,967 |
| Обновление карты, включая растр и публикацию | 2,358 | 7,676 |
| CPU целиком без явного VBlank wait | 19,256 | 26,960 |

Sprites и disclosure нельзя складывать: второе входит в первое. p95 разных
стадий не описывают один и тот же кадр. Сумма geometry+disclosure+submit
вычислена **для каждого кадра**, затем агрегирована: mean 12,531 мс,
p95 15,473, max 18,099. Бюджет одного VBlank — 16,715 мс при измеренной
развёртке 59,826098 Гц. Поэтому средний CPU уже превышает бюджет 60.

Явный финальный geometry fence — примерно 0,0012 мс. Это не полное время
GPU: синхронные записи FIFO могут ждать внутри стадии submit. Raster и
FIFO stalls отдельно этим счётчиком не измеряются. Проверка SDK показала,
что `glVertex3v16` и `glTexCoord2t16` — inline MMIO stores; объяснять расходы
накладными расходами вызова этих функций неправильно.

## Отдельный замер submit

Добавлен **только диагностический**, по умолчанию выключенный
`N23_PIPELINE_PROFILE`. Он разделяет camera plan, полную валидацию,
начальный fence, emission и завершение. Все прежние проверки и команды
остаются на месте. Кольцо публикуется с seqlock; production C12 не менялась.

Сборка `pipeline14-scn-ru` использует C12 features плюс
`scn,pipeline,geomfine`. ROM
`5eaf7b114246701d6586f4d6f59c1f10cfc4422ce9f44e560c7d959fa5d54aee`,
ELF `fc87e1d076473d98cb33c6fdc5f9b3333307c6cefeeb203c88de693df86194ef`.
На сохранённом `build/nds/n21p/scene-heavy-cost.dat` получены все 154 записи,
без пропусков. Runtime/audio/native map/HUD pixel errors отсутствуют.
Неполный HUD/touch-набор ожидаем для короткого cost packet; приёмку полной
игры он не заменяет. Fine timers добавляют расходы, его FPS не принимается.

| Case | Camera plan | Проверка данных | Emission | Начальный/final fence |
|---|---:|---:|---:|---:|
| 14 | 0,040 | 2,498 | 3,147 | 0,0012 / 0,0013 |
| 15 | 0,040 | 2,368 | 2,689 | 0,0012 / 0,0013 |
| 39 | 0,041 | 2,489 | 3,052 | 0,0012 / 0,0013 |

Все значения — средние мс. Emission включает подготовку значений, SDK state
и MMIO/FIFO stores; его нельзя целиком назвать ни вычислениями, ни ожиданием GPU.
Geometry fine: build 2,55–2,57 мс, cull 1,12–1,41, merge 0,45–0,50,
doors 0,29–0,95. Дополнительные таймеры и оставшиеся части geometry учитываются
отдельно, эти числа не вычитаются из product FPS как обещанный выигрыш.

## Что видно в устройстве конвейера

- `render/scene.c:dok_render_scene_prepare` для каждого кадра строит исходные
  quads из текущих TMap/FCMap и соседей, отсекает, переводит в локальные координаты
  и объединяет. Дверной проход затем обрабатывает исходные door records.
- `ui/map_discovery.c:dok_map_discovery_update` отдельно строит native shapes,
  проецирует, сортирует и выполняет исходное раскрытие 320 колонок. Это имеет
  самостоятельную семантику: список видимых GPU-полигонов его не заменяет.
- `render_backend.c:dok_nds_render_submit` проверяет матрицы/vertices/UV/materials,
  затем повторно проходит готовый frame для отправки. Публичный API и capacity
  guards полезны; удаление проверок не является принятым решением.
- Hero callbacks отдельно выполняют source getter/wake/gather ради игровой
  семантики. Их нельзя просто привязать к частоте вывода или убрать как дубликат.

Вывод: крупные кандидаты — представление и срок жизни статических данных,
совместное использование подходящей исходной топологии и способ передачи
проверенных команд. Перестановка нескольких arithmetic operations этого
вопроса не решает. Дешёвые камеры с почти 60 FPS и полный SCN budget30
подтверждают работоспособность аппаратного renderer, но не оптимальность
подготовки тяжёлой сцены.

## Предварительная оценка хранения

Текущий C builder запущен на сохранённых N07 initial LEVEL/bindings; после
вычитания world x/z сравнивались точные material/flags/glyph/UV/height/order.
Это **только initial-world census**, не готовый runtime cache.

| LEVEL | Исходных faces | Уникальных cell templates | Faces в templates | Quads + u16 cell index, байт |
|---|---:|---:|---:|---:|
| 1 | 8208 | 115 | 313 | 36 988 |
| 2 | 8877 | 127 | 342 | 39 656 |
| 3 | 6286 | 75 | 184 | 25 120 |

Сохранение полного `dok_geometry_cell` для каждой клетки требует 4 538 368
байт, одних active quads — 578 312–816 684. Наивный полный cache не подходит.
Templates допускают гораздо меньший объём, но приведённые числа не включают
metadata/alignment/dynamic capacity и **не означают, что такая RAM свободна**.
`scene_high_water` сейчас отражает `sizeof(scene)`, не сумму scene+HUD+map tail.
Проверять фактические полные области, SDK minimum 196608 и reserve 655360.

## Следующее архитектурное сравнение внутри N23

### Проверка на оверинжиниринг

Дополнительное указание пользователя: оценивать сложность относительно
конкретного NDS-порта этой DOS-игры, а не универсального движка/конструктора.
Это критерий выбора решения наряду с временем, RAM и точностью поведения.

| Решение | Оценка для этого порта |
|---|---|
| Hardware 3D, GPU black fog, явные VBlank/DMA окна | Сохранять: аппаратные ограничения и уже измеренная польза. |
| Повторная полная проверка внутренних vertices/UV/materials при каждом submit | Подозрение на избыточный контракт: 2,37–2,50 мс. Проверять загрузку и изменяемые данные там, где они создаются; не заменять это системой универсальных sealed handles или графом зависимостей. Сначала доказать узкие предпосылки текущих трёх уровней. |
| Полный runtime cell builder для неизменных стен/пола/потолка | Сильный кандидат на упрощение: подготовка при загрузке, без универсального dynamic mesh API. |
| Общий cache с hashes/LRU/epochs/journal ради статической геометрии | Пока не нужен. Сначала проверены фактические writers; базовая геометрия сохраняется при pickup/corpse substitutions. |
| Native disclosure и getter/AI cadence | Сохранять поведение, но не обязательно прежний способ промежуточного представления. Отдельная семантика оправдана проверками оригинала, общий «visibility engine» не требуется. |
| Raw/VRAM working set | Ограничение памяти реально; сам bounded cache обоснован. Каждый дополнительный уровень cache/pool должен окупать стоимость и сложность, а не вводиться по привычке. |
| Десятки compile-time experiments | Не дают runtime overhead, когда выключены, но создают стоимость сопровождения. После выбора итогового пути оставить нужные production/diagnostic режимы; исторические эксперименты и доказательства сохранять отдельно, не удалять в этой проверке. |

При просмотре production writers обнаружено более простое свойство:
`items.c` удаляет class3/10 в space/class0; `ai.c:dying` пишет corpse/class9
в TMap только поверх class0. FCMap и structural classes2/4/7 эти операции
не меняют. `interactions.c` меняет door phase/flags, а не structural TMap.
World geometry bindings не подменяются sprite animation bindings.

Текущий C builder проверен на сохранённых N07 картах/bindings всех трёх
уровней: 536272 одиночных замен, 2693648 full-cell byte comparisons,
а также совместные изменения всех подходящих клеток каждого мира.
`root15-topology-invariant.json` — PASS. Сравнивались затронутая клетка и её
соседи; перебраны все enemy glyphs соответствующего LEVEL. Host-мир менялся
только внутри проверки, состояние эмулятора не записывалось.

Это подтверждает invariant базового конструктора для данного набора inputs
и перечисленных мутаций. Не проверены новый runtime representation,
current C12 material layout, native disclosure flags/sprites, GPU pixels и
ARM-выигрыш. Но **предварительно проектировать общий invalidation journal
не требуется**: первый вариант должен опираться на статические данные уровня
и отдельные уже существующие dynamic doors/sprites/water.

1. На одинаковом bounded ARM9 packet сравнить текущий конвейер с компактными
   заранее построенными данными исходной статической геометрии. Templates —
   только вариант экономии RAM, не новый общий cache API. Использовать
   проверенный выше invariant, дополнить проверкой current C12 inputs и
   отдельными doors/sprites/water. Полный scan/hash мира каждый кадр и
   прежний проигравший point/clip cache не возвращать без новой пары.
2. Отдельно сравнить текущую emission с подготовкой packed GPU commands,
   используя доступный transient workspace. Сохранить порядок, quantization,
   texture/palette state, полную validation до FIFO writes и DMA leases.
   Сначала установить долю CPU packing и FIFO ожидания; выигрыш DMA не обещан.
3. Общая topology для renderer/native disclosure допустима только после
   byte-exact host/original checks. Native bounds, порядок/ties, source quirks,
   visited и getter/AI cadence остаются прежними. Совпадение GPU pixels само
   по себе недостаточно.
4. Принять крупное решение по парным ARM timings, RAM и семантике. Только
   затем продолжать точечные оптимизации. После выбора повторить ordinary
   тяжёлые участки, включая естественные ≥61 AI, и накопленные RU/EN gates.

Ни template cache, ни command stream здесь не одобрены как product solution.
Следующий ID — **N23**. N24 и физические DS/listening не начаты.

## Остальные проверки этого продолжения

C12 RU/EN SCN: по63 cases/12880 presentations с полным CPU profile,
runtime/audio/HUD/map PASS. Normal 6637 intervals: RU 51,346931 FPS,
EN 51,247524; каждый interval ≤2 VB, minimum 29,913049. Goal60 false.
EN live/LCD LEVEL1–3 PASS: 1836 intervals, 1813×1 VB+23×2 VB,
42 touch responses/max6 VB, loading отдельно. C12 compiled inputs в новых
production/SCN builds совпадают с frozen C12 RU.

Три ordinary LEVEL3 samples `tour13/14/15` не принимаются как прохождение:
natural52/Game Over, natural19/blocked sample и natural15/Game Over.
У каждого свой sample FPS budget PASS; **natural61 остаётся открытым**.
Новых таких маршрутов после вопроса пользователя не запускали.

Доказательства: `research/evidence/nds/n23/root13-pipeline-audit.json`,
`root14-topology-census.json`, frozen candidates и
`review13/manifest.json` (1745 files, SHA
`895ccb99e14d21fe0d12a4c43e27ea576d6efe6aa8b294b1683577a1e20037c0`).
Отчёты и исходные snapshots сохранены; disks/core logs не дублированы.

Общий Python3.14.6, Zig0.13.0, BlocksDS1.24.0, GCC16.2.0;
DS-mode melonDS DS libretro1.4.0 с прежним pinned core SHA. Точные features,
SDK commands и source hashes есть в manifests/build logs соответствующих
candidates. Воспроизводимые команды выполненных замеров:

```powershell
py -3.14 -B -X utf8 ports/nds/tools/performance/run_logged.py build/nds/n23/pipeline14-scn-ru/pipeline-cost.log ports/nds/tools/performance/run_pipeline_cost.py --target build/nds/n23/pipeline14-scn-ru --language 1 --packet build/nds/n21p/scene-heavy-cost.dat
py -3.14 -B -X utf8 ports/nds/tools/performance/audit_pipeline.py --profile research/evidence/nds/n23/route-profiles/root12-level1-exit-negative01/run/runtime-profile.json --split-target build/nds/n23/pipeline14-scn-ru --output research/evidence/nds/n23/root13-pipeline-audit.json
py -3.14 -B -X utf8 ports/nds/tools/performance/census_topology.py --output research/evidence/nds/n23/root14-topology-census.json
```

Для повтора выбирать новый immutable label/output/run-id, не перезаписывать эти
результаты. Начальная `pipeline13` предшествует seqlock-публикации, не исполнялась
и не используется как измерение. Полное программное принятие N23 ещё открыто.
