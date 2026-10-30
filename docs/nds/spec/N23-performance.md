# N23: оптимизация полной игры

30 октября 2026 года. **В работе; бюджет полной игры ещё не принят.**
Продолжение по новому поручению: [аудит архитектуры и оверинжиниринга](N23-pipeline-audit.md).
C12 RU/EN SCN и EN live/LCD завершены, natural61 ещё открыт.
Перед следующей точечной оптимизацией выбрать простой путь подготовки
статических данных по парным ARM замерам. N24 не начинать.
Текущее состояние и следующий шаг — [N23-next-chat](N23-next-chat.md).
Последняя immutable production control — `candidate12-ru`: ROM
`f7049e2c7ec1900d9d644503ea359abbd3d7c622a4aae01de0e503aecaf36bdc`,
ELF `91ce5a3fc646651bb2802a79784779bb4339049ff2ddd4c30c2728cc9f010dc3`.
C12 L2 prefix220: 5 115 intervals,36,248578 FPS/min29,913049/все≤2VB.
C12 L1 partial allkeys7: 8 149 intervals,33,94297 FPS/min29,913049/все≤2VB,
но Game Over без FINISH. Эти два FPS PASS не являются приёмкой N23.
C12 RU all3 live/LCD PASS; matching EN/SCN ещё нет, natural61 открыт.
Физическая DS/DS Lite, прослушивание и аппаратный high-water стека остаются
N24. Все приведённые ниже времена получены в DS-mode melonDS DS libretro
1.4.0, core SHA-256
`c7e6b58c667075a2a9868f2c3ba7a98e24200410a6da3051a7c555f1f0eef8ef`.

## Контроль и границы

Вход — принятая N22 candidate-partial02 и её
[обычные маршруты](N22-route-performance.md). Их ROM/ELF, архивы и профили
не перезаписываются. N23 использует `build/nds/n23/` и
`research/evidence/nds/n23/`. Все новые labels неизменяемы после появления
`build-manifest.json`; manifest и точные compiled inputs сразу копируются
в `research/evidence/nds/n23/candidates/<label>/`.

Критерий 30: ≥99% обычных интервалов ≤2 VBlank **и каждое** корректное
окно ≥1 с имеет cadence ≥29,913049144 FPS. Цель 60 отдельная: ≥99% ≤1 VB.
Среднее выше 30 при провале секундного окна не считается PASS.
Обычные маршруты управляются joypad/pointer; RAM, HP, инвентарь, AI и часы
не записываются. Загрузки, реальные UI-gaps, диагностические fixtures и
искусственное all-awake учитываются отдельно.

`N23_FINE_PROFILE=1` публикует дополнительный read-only ring: compaction,
decode, gather и каждую длительность callback. Этот вариант не принимается
по итоговому FPS. Обычный N21P stage/history ABI сохранён. Fixed-video replay
старого ввода на новом ROM меняет sampling и может изменить мир; он не
доказывает равенство poses, прохождение или парные картинки.

## Проверенные локальные изменения

1. `frame_store.c` выбирает прежние LRU-жертвы, включая равные ages и
   swap-removal, затем уплотняет payload пакетно. При NO_SPACE сохраняются
   прежние частичные удаления. Независимый oracle сравнивает исходный C N22
   с новым C: status, порядок/offset/pins/age, counters и все used bytes.
2. `dok_frame_store_require_many` сохраняет последовательность requests,
   включая дубликаты. Warm-hit prefix обходится без shadow workspace.
   При pressure оставшаяся metadata моделируется отдельно; форма packed RLE
   проверяется до первого payload write. Любая неопределённость возвращает
   исполнение к обычной последовательности с исходным live cache после
   уже завершённого prefix. Provenance переживает swap: вытеснённый исходный
   ID, позднее запрошенный снова, распаковывается заново.
   Workspace заимствует существующий scratch после geometry/discovery;
   новых постоянных caches или heap allocations нет.
3. Для cell side 480 signed quotient/remainder получают точную константную
   ветвь, остальные размеры используют прежний IDIV. Collision и AI собраны
   ARM: actual objdump подтвердил SMULL; общий fallback сохранён.
4. `clear_at` один раз вычисляет два X и два Z индекса, сохраняя WORD wrap,
   clamp и порядок MMap → TMap. Не меняются occupancy, flags или resolver.
5. Карта имеет отдельную ветвь ширины один pixel: вертикальные stores вместо
   одного tiny memset на строку. Остальные прямоугольники прежние.
   Отклонённый в N21P общий tiny-row unroll не возвращён.
6. Rational clock conversion имеет точный 32-bit fast path. При
   `elapsed,num,den≤UINT32_MAX` и `fraction<den` произведение с остатком не
   переполняет uint64. Малые значения обходятся сравнением либо uint32
   divmod, остальные сохраняют точный uint64 путь. Набор samples, remainder,
   pause, uint32 world ticks и scheduler deadlines не меняются.
7. Frustum culling распределяет прежние пять int64 plane equations в
   coefficients камеры один раз на prepare. Все прежние common-plane
   решения и полные scene bytes совпали на 13 816 poses; отдельный scalar
   oracle проверил 564 000 quads и крайние int32 координаты.
8. Первый проход карты объединяет последовательные одноцветные FCMap cells
   в один inclusive rectangle. Второй проход и порядок разных цветов прежние.
   Парный ARM9 набор 40 ракурсов LEVEL2: 260,771 →215,386 мс суммарно.
9. Door builder использует существующий private in-place geometry path,
   сохраняя координаты, crop и mirror. Host: 29 342 scenes, 408 focused
   cases и 676 error cases совпали. Тяжёлый door stage: 1,4267 →0,9340 мс;
   sparse case остаётся 0,28975 мс. Ранний вариант с eager field table
   отклонён из-за замедления sparse scene.
10. RLE unpack для длины ≤16 использует ограниченные byte stores вместо
    libc call. Существующие проверки malformed stream сохраняются.
    Парный ARM9 набор 336 настоящих resident streams: 232,112 →178,213 мс,
    худший тот же frame 5,19634 →3,51874 мс. Все pixel hashes совпали;
    независимые byte-exact и protected-page checks приведены ниже.
11. `N23_EFFECT_POOL_WORKSET` ограничивает generated pool суммой всех
    различных material slots текущего GPU-fog кадра. Последний class12 writer,
    приоритет water flags, odd phase keys и non-96 payloads учитываются.
    Освободившийся хвост остаётся в том же assets slab у raw cache.
    Для LEVEL1/3 bound равен нулю (pool minimum 32 КиБ), для LEVEL2 четыре
    tiled материала требуют прежние 128 КиБ. Это не уменьшение memory caps
    и не изменение exact-alpha fixtures.
12. Disclosure сохраняет исходный Shell gap/tie order, но использует
    insertion shifts вместо повторных swaps. Проверка открытых колонок
    выполняется 10-word bitset; перед возвратом восстанавливаются все
    прежние 320 byte columns. Raw records, sort keys, flags, stats, visited
    и полный workspace совпали с N22 в независимом oracle.
13. Неизменная полоса map buttons сохраняется в прежнем RAM mirror и SUB
    VRAM. Новый full repaint обязателен при first draw, frontend invalidate,
    capture/language/font/pixels changes. Остальные redraws передают первые
    40 960 bytes вместо 49 152. Полные pixels одинаковы, map deadline и
    каждый touch sample сохранены. Font cache сравнивает точные bytes
    используемых glyphs, без hash collision assumptions.
14. Visible weapon overlay сохраняет прежний INDEX8 VRAM image при точном
    совпадении view, immutable frame ID, pitch/bytes и world serial. Любая
    загрузка мира или возврат frontend сбрасывает validity; HUD пишет только
    следующие 38 строк. Первая ветвь сохраняет raw admission, вторая отдельно
    исключает его на подтверждённом retained hit. Cache LRU requests/counters
    при второй ветви намеренно меняются; NDS cache API сохраняет свои правила.
    Scratch/raw pointers между кадрами не заимствуются.
15. Неиспользуемый хвост 64-КиБ loading slot исходного LEVEL освобождён
    внутри assets slab. Все фактические CP866 bytes и token spans остаются
    pinned. Новый offset — aligned source_bytes; capacity guard загрузки
    64 КиБ прежний. LEVEL1/2/3 освобождают 41 152/41 248/37 536 bytes.
    Сравнены все source/models/material metadata/pixels и EN append leases:
    46 494 checks/3 703 497 bytes exact. Ни один slab cap не понижен.

Эксперимент `N23_CACHE_REPLACE`: если один старый LRU victim ровно того же
размера, новый декодированный frame занимает его физический slot. Сначала
проверяются полный RLE stream и отсутствие alias с raw pool. Записи сохраняют
прежний swap-remove/append order, ages/pins/counters; все retained pixels
неизменны. Физические offsets намеренно переставляются. Это разрешено
cache contract: borrowers получают pointers после всех require. Pool по-прежнему
непрерывно покрывает [0,used), без holes или overlaps. Malformed/NO_SPACE
сохраняют прежний путь и failure-prefix mutations. Корректность последующих
обычных multi-victim compactions проверяется отдельно.

Управляющие defines каждого варианта записаны в build manifest. Возможный
per-submit cache 64 проверенных clip matrices (`N23_CLIP_CACHE`) остаётся
отдельным экспериментом; его выигрыш и включение в продукт не приняты.

## Измерения и доказательства

Fine ordinary baseline: 3 190 frames/13 690 callbacks, пропусков callback нет.
Худшая compaction —14,6334 мс: восемь жертв и 1 152 384 перемещённых bytes.
Steady decode максимум3,6216 мс, gather0,6171 мс. Это подтвердило отдельный
CPU cache bottleneck; GPU relocation в позднем effects не объясняет sprites.

Контролируемые ARM9 fixtures используют настоящие resident payloads с
синтетическим порядком запросов. Hash вычисляется **вне timed region** по
всем used bytes и всем active records (order/offset/age/pins). FNV совпадение
не заменяет независимый побайтовый host oracle.

| Набор | Контроль | Вариант | Результат |
|---|---:|---:|---|
| 336 одиночных requests | 1 195,606 мс | 701,920 мс | все 336 full-cache hashes равны |
| Тот же request92, 11 жертв | 37,6951 мс | 4,9511 мс | те же ID/used/evictions/hash |
| 42 группы по 8 requests | 1 171,122 мс | 676,894 →372,091 мс | per-victim →per-request →frame batch |
| 10 000 local clock reads | 147,2442 мс | 45,4208 мс | одинаковый trace hash/ticks/fraction |

Это микропрофили, а не FPS обычного маршрута. Полные отчёты и первые парные
сводки — `n23/cache-benchmark/` и `n23/clock-benchmark/` в evidence.

Независимые root proofs:

- `root-cache-review.json`:64 697 cases/6 646 598 checks, включая30 000
  request sequences, malformed RLE и partial failure prefixes;
- `root-cell-review.json`:56 739 245 scalar values/283 696 225 checks;
- `root-collision-review.json`:452 529 old/new native clear/resolve pairs;
- `root-memory-review.json`:строгий SHA-bound compiled-header budget;
- `root-codegen-review.json`:actual ARM/Thumb compile commands и instructions;
- `root-clock-review.json`:200 000 arithmetic cases и 160 000 clock operations;
- `root-cull-review.json`:564 000 quads, int64 overflow bound;
- `root-floor-runs-review.json`:32 044 rasters и 954 native PKR/PKE cases;
- `root-codec-review-mode2.json`:576 152 paired cases, 1 536 protected-page
  boundaries и 47 252 550 checks against frozen N22 unpack;
- `root-recompile-review.json`:10 исторических benchmark objects из frozen
  sources совпали по allocatable sections и symbolic relocations;
- `root-discovery-bitset-review.json`, `root-discovery-sort-review.json`:
  по 12 723 cases/50 894 checks, весь 49 620-byte workspace и visited exact;
- `map-buttons-check.json`:16 948 698 checks реального map runtime,
  включая deadline/paused/split-transfer/class1 и button/font/invalidate cases;
- `root-cache-logical-review.json`:64 825 cases, 30 000 request sequences,
  128 mixed replacement/compaction/failure sequences и 6 643 случаев с реально
  разными physical layouts; logical payloads и span partition exact;
  текущий count61 882 532 assertions; старый wrapped guard counter superseded;
- `root-collision-object-review.json`:canonical full-reference lookup и
  старый scan fallback, 1 920 uniquely bound worlds и 428 public checks;
- `root-combat-sar-review.json`:34 114 739 values×shifts, exact 32-bit SAR;
- `overlay-reuse-check.json`:6 000 frames, 2 976 visible/2 451 visible hits,
  294 912 000 framebuffer bytes exact;
- `overlay-admission-check.json`:512 frames, в 490 retained frames raw weapon
  уже вытеснен; 22 cold reload после смены view, pixels/leases/HUD exact;
- `root-cache-benchmark-review.json`:независимая проверка первой ARM9 пары;
- `root-review-inputs.json` и `root-review-inputs/`:исторические точные версии
  входов независимых proofs. Текущие paths не перепривязывают старый proof.

Свежие шесть host suites, x86 collision и 2 060 AI/combat callbacks прошли
до и после corner reuse (`n23/regression/`). Primary promoted map gate
(vertical/floor-runs/direct-seen) прошёл 17 524 full rasters, 7 784 rectangles,
четыре alignments и 1 036 615 680 pixels с guards; свежие954 PKR/PKE x86
cases тоже совпали (`n23/map-check.json`, `n23/map-native.json`). Отдельный
expanded independent audit проверил32 044 rasters/1 631 354 880 pixels
(`n23/auditor-map-direct-review.json`); это другой набор, не сумма primary gate.

Накопленный SCN контроль RU и первый ARM/cache вариант:63 cases и12 880
presentations каждый, functional PASS. Normal:44,8358 →45,3220 FPS,
минимум29,913049, все интервалы≤2 VB. Эти ранние варианты ещё не содержат
всех последующих изменений; итоговыми прогонами не являются.

Обычные ранние N23 LEVEL1 samples пока **FAIL**: варианты без fast clock
имели секундные минимумы25,50…28,92 FPS. Все такие кадры сохранены; среднее
около30 FPS и локальные ускорения не объявляются приёмкой. Первую L3 host
policy с пропуском недостигнутого key goal и ранним west retry отклонили;
она дала max19 natural AI и Game Over, не покрыла исходный burst61.
Исправленная host policy хранит цель до фактического goal_reached и меняет
retry intent с учётом настоящих keys/weapon. Ожидается новая обычная матрица.

С fast clock ordinary LEVEL1 prefix (180 cells) прошёл: 2 857 intervals,
130×1 VB +2 727×2 VB, average 30,609449, minimum 29,913049 FPS.
Clean03 LEVEL2 prefix (220 cells) ещё **FAIL**: 4 996 intervals,
1 132×1 VB +3 849×2 VB +15×3 VB, average 33,677880,
minimum 27,918846 FPS. В candidate05 добавлены измеренные decoder и door
изменения; его prefix тоже FAIL: 4 354 intervals, average 32,394333,
19×3 VB, minimum 27,918846. Candidate06 добавляет bitset/sort и buttons:
4 271 intervals, average 32,809099, 10×3 VB, minimum 27,918846 — также FAIL.
Проценты долгих кадров улучшились, секундный критерий ещё не выполнен.

Candidate08 с retained overlay: LEVEL2 6 015 intervals, average 40,243120,
minimum 28,915948, 2×3 VB. Среди 5 769 visible resource rows 5 550 имеют
прежний полный опубликованный view/serial. Это не окончательная приёмка:
две miss/map-redraw коллизии дали CPU35,59 и36,44 мс. Candidate09 исключает
лишний raw weapon admission: 3 478 intervals, average34,116276,
minimum29,422671, 1×3 VB. Путь после изменения polling отличается; средние
разных ordinary samples не являются парным speedup.

Fine08 диагностический prefix содержит 5 257 CPU frames и 8 120 callbacks,
missing callbacks0. Compaction достигает 3,95 мс при 233 600 moved bytes;
другая —3,91 мс/263 808 bytes. Именно эти перемещения проверяет replacement
эксперимент. Timer build исключается из product acceptance.

Частичный LEVEL3 candidate08: actual47 AI, 5 831 presentations,
average41,9437, minimum29,913049, один3 VB; budgetPASS для этого sample.
Он не заменяет negative03 с natural56 AI и исходную N22 нагрузку61 AI.

Transient x/z rotation cache отклонён: при точных keys и raw wrapped
transforms discovery heavy14 замедлилась 5,51534 →6,14872 мс. Flag остаётся
нулём. `mapbase02` →`mapbitsbuttons02` уменьшает heavy14 CPU на 0,3773 мс,
heavy39 на 0,7209 мс; sort добавляет ещё 0,0387/0,0097 мс. Это сравнение
одного 154-frame diagnostic packet, не ordinary приёмка.

После обнаруженного изменения C inputs во время exploratory build02 этот
label отклонён (`provenance-rejected.json`). Новые builds вычисляют hashes
всех C/header/Makefile inputs и build driver до SDK и требуют точного
совпадения после SDK. Только затем сохраняются ROM/ELF и compiled inputs.
Ранее выполненные causal cache/clock/cull benchmarks дополнительно проверены
независимой SDK recompilation frozen sources (`root-recompile-review.json`).

## Воспроизведение

Из `C:/Source/DoK-port`, общий CPython3.14.6, Zig0.13.0,
BlocksDS1.24.0-1/GCC16.2.0. SDK находится в общей пользовательской установке.
MSYS2 в ограниченном sandbox может отказать в signal pipe; команды сборки
выполнялись через разрешённый elevated SDK process, без установки toolchain.

```powershell
py -3.14 -B -X utf8 ports/nds/tools/performance/build.py <new-label-ru-or-en>
py -3.14 -B -X utf8 ports/nds/tools/performance/check_regression.py
py -3.14 -B -X utf8 ports/nds/tools/performance/check_map.py
py -3.14 -B -X utf8 research/evidence/nds/n23/root-cache-review.py
py -3.14 -B -X utf8 ports/nds/tools/performance/run_logged.py <log> ports/nds/tools/performance/ordinary.py --target <target> --level 1 --cells 180 --run-id <new-id>
py -3.14 -B -X utf8 ports/nds/tools/performance/run_logged.py <log> ports/nds/tools/run_joint_emulator.py --target <scn-target> --packet build/nds/n23/scene.dat
```

Visible LCD response на clean03 проверен фактическими pixels: 42 map responses
имеют максимум 6 VB /100,291 мс. Загрузки миров учитываются отдельно.
Повтор на окончательном RU/EN candidate остаётся открыт.

Остаются: итоговая RU/EN накопленная матрица, ordinary тяжёлые участки всех
трёх миров, окончательная visible response, audio/I/O/RAM/VRAM/stack
сводка, final archive и независимая приёмка. N24 автоматически не начинается.
