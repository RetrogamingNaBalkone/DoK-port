# Передача контекста NDS между чатами

## Текущая передача после N13P — 29 октября 2026 года

Поручение **N12 → N13 → N13P выполнено (эмулятор)**. После архитектурного решения
остановиться. Следующий ID — **N14**, его реализация ещё не начата.
[Паспорт N13P](tasks/N13P.md), [решение и команды](spec/N13P-architecture.md),
[compact](../../analysis/nds-n13p-performance.json).

Пользователь разрешил приближённое GPU-затемнение; цель — художественный замысел:
дальние объекты скрываются во тьме, создавая пугающую неизвестность. Принято решение
сохранить аппаратный3D, проекцию, UV, depth и streaming, заменить производственное
разбиение/weld LIGHT чёрным аппаратным fog. Точный N13 остаётся reference.
Кэш готовой LIGHT-геометрии дал только4,15/4,11 FPS при170640 B RAM и не выбран.

Текущая RU ROM `build/nds/n13p/fog-ru/dok_nds_effects.nds`, SHA-256
`71b7a7a031b3117c5774c2a9c8db362a5389e408f1c62e014c739c29ce6627d4`.
RU/EN:41 cases/2740 новых presentations каждый,1849 штатных интервалов все≤2 VB,
40,021/40,007 virtual FPS, minimum cadence29,913049. Audit15,80 FPS отдельно,
его41 изображения совпали с обычной RU. Все462 source-frame hashes и callback
traces совпали с baseline; состав объектов, source timing, вода и mirror UV
сохранены. Три fade полностью чернят scene+HUD; FAT неизменён.

GPU depth ramp: RGB255 при глубине≤1920,186 на2400,105 на3000,16 на3600,
полный0 на4000/4800. Source index0 alpha сохранён; дополнительные LIGHT holes
nonzero→0 заменены RGB-затемнением. Это согласованная адаптация, не точная LIGHT.
Fog settings меняются на VBlank вместе с commit; исходная VGA6/fade сохранена.

Max291 quads/287 GE polygons/1154 vertices, texture260096 B, palette8192 B.
CPU effect pool128 КиБ вместо384 внутри прежнего1 МиБ asset slab. Освободившаяся
RAM используется сырыми кадрами; весь bounded working set действующих анимаций
проверяется/читается при загрузке уровня. Measured max raw cache554880 B.
Static startup274432 B, managed2293760 B, SDK headroom446464 B,
reserve655360 B не тронут. Source world slab уже379576/393216 B: дальнейшие
состояния требуют проверки размещения; diagnostic traces можно убрать из production.
Худшая сумма measured prepare/submit/upload30,45/30,52 мс RU/EN из33,43 мс
двух VBlank; номинальные2,9 мс не включают probe/history и не гарантируют бой.

Проверки:36 effects,10516 runtime/1500 sweeps,12 pinned replacement,
7040 exact-cache/56 packets; повторены150 native crops на язык и540 actual phase
pairs. Полные ARM9 histories/traces сохранены. Быстрые кадры могут опережать
одиночный effect-metadata probe; его неполное покрытие не выдаётся за полный trace.
Baseline/точные исходники/ROM/manifests — `research/evidence/nds/n13p/`.

Следующему этапу брать fog-путь: N13P builder явно задаёт `EFFECT_GPU_FOG=1`;
default N13 Makefile оставлен для точного reference. N14 не запускать автоматически.
30-cadence принят только для накопленных fixtures. Живые AI/бой, звук и полная
touch-карта ещё не реализованы; N17P/N21P остаются архитектурными проверками.
Физическая DS/DS Lite — N24. Игровых сохранений нет; GBA/SDL2 не изменялись.

## Историческая передача после N13 — 29 октября 2026 года

Поручение — **N12 → N13 → N13P**, затем решение об архитектуре и остановка перед
N14. **N12/N13 готовы (эмулятор)**. Следующий ID — N13P; он разрешён текущим
поручением. Производительность совместного renderer ещё не принята.

Текущая RU ROM `build/nds/n13/ru/dok_nds_effects.nds`, SHA-256
`19254dd869147d5cd34fba757bd246c0cf995efa148d49cea98dd3b19409e916`.
[Паспорт](tasks/N13.md), [spec](spec/N13-effects.md),
[compact](../../analysis/nds-n13-effects.json), доказательства `research/evidence/nds/n13/`.
Новая задача не должна считать старые full reports прогонами финальной HUD ROM:
2350 presentations на вариант сняты до последней HUD-правки, после неё выполнены
отдельные592-frame RU/EN/audit regressions. Точные старые ELF воспроизведены и
сохранены; final ROM/ELF/manifests — `research/evidence/nds/n13/final/`.

Native PKR/PKE LIGHT, unsigned clamp, after-LIGHT zero mask, VGA6 percentage
доRGB555, water getters/callbacks и native fade sequences восстановлены.
16 palette rows8192 B;384 КиБ variant cache внутри1 МиБ asset slab. Shared
wall/floor edge partition устраняет DS T junction cracks; GE quad strips
сохраняют все полигоны при меньшем числе вершин. Staging1984 quads укладывается
в N03 scene slab на ARM9 и host; reserve не увеличивался и не использовался.

- 2865 native LIGHT vectors и150 crops на язык;540 actual phase pairs;
  9283 native band interiors;65192 host poses с объектами;1500 service sweeps.
- RU/EN/audit:37/2350,462 frame hashes и5983 callbacks; финальный HUD:12/592
  на вариант, весь верхний кадр чёрный на native1%. N12 regression26/1790 PASS.
- Fresh DOS3 levels: same camera/phase tuples, Native crop=actual ARM9 bytes.
  Наблюдённая LEVEL2 ВОДА1 phase3, остальные0. В LEVEL1/3 scrolling water
  не размещена. Отдельный reflected-world pass не найден в reviewed normal PLAY;
  door mirror UV отдельно от него.48 presentations real N08 clock PASS.
- До HUD-правки:1476 normal presentations, RU2,78 virtual FPS, p99/max47 VB,
  min cadence1,27; prepare314,05 ms, submit35,54 ms.60/30 цели не пройдены.
  GPU325632 B, palette8192 B;5880 vertices/1569 polygons;
  reserve655360 B, SDK headroom450560 B. Это не аппаратный FPS.

Первый шаг N13P — проверить N07P/N12/N13 manifests, повторить полный накопленный
набор уже с финальным HUD и добавить максимальные LIGHT poses из host-check.
Фиксировать baseline до правок. Измерять geometry/LIGHT/weld preparation,
cache variants и GE submission; source timing, изображения и мир не сокращать.
Можно проверять точный кэш готовой геометрии; его RAM и промахи на поворотах
обязательно учитывать. Решение о необходимости переработки принимать после
парного профиля. N14 не запускать при нехватке бюджета.

Команды и общий toolchain/core — в N13 spec/паспорте. Физические DS/DS Lite — N24.
Игровых сохранений нет. GBA/SDL2 статусы этой работой не изменены.

## Историческая передача после N12 — 29 октября 2026 года

Пользователь поручил **N12 → N13 → N13P**, затем решение об архитектуре.
**N12 готова (эмулятор)**; продолжать N13 и N13P, остановиться перед N14.
Предыдущая остановка после N11 больше не блокирует эти три задачи.

Текущая RU ROM — `build/nds/n12/ru/dok_nds_sprites.nds`, SHA-256
`bc035a51c906857ecb600bce70c4cee830faefb48a02d48d895b13e2b9827c53`.
[Паспорт](tasks/N12.md), [правила/команды](spec/N12-sprites.md),
[compact](../../analysis/nds-n12-sprites.json); полные доказательства —
`research/evidence/nds/n12/`. N07P ROM/reports теперь исторические, не переписаны.

Независимые billboard/animation API, metadata bank, RAM cache с pins и GPU
plan/submit/commit. Epoch/revision связаны с scene lease. Native initial frame0,
SAR16, signed intervals и подавление MMap стеной проверены по x86. Состояния
объектов fixtures; живые AI/бой ещё не добавлены.

- 1240 native projection vectors,12 constructors;1494 placements/4144 poses.
  24451 C checks; scan65192 poses/586728 checks; max sprites39/28/82.
- RU/EN/audit:26 cases/1790 frames каждый;462 ARM9 hashes всех кадров на вариант;
  actual N04 bytes, flat depth и21738 controlled masked pixels PASS. Контрольные
  PNG просмотрены. FAT неизменен, игровых записей нет.
- Штатные1296 presentations: RU/EN43,61/43,56 virtual FPS, все активные интервалы
  ≤2 VBlank, minimum window cadence29,913049. Цель60 не принята. Loading и
  artificial all-frame walk отдельно; audit16,41FPS. GPU max260096 B,
  palette512 B, reserve655360 B, SDK headroom499712 B.
- Timestamp cadence и integer1s count29 опубликованы отдельно: идеальные два
  VBlank не отвергаются из-за59,826Hz. 8 boundary/stall metric checks прошли.
- 439 исторических inputs и95 N07P pins проверены с точными архивами. Старый
  backend — неизменённый префикс; N12 API добавлен после него. N13 архивирует
  заменяемые N12 bytes до правок; integrity целиком не отключать.

Команды: `check_sprites.py --runtime`, затем `--variant en --runtime`,
`--variant audit --runtime`, `summarize_sprites.py`; Ghidra —
`export_sprite_evidence.ps1`. Общие Python3.14.6/Unicorn2.1.4, Zig0.13.0,
BlocksDS1.24.0-1/GCC16.2.0, melonDS DS libretro1.4.0; core SHA проверяется.
Первый шаг N13 — сверить compact/manifests, восстановить LIGHT/after-LIGHT mask,
водные фазы и реестр отражений/палитровых переходов. В N12 свет identity;
совместное архитектурное решение ещё **не принято**: это N13P.
Полная игра/карта/звук, аппаратный FPS и DS/DS Lite остаются последующим этапам/N24.
GBA/SDL2 этой задачей не менялись.

## Историческая передача N07P и более ранних задач

После остановки на N11 пользователь 29 октября отдельно поручил анализ,
оптимизацию вывода и оценку FPS текущего прототипа, затем добавление промежуточных
оптимизаций в план. **N07P готова (эмулятор)**; последующие реализации не запущены.
Следующий игровой ID — **N12**, только по новому поручению.

## Текущее состояние — 29 октября 2026 года

**N01 завершена; N02–N11 и N07P готовы (эмулятор).** N12–N24 и новые
N13P/N17P/N21P не начаты; автоматического продолжения нет.
Текущая ROM — `build/nds/perf/n11-optimized/dok_nds_interactions.nds`,
SHA `a355d7c1dabce6713c04f8c01cb41734271a5cb7db6e03546f93ea2a331364bf`.
Старые N07/N11 ROM/reports сохранены и являются историческими.

N07P: [паспорт](tasks/N07P.md), [компактный итог](../../analysis/nds-render-performance.json),
полные отчёты/архив — `research/evidence/nds/perf/`. Ускорены admission/FIFO,
frustum early exit и door pass; кадр запрашивается каждый sweep. Полный scan/hash
мира на каждом кадре вынесен в отдельный `RENDER_AUDIT=1`, обычная сборка — 0.
Обе прошли snapshot/source verifier, audit сохранил полную проверку неизменности мира.
Значительная часть роста N11 — устранение тестового overhead, а не один renderer.

- Изолированный renderer: старты LEVEL1–3 **59,83 FPS**, CPU work сократился
  на 15–18%; тяжёлые LEVEL1/2 — 29,91, LEVEL3 — 59,83 вместо 29,91.
  936 кадров, геометрия всех кадров и 19 контрольных top PNG идентичны baseline.
- N11 state-driven диагностический маршрут: **58,23 FPS** вместо 18,75;
  худший case 46,93, минимальное полное окно 1 с — 38 кадров. 100% измеренных
  активных интервалов ≤2 VBlank, 97,26% — 1; строгая 60-цель ≥99% **не достигнута**.
  Audit около 19,94 FPS. Это DS-mode эмулятор, не физический FPS и не полная игра.
- 1601 ARM9 shared check, 10 admission checks, свежие host suites и
  phase-zero scan 65192 poses/4937991 checks — PASS. В обычном runtime 654 исходных
  операций/990 hero callbacks проверены x86 oracle; audit отдельно 227/950.
  Начальные/закрытые двери совпали pixels с прежней N11, overflow 0, reserve 655360 B.

Повторять `benchmark_renderer.py --variant optimized`, `check_renderer_admission.py`,
`check_render_performance.py` и его `--audit`, затем `summarize_render_performance.py`;
всем runtime-командам передавать `--core <принятый DLL>`. Точные команды в паспорте.
Baseline пересчитывать с `--skip-build`; его исходники до оптимизации архивированы.
SDK общий, версии прежние, 20 packages закреплены. Требуется сохранённый N04 package
и точные N07 scene/trig inputs; оригиналы не менялись.

**Текущая база integrity:** N07P `host-check.json`/`host-check-audit.json`,
точные архивы заменённых renderer/build files и исправленных oracle imports,
`archive-manifest.json` и compact.
Старые N11 gates намеренно требуют прежние hashes renderer: не пересоздавать их
ради нового состояния. N07P проверяет старые pins через архивные bytes и новые
source/ROM manifests; финальная сверка — `research/evidence/nds/perf/final-integrity.json`.

По замечанию пользователя NDS-инструменты отвязаны от окружения соседнего проекта:
общий `py -3.14`, Unicorn2.1.4 в user site, `ports/nds/tools/requirements.txt`.
Пять старых `sys.path`-вставок удалены, генерируемая команда `audit_evidence.py`
исправлена. Проверка импортов — `python-environment.json`; повтор N11 на общем
Python прошёл. Игровые данные/EXE/доказательства берутся из этого репозитория.

Новая очередь: **N12 → N13 → N13P → N14 → N15 → N16 → N17 →
N17P → N18 → N19 → N20 → N21 → N21P → N22 → N23 → N24**. P-этапы обязательны;
их [протокол](spec/performance-checkpoints.md) требует совместной нагрузки всех
принятых возможностей и явного решения по архитектуре до следующей вехи.
По уточнению пользователя оставлены три крупные промежуточные точки:
N13P — объекты вместе с LIGHT/водой/отражениями, N17P — живой бой/эффекты,
N21P — два экрана/карта/HUD/звук. Отдельные P-задачи после N12/N15 не нужны:
их обычные замеры и исправления выполняются внутри реализации.
Архитектура ранней геометрии жизнеспособна, бюджет этих подсистем
ещё открыт; аппаратное подтверждение полной игры остаётся N24.
Первый шаг будущей N12: прочитать паспорт, сверить N07P manifests и принять
модель объектов/анимаций/перекрытий. GBA/SDL2 статусы и handoff не менялись.

Ниже — результаты N01–N11 до N07P; их hashes и числа относятся к тем приёмкам.

Готовы внешний пакет RU/EN, bounded loader/cache, определения и начальные данные
RU-мира с профилем и независимый 3D renderer трёх уровней. Полный игровой
цикл ещё не создан; N09 добавила service loop, N10 — исходные collision/occupancy
и диагностическую навигацию по трём RU-уровням с отдельной read-only камерой.
N11 добавила Use/keys, door machine/crop/mirror, сенсоры/conveyor/FINISH output
и direct displacement; новая эмуляторная приёмка прошла. Диагностика N02 собрана общим
BlocksDS 1.24.0-1 / GCC 16.2.0; две чистые сборки совпали по .nds/ELF/map и
версиям 20 закреплённых пакетов. Установка SDK завершена с exit 0, live job нет.
По старому handle 62711 ничего не перезапускать; временный XferCommand удалён.

Все три runtime-сценария melonDS DS **libretro core 1.4.0** прошли: marker,
отсутствующий marker и отсутствующий носитель. Проверены 12 кнопок, touch,
реальный PCM около 440,22 Гц и таймеры; порядок и читаемость экранов проверены
по PNG. Это отдельный core-проект, не standalone melonDS 1.4.0. Виртуальные
интервалы не доказывают реальные DS FPS. Физическая проверка отложена до N24.

Команды и точные хеши — [N02-building](spec/N02-building.md),
[результат N02](tasks/N02.md), [компактный итог](../../analysis/nds-n02-platform.json).
Первичные доказательства — `research/evidence/nds/n02/`; текущие
ROM/ELF/map — `build/nds/n02/`. Общий SDK —
`C:/Users/dspev/.local/msys64/opt/wonderful`, не копия в проекте.
Платформенная диагностика — GNU C11 из-за SDK asm, ядро N03 — strict C11.

Результат N03: [паспорт](tasks/N03.md), [архитектура](spec/N03-architecture.md),
[контракты](spec/N03-contracts.md), [память/SDK](spec/N03-memory.md),
[компактный итог](../../analysis/nds-n03-architecture.json).

- Ядро strict C11; typed caller-owned shell без игрового мира, immutable
  descriptors, logical IDs+generation+epoch, lifetime/scene-effects boundary.
- Бюджет ARM9 3584 КиБ по политике порта: static/startup 512, семь областей 2240,
  SDK dynamic 192, reserve 640. Потолки не означают готовую полную игру.
- Реальный probe: 42048+7104+2293760+671744+655360=3670016B.
  SDK runtime limit 0x023F4000 сначала ограничен policy 0x02380000, потом резервом
  до 0x022E0000. Иначе 640 КиБ вычитались бы из неверной границы.
- Host contracts и 28 459 проверок памяти прошли; Linux собран без запуска,
  все headers/core/tests — ARM9 strict C11. Runtime: 42 проверки, 120 состояний,
  171 frontend frames — PASS;
  весь управляемый блок независимо прочитан после cache flush. DTCM pattern 0→544 байта,
  guard OK; это не полный stack profile. Финальные экраны визуально проверены.
- Первичные результаты — `research/evidence/nds/n03/`, текущие ROM/ELF/map —
  `build/nds/n03/`. Полный повтор: `python -X utf8 ports/nds/tools/check_core.py`,
  затем `run_memory_emulator.py --core <общий melonDS DS libretro DLL>`.

Результат N04: [паспорт](tasks/N04.md), [формат](spec/N04-assets.md),
[конвертер](spec/N04-converter.md), [runtime](spec/N04-runtime.md),
[компактный итог](../../analysis/nds-n04-assets.json).

- Внешний пакет DOKNDS1: 9230016 байт, 1466 записей всех 16 типов, 292 RAW,
  738 уникальных payload и 728 aliases. SHA
  `2f03ba02c9986b4e0bd321e94ca00df8a355f7dbb1dff5598a950d3366061511`.
  Все исходные RU/EN данные и полные тексты сохранены. В ROM банк не встроен.
- Независимый oracle сверил все производные байты/provenance с RES;
  две конверсии побайтово совпали. 4759 host checks PASS, Linux только cross-build,
  ARM9/header gates PASS. Семь пар фонов и контроль аспекта просмотрены.
- ARM9 FAT/DLDI runtime: happy/missing-media/missing-package PASS; happy проверил
  все 16 типов обоих языков, реальный длинный PCM 474361 sample/8 chunks и
  GPU payload 131072 байта. Восемь control/negative checks PASS. Все 49152 байта
  cached image независимо совпали с actual ARM9 RAM. Финальные экраны просмотрены.
- Workspace 114688 байт из I/O 128 КиБ; asset pool 1 МиБ, audio staging 256 КиБ.
  Фиксированная пара candidate/backing 262144 байта — не пик полной игры.
  Heap limit 0x022E0000, reserve 655360, SDK headroom 1474560, static 92692 байта.
- Текущие ROM/ELF/map — `build/nds/n04/`, пакет — `build/nds/assets/`;
  первичные reports/logs/PNG/manifest — `research/evidence/nds/n04/`.
  Полный повтор — `check_assets.py`, затем `run_assets_emulator.py --core <DLL>`.
  Core/DLL и SDK прежние общие, точные команды/SHA в паспорте.
- Generic reader проверяет SHA/схему; converter/oracle закрепляют original SHA.
  Синтетические palette controls не являются оригинальными assets. Candidate
  при отказе может меняться; опубликованные backing/metadata/lease сохраняются.
  File adapter ограничен положительным 32-bit LONG. LIGHT/material и UV уточняют
  N06/N07/N13; LEVEL grammar/world/profile — N05, PCM playback — N18.
  Физическая microSD/DS проверка — N=24; FPS и full-game peak не заявлены.

Результат N05: [паспорт](tasks/N05.md), [профиль и границы](spec/N05-balance.md),
[исходные доказательства](spec/N05-evidence.md), [модель мира](spec/N05-world.md),
[компактный итог и числовой diff](../../analysis/nds-n05-balance.json).

- Все общие и классовые поля, точные CP866 spans, наследуемые ссылки и три карты
  восстановлены независимо от GBA. Base неизменяема, effective copy отдельная;
  fingerprints исходных bytes и модели отвергают неизвестные образцы.
- Только LEVEL1 получает 39 изменений и 35 сохраняемых условий: 81/86 speed 20,
  wake 9000, HP 42; 8C HP 12; class 9 + БОЯР_И N=24; class 9 + МЫШЬ_Л N=8;
  A0 speed 60, HP 54, scale 7, wake 3400. LEVEL2/3 и остальные параметры полностью RU.
  33 определения бояр соответствуют 31 размещению; мыши 30/30 тоже не означают
  одно размещение на glyph. Это контроль исходного образца, не список позиций.
- InitialWorld содержит подтверждённые карты, references и actor seeds, без
  полного native constructor dump или AI. Один seed на определение сохраняет
  повторы и unplaced; неизвестные pose помечены. Epoch 1/2/3 для новых миров,
  один epoch на RU/EN представления. Функция world build не принимает язык.
- Presentation выдаёт refs/text по `(glyph,class,slot)`; числовые поля EN не
  входят в мир. Все исходные bytes/spans/maps/seeds и разрешённые bindings
  шести комбинаций LEVEL1–3/RU–EN совпали с независимым original-data oracle.
- Host: 5694+21236=26930 проверок, 5000 bindings, 19 отклонённых моделей, два
  корректно переподписанных плохих пакета. Linux собран, не запущен; ARM9
  compile gates и 20 SDK pins прошли. Эмулятор: 38 assertions, 1048 frontend
  frames, шесть полных экспортов и 30 финальных наблюдений — PASS. Каждый JSON
  из actual RAM сверён целиком, максимум 475246 байт; RAM не записывается,
  кнопка A только переводит к следующему случаю.
- Main RAM: static 106716 + startup 3876 + четыре slabs 1703936 + SDK headroom 1200128
  + reserve 655360 = 3670016 байт. Resident ProbeWorld 346332/393216, scratch 105052/131072,
  I/O 114688/131072; asset pool — 1 МиБ. Тестовый JSON buffer 524288 входит в asset pool.
  Невыделенные events/scene/audio — 576 КиБ — входят в headroom вместе с минимумом
  SDK 192 КиБ и запасом code/startup 404 КиБ. Полный игровой peak/FPS не измерены;
  физическая DS — N24. Полная расшифровка памяти и границы измерений — в паспорте.
- Sources/artifacts и пакет N02–N04 неизменны. N05 ROM/ELF/map — `build/nds/n05/`;
  source manifest, profile diff, oracle/host/review и emulator/log/PNG —
  `research/evidence/nds/n05/`. Команды `check_world.py` и `run_world_emulator.py`,
  точные пути, версии и SHA — в паспорте. Игровых сохранений и записи мира нет.

Результат N06: [паспорт](tasks/N06.md), [визуальный контракт](spec/N06-visual.md),
[камера](spec/N06-camera.md), [UI](spec/N06-ui.md), [DOS-эталоны](spec/N06-references.md),
[компактный итог](../../analysis/nds-n06-visual.json).

- Приняты clip 320×160, D=200 / k=8 / K=4096, depth+200 и off-axis frustum.
  DOS 4:3 → DS 256×192, отдельные scene 154 / HUD 38, principal (127.1,76.01875),
  focal (160,192.5). 0.2604167% округления по Y без расширения обзора.
  Strict C11 oracle сохраняет low32 arithmetic/SAR/IDIV; 19626 checks PASS.
- Новый исходный DOS audit: 12 процессов/48 наблюдений, включая шесть clean
  PLAY после естественного закрытия attempt popup свежим Enter. VGA/DAC/RGB
  согласованы, scheduler работает, фазы эффектов неизвестны. Guest RAM/code
  не записывались. Для общего мира используются три clean RU-сцены, не EN-поза.
  Наблюдаемый DAC6=max(0,GAMEPn−1); причина fade не доказана, N04 не изменён.
- HUD со всеми signed16 числами, ключами/оружием/попытками/счётом; 16 текстов,
  34 полные страницы, 546 message bindings. UI verified: 65536 numeric widths,
  960 transitions, независимые 14233 glyph bytes/73728 pixels. Просмотрены
  все страницы, physical aspect сравнения и все 32 target screens.
- ARM9 bitmap viewer: happy и четыре negative scenarios PASS, 91 assertion,
  911 frontend frames. Все 16 пар сравнены целиком в RAM и RGB5/RGB8 display.
  Фиксированная RGB8 формула core `(c5<<3)|(c5>>3)` проверена для каждого
  пикселя и всех 32 значений трёх каналов. При отказе кадра старый display сохранён.
  ROM 140288 bytes SHA `517891c891ef3719cefb98a23c6d9ab5a0f8d3218920c5360cd233b379c200f2`.
- Память viewer: static 89044 + startup 28 + assets 1048576 + SDK/alignment 5136
  + headroom 1871872 + reserve 655360 = 3670016 bytes. Heap limit 022E0000,
  staging 196608 внутри assets, чтение ≤65536. Это не full-game peak.
- Текущие ROM/ELF/map/frames — `build/nds/n06/`; первичные reports, captures,
  PNG, source manifest и emulator archive — `research/evidence/nds/n06/`.
  Повтор — `check_visuals.py`, затем `run_visual_emulator.py --core <DLL>`
  для happy/bad-index/bad-hash/short-frame/bad-pixel. Точные команды — в паспорте.
  Пакет и принятые исходники/артефакты N03–N05 сохранены; GBA/SDL2 не менялись.

Результат N07: [паспорт](tasks/N07.md), [архитектура](spec/N07-renderer.md),
[исходная геометрия](spec/N07-geometry.md), [host сцена](spec/N07-host-scene.md),
[GPU](spec/N07-gpu.md), [проверки](spec/N07-validation.md),
[компактный итог](../../analysis/nds-n07-renderer.json).

- Реальные ARM9 3D-полигоны из N05 RU-world и неизменного внешнего N04-пакета.
  Original-instruction oracle проверил 12288 клеток, 23371 quad, 677759 значений,
  512 door branches; 2831 geometry checks, 20762 merge checks, 1444 AABB PASS.
  Scan 65192 cell-center poses и 22 полных packet подтвердили материалы/границы;
  это не все непрерывные камеры. UV resets запрещают actual merges;
  synthetic scene отдельно исполнила восемь merges с provenance.
- Cell-local v16: 512 world/GPU unit, origins на решётке512, общая Q12 rotation
  и combined columns. Actual GE проверил 24 вершины/шесть общих endpoint pairs.
  N06 off-axis viewport `(0,38,255,191)` даёт 154 строки сцены +38 HUD.
  Gather исправляет потерю p1, horizontal near clipping адаптировано к GE;
  точное повторение DOS raster не заявлено.
- Полные static texture sets 278528/319488/294912B помещаются в 393216B VRAM,
  admission целиком до loading, в active нет uploads/evictions. Reserve655360B
  сохранён: static/startup172032 + managed2293760 + SDK548864 + reserve655360
  =3670016B. Scene184408, assets high-water385024B; это не full-game peak.
- Финальный core 1.4.0 PASS: 19 случаев, 982 records =936render (488moving)
  +8loading +19SYNC +19DIAGNOSTIC. Все936, включая первые, ≤2VBlank и
  ≤1120380ticks. RDLINES46, sticky flags0, negative mask255. Восемь flat cases:
  286544 union pixels без внутренних щелей, 161212 exact RGB555 interiors;
  HUD и ARM9 packet точны, FAT неизменен. Реальные DS FPS не проверены.
- Thumb baseline и поздний startup-phase FAIL сохранены. Приняты ARM math,
  корректный seqlock odd-flush до payload и явная inactive→ACTIVE SYNC.
  Общий boundary sampler подтверждён disassembly; tail последнего кадра
  включён в DIAGNOSTIC. Ни один первый render не исключён из строгого бюджета.
- ROM186368B SHA `6815321607c94e1dcc20116819504de62175c94cf2647cdaf9003327ff8f5f04`.
  Текущие ROM/ELF/map/fixture — `build/nds/n07/`; постоянные reports/PNG,
  frozen inputs и manifests — `research/evidence/nds/n07/`.
  Повтор: `check_renderer.py`, затем `run_renderer_emulator.py --core <DLL>`;
  точные команды и все SHA в паспорте/compact JSON. 133 прежних pins сохранены.

Текущий ID — **N09**; часы и очередь N08 приняты на host/эмуляторе.
Свет/вода/отражения и source backdrop/far fill остаются
N13: чёрный цвет за конечным gather и identity LIGHT не являются итоговой
картинкой DOS. Actors — N12, динамика дверей — N11, gameplay HUD — N19,
карта — N20. N07 не создаёт gameplay loop, игровые часы или сохранения.
Физическая DS/DS Lite, microSD/loader и реальные FPS остаются итоговой N24.

Результат N08: [паспорт](tasks/N08.md), [API/архитектура](spec/N08-time.md),
[исходные доказательства](spec/N08-evidence.md), [валидация](spec/N08-validation.md),
[компактный итог](../../analysis/nds-n08-time.json).

- Новые strict C11 world clock и scheduler, без SDK/malloc и GBA-кода.
  Bank512 owners ×72 =36864B, в events slab64KiB. Логические refs N03,
  три slots/owner и native capacity4→3. Caller сохраняет lifetime/epoch.
- Native x86 PKR/PKE:16 scheduler cases,24 callbacks,8 PIT cases,
  11 stopwatch steps на язык. Новый C совпал в32cases и242actual clock reads,
  включая compaction/read-slot, first-slot interval, live append, ring order,
  shrinking bound, unsigned wrap/negative intervals и now_after_callback.
- Явные corrections: deferred current-owner destroy, pending activation
  заранее зарегистрированного пустого owner, запрет reentrancy/register/reset
  внутри dispatch. Однопроходный API не повторяет native busy loop при wrap
  часов внутри callback. Это отдельно доказанная адаптация, не exact native.
- Clock использует монотонный uint64 SDK timer2/64, descriptor33513982/64;
  world uint32 ticks —1193182/119, fraction сохраняется. Pause исключает
  menu time. SDK IRQ нельзя блокировать на весь low16 период (~125мс).
  Нет новой update60/GBA100tick сетки. Renderer/interpolation не вызывает
  callbacks; одинаковый service timeline отделён от render observations.
  Разные native polling samples могут давать разное запаздывание.
- Unified host/ARM9:17974shared checks,5760independentnumeric checks,
  246prior pins unchanged,24sourcepins; Linux только cross-build.
  Review actualC:12000divmod vectors и4194304lifecycle operations PASS.
- Финальный core1.4.0 PASS:398frontend frames,556trace records без потерь.
  По30synthetic callbacks при12/24/48render reads;62live callbacks,
  378точных rational transitions. Start pause126VBlank удержала15343→15343,
  pausedcallbacks0; deadline15425 сохранён, resumed callback15511, burst нет.
- RAM closure118784static/startup +2293760slabs +602112SDK +655360reserve
  =3670016B. Stack12288B, per-function .su не доказывает полный IRQ/SDK peak.
  ROM109056B SHA `41eba541a417f99bb39d48f0c241199b6b596a1feca4b8040034547e4e7adcf8`.
  Build — `build/nds/n08/`, permanent evidence — `research/evidence/nds/n08/`;
  frozen source/build/host manifests сохранены до final runtime.
- Повтор: `check_time.py`, затем `run_time_emulator.py --core <DLL>`.
  Точные команды/SHA — паспорт/compact JSON. Реальная DS, кварц/IRQ latency/FPS
  остаются N24; полный SDK uptime wrap не ожидался, большие delta инъецированы.

N09 должна принять input service timeline и camera presentation до интеграции
движения. В N10/N12/N15 пул owners нужно предварительно регистрировать либо
откладывать создание до конца sweep: register внутри callback запрещён.
Полного gameplay loop и игровых сохранений N08 не добавляет.

Результат N01: [паспорт](tasks/N01.md), [реестр](spec/N01-evidence.md),
[матрица N03–N22](spec/N01-subsystems.md),
[DOS-сценарии](spec/N01-dos-references.md),
[компактный итог](../../analysis/nds-n01-evidence.json).

- Четыре EXE/RES совпали с inventory до/после, оригиналы неизменны.
- Совпали 18 экспортных SHA, доступны 921 RU- и 930 EN-файлов функций.
  Ghidra-проекты и ASM есть; открытие проектов в N01 не проверялось.
- Семь исторических DOS-отчётов и 76 индексных кадров прошли аудит.
  Есть старты LEVEL1 RU/EN, короткие turn/forward и time-пробы.
- Матрица определяет источники всех N03–N22 и назначает владельцев пробелов.
  Процедуры поздних проверок подготовлены для трёх уровней обоих языков.
- Шесть сохранившихся DoorMirror ASM/log/manifest сохранены из временного build
  в `research/evidence/nds/n01/door-mirror/`, SHA копий совпали с источниками.
  Старый `center.bin` отсутствует; исторический manifest не исправлялся.
- Проверки ссылок/зависимостей и whitespace пройдены; 42 сводки актуальны.
  Два субагента подготовили разделы N01, третий выполнил независимый review.

Новые Ghidra/x86/DOS-прогоны и NDS-сборки в N01 не выполнялись.
Проверка целостности не означает полного восстановления поведения.
GBA/SDL2-код, планы и handoff не менялись.

## Порядок выполнения активной цели

Пользователь поручил последовательно выполнять по одной NDS-задаче,
подключать субагентов к каждой и проектировать от общего к частностям.
После закрытия ID обновлять его паспорт, реестр плана и этот handoff.
Уточнение пользователя от29 октября: **завершить N11 и остановиться**.
Это отменяет прежнее автоматическое продолжение после N11. После приёмки
цель приостановить; N12 оставить не начатой до нового указания. При незакрытых
критериях продолжается только N11.

Архитектурные приоритеты: платформа N02, контракты/память N03, пакет/рабочий
набор N04, проекция и renderer N06/N07, время и порядок событий N08.
Точечные оптимизации до принятия этих решений не начинаются; полная проверка
производительности остаётся N23. N07 измеряет жизнеспособность архитектуры.

## Что принято и не должно потеряться

- Независимое восстановление по PKR/PKE, декомпиляциям, ASM и оригинальному
  поведению. GBA — исследовательский референс, не кодовая основа. Завершение
  других портов не является зависимостью. SDK — общий пользовательский, не в проекте.
- DS/DS Lite, DS mode, BlocksDS/libnds, отдельный подготовленный пакет на microSD.
- Верхний экран 256×192: исходные 320×200 трактуются как показ 4:3,
  а не квадратнопиксельные 8:5. Сцена 256×154 и DOS-HUD 256×38 сохраняют
  исходный обзор с округлением границы. Жизнь/заряд — числами, иконки слева.
- Низ — исследуемая карта без остановки игры, drag и кнопки «−», «+», «Центр».
  Масштабы 2/3/4/6/8, старт 3; drag отключает следование, «Центр» включает.
  DOS-раскрытие восстанавливается отдельно от GBA-веера.
- Все текстовые заставки: фон сверху, полный текст снизу с пагинацией RU/EN.
  Оригинальная стартовая заставка с надписями и музыкой обязательна.
  Статические картинки готовятся offline с правильным исходным аспектом показа.
- Крестовина — ходьба/стрейф, Y/A — левый/правый поворот, R — огонь,
  удержание L — бег, X/B — Use, Select — следующее доступное оружие,
  Start — пауза. Touch не вращает игровую камеру.
- RU-карты LEVEL1–3, общий профиль монстров, язык меняет локализацию, а не мир.
  Точные числа профиля — в плане/N05: бояре 81/86 HP 42/speed 20/wake 9000,
  8C HP 12, группа бояр N24, мыши N8, первая Баба-яга HP 54/speed 60/scale 7/wake 3400.
- Три независимых уровня, исходный retry и выходы; автоматической кампании нет.
  **Игровых сохранений нет**, включая autosave и восстановление после перезапуска.
  Записываются только настройки. «Продолжить» в паузе — текущая сессия в RAM.
- 30+ FPS трактуются как вывод через VBlank (~29,9); цель 60 — каждый VBlank.
  Промежуточные N07/N23 проверяются на эмуляторе; аппаратные замеры — в N24.
  Аппаратный 3D разрешён, но исходные свет/вода/отражения не удаляются ради FPS.

## Конкретные ограничения источников

- EN input: корректный Ghidra-вход **1050:199C**, старый targeted export
  использует 1068:199C. Перед переносом исправить и проверить в **N09**.
- Automap/DoorMirror collectors передают устаревший scriptPath. Исправить
  и повторить профильный экспорт в **N20** и **N11/N13**. DoorMirror binary
  `center.bin` потребуется получить заново; постоянные ASM/log/manifest сохранены.
- DOS discovery visited не доказан draw-fixtures (**N20**); полный реестр
  отражений мира, динамического near/палитры и цветовой oracle открыт (**N13**).
- Полные DOS-маршруты LEVEL2/3, бой, высоты/вода, смерть/retry и завершение
  не проверены целыми сессиями. Конкретные процедуры/владельцы — в реестре
  сценариев. Грубые host timings не равны scheduler ticks; VGA palette/audio
  отсутствуют. Space/Ctrl attempts не считаются дверью/выстрелом.
- Общий `dos_reference.py` пока импортирует `verify_gba.png` как PNG writer;
  это legacy-зависимость исследовательского инструмента, не основа NDS-ядра.
  Он поддерживает старт LEVEL1, а не маршруты всех уровней.

## Повтор проверки N01

Из `C:/Source/DoK-port`, PowerShell; фактически использован CPython 3.14.6:

```powershell
C:/Source/wasdify/.venv/Scripts/python.exe ports/nds/tools/audit_evidence.py --phase before
C:/Source/wasdify/.venv/Scripts/python.exe ports/nds/tools/audit_evidence.py --phase after
C:/Source/wasdify/.venv/Scripts/python.exe tools/reports/summarize_reports.py --check
git diff --check
```

Полные отчёты — `research/evidence/nds/n01/audit-{before,after}.json`;
контрольные SHA и все команды — в паспорте N01. Исследования локальные,
в Git входят только инструмент, Markdown и компактный JSON. Исторические
экспорты получены Ghidra 12.1.2; новые в N01 не создавались.

## Принятый результат N09 — вход N10

[Паспорт N09](tasks/N09.md), [компактные итоги](../../analysis/nds-n09-movement.json),
[input](spec/N09-input.md), [motion/camera](spec/N09-motion.md),
[ASM/evidence](spec/N09-evidence.md), [runtime](spec/N09-runtime.md),
[validation](spec/N09-validation.md). Все три программных критерия N09 приняты;
Историческая передача N09; N10 принята ниже. Текущий результат N11 находится
в последнем разделе этого handoff; N12–N24 не начаты.

- Новые core input/controls/motion/camera и SDK input используют N07 renderer
  и N08 clocks/events без изменений. 282 прежних pins сохранены;65 source pins,
 20 команд,118511 host/ARM9 shared checks,20 SDK packages. Oracle PKR/PKE:
  по 9960+106 calls и30087 подшагов на язык,17 outputs и каждый candidate совпали.
- Исходная инерция сохранена. Противоположные команды идут последовательно,
  не нейтрализуются. Signed WORD/low32/SAR/IDIV и full distance восстановлены.
  EN input — Ghidra 1050:199C, старый 1068:199C ошибочен. Use X/B **release-only**:
  IRQstate3held не запускает Use, state2 послеbreak потребляется reader.
  `use_triggered` не заменять raw held/pressed в N11.
- Ordered ring64/2624B, counter uint64, строгий sequence; controls удерживает
  каждый фронт до native service. UI boundary на входе, каждом UI poll и возврате
  очищает pending и блокирует held/touch через release. Ошибка overflow — fault
  до следующего SDK poll. Невидимые между двумя hardware polls переходы невосстановимы.
- Герой event 416 без fixed global timestep/catch-up. Первый elapsed 0,
  stopwatch restart после движения. Свежая command mask00182A3C перед callback.
  Camera history чистая, horizon≤416, shortest arc; interpolation/reset доступны.
  Q16 heading округляется только в N07 adapter≤0,5°; motion heading 360 не меняется.
- Shared replay при одной input/service timeline дал одинаковую симуляцию
  с0/30/60 observations:31inputs/128services/20callbacks,final(15090,−300,22821,h88),
  trace3237372460/final2034872508. Это не эквивалентность разных polling schedules.
- Итоговый runtime:1173frontendframes/1085records/444inputs+renders/400active,
  192 callbacks сверены PKR x86,441camera/1084clock checks,2pause×44VBlank,
  4Use+1Select. Rpress/release20829/21164 между service20495/21165 не потерян.
  HUD9728pixels×5 точен. RAM327680+2293760+393216+655360=3670016B;
  world268704/scratch105056/scene184408/assets344064, events36864B. FAT без writes.
- ROM222208B SHA90ab2eccf3d82bd0c1e6188e84ed120ce2d4387f826f680924abe28ab5061a75.
  Сборка: `check_movement.py`; runtime: `run_movement_emulator.py --core <pinned DLL>`.
  Выходы `build/nds/n09/`, постоянные доказательства `research/evidence/nds/n09/`.
  Полные команды, versions, hashes и ограничения — в паспорте. N09 console временный,
  снимок может попасть в redraw; игровая карта N20/UI N21. Use/fire пока команды.

Промежуточная приёмка остаётся host/эмуляторной. Реальная DS/DS Lite, flashcart,
loader, реальный FPS/latency/IRQ/stack peak проверяются на итоговом хеше в N24.
Игровых сохранений нет; писать на microSD разрешены только настройки.

## Результат N10 и следующая задача: N11

[Паспорт N10](tasks/N10.md), [компактный итог](../../analysis/nds-n10-collision.json),
[архитектура](spec/N10-collision.md), [ASM/evidence](spec/N10-evidence.md),
[validation](spec/N10-validation.md). Все три программных критерия выполнены;
На момент передачи N10 входы N11 были готовы; её итог приведён ниже.
Прежние347 pins N01–N09 сохранены.

- Native square footprint/resolver и bounded actor begin/query/commit/end
  разделены. Карты принадлежат только N05 `dok_render_world.initial`, effective
  RU definitions заимствуются; flags/counters отдельные. Exclusive context
  запрещает внешнюю mutation и проверяет epoch/revision/full object reference.
- PKR/PKE Ghidra NE addresses: clear1098:0448/1030:052C,
  resolve1098:0901/1030:09E5, wrapper1068:0EB1/1020:13BB. Radius238/cell480,
  MMap перед TMap, flag0x10, signed WORD/low32/IDIV и исходный centerZ сохранены.
  Wrapper на каждом подшаге выполняет DEC/clear/resolve/INC/write occupancy;
  door counter WORD оборачивается, repeated glyph означает один объект.
- Native map-index clamp допускает реальные outside coordinates, wrapper
  перезаписывает чужой origin. NDS bounded guards отдельно защищают край,
  foreign/invalid refs, чрезмерный quarter step и занятый resolved result.
  Сначала native resolver, затем final bounds guard: sliding не теряется.
- Geometry/wrapper не зависят от y/bounds/FCMap. Это не весь callback героя:
  floor class12/13 запускают slow/effect/trigger; механики остаются N13.
  На момент N10 полной door machine ещё не было; её результат N11 приведён ниже.
- Camera: чистая интерполяция committed history с фактическим tick interval,
  reset после adjustment/reject, независимый footprint admission с исключением
  только текущей self-клетки; fallback на committed pose. Render не пишет мир.
- Gate:69 source pins/15 commands/20 SDK packages;107 unit+9909 route/driver
  host checks, ARM9 total5036=107+3×1643. Linux3static links+2header compiles,
  исполнения Linux нет. RU/EN над одним RU-миром и0/30/60 observations совпали
  на одной явной input/service timeline; разные polling schedules не приравнивать.
- Native oracle:65280 exact geometry calls,1152 wrappers/3456 substeps,
  24controlled fixtures; guards классифицированы отдельно. Ghidra12.1.2,
  Unicorn2.1.4, исходные EXE SHA до/после совпали. Полный DOS/DPMI не заявляется.
- Emulator:2753 frontend frames/1651records/675inputs,291callbacks/720steps,
  294full snapshots. Все карты/counters совпали x86:4767744+148992bytes.
  Уровни1/2/3:99/99/93callbacks,81/65/77разных XZ,352adjustments/0guards.
  672presentations=600active+72paused,672camera/1648clock checks;
  3pause/resume×48VB, tick/fraction/callback frozen;370fractional alpha,fallback0.
- RAM335872+2293760+385024+655360=3670016B;world270064,
  scratch113040,scene184408,assets high-water385024,events36864. FAT unchanged.
- ROM218112B SHA9ac4e82ccf5b17a882d1eff83e4e111114028e326cce2fa91c14050f4883deba.
  Сборка `check_collision.py`, runtime `run_collision_emulator.py --core <pinned DLL>`;
  полные команды/версии/хеши — в паспорте. Выходы `build/nds/n10/`, постоянные
  доказательства `research/evidence/nds/n10/`; архив27PNG/result/snapshots/manifests
  byte-verified. Full raw core stdout не записан; run.log явно derived.
  Независимый final review повторил сохранённый trace/x86 без runtime/rebuild.

Исторический вход N11 после приёмки N10: прочитать паспорт и принять общую door/keys/switch/trigger
state machine, владение данными и порядок событий по PKR/PKE ASM, затем детали.
Использовать существующие flags/counters setters только вне active collision
context; `use_triggered` N09 **release-only**, не заменять held/pressed.
N08 запрещает register owner внутри dispatch: pre-register pools или deferred
activation после sweep. Ключи/триггеры не встраивать в collision geometry.
GBA-код не является архитектурной основой; привлекать субагентов на каждую задачу.

На момент передачи N10 live level1→2→3 была диагностической последовательностью,
не кампанией. Billboard actors оставались N12, floor effects тогда ещё не были
реализованы; их фактический результат N11 приведён ниже. Weapon effects — N15,
lower map — N20, UI — N21.
Temporary console может показывать partial redraw/stale periodic metrics.
Все hardware checks остаются N24 на итоговой сборке; игровых saves нет,
на microSD записываются только настройки. GBA/SDL2 статусы не менялись.

## Итог N11 и остановка перед N12

[Паспорт](tasks/N11.md), [архитектура](spec/N11-interactions.md),
[исходные доказательства](spec/N11-evidence.md), [renderer](spec/N11-render.md),
[runtime](spec/N11-runtime.md), [validation](spec/N11-validation.md),
[компактный итог](../../analysis/nds-n11-interactions.json).
Все три программных критерия выполнены. Работа после N11 остановлена;
**N12 не начата**, продолжение только по новому указанию пользователя.

- Single N05 map owner и N10 flags/counters сохранены. N11 владеет door
  phase/extent/state/ready/marker и previous-FC reference. Все class7 owners
  pre-register capacity4/3 slots до dispatch; Use key mask принимает любой общий
  бит, сенсор обходит замок, class6 остаётся source no-op. Ранние дубликаты Use
  сохраняют source refusal без rollback. Keys не расходуются.
- OPEN/CLOSE/AUTO, signed occupancy, passability threshold, marker, обе задержки
  AUTO, source crop/mirror и FINISH output реализованы. FCMap class12/conveyor
  выполняется до motion; class13 реагирует на смену объекта, не соседней клетки.
  Полная stat/inventory/pickup система остаётся N14; выполненные floor effects
  N11 не откладывать обратно в N13. Переходы N22 потребляют уже готовый FINISH.
- C2 вычисляется исходной арифметикой от фактического NDS service sample:
  signed LOWWORD/min8 для extent, signed DWORD для floor divisor. Fixed30/GBA
  сетки/catch-up нет. Два public controls latch разделяют main Use вне dispatch
  и hero motion pulses; pause очищает input и оба latch, held подавляются до release.
- Новый `dok_collision_displace` вызывает resolver и occupancy commit один раз,
  сохраняет легальный conveyor350 при C2=4. Прежний quarter guard относится
  только к N09 wrapper. Два исходника N10 расширены append-only, их старые тела
  и объявления сохранены байтами; старые N10 ROM/reports/manifests не переписаны.
- Frozen `research/evidence/nds/n11/initial-dependency-integrity.json` содержит
  439 pins:437 текущих старых входов и2 архивных исходника. Supersession manifest
  закрепляет причину, старые/current SHA и byte-proof. **Следующая integrity база:
  этот frozen N11 initial + принятый N11 compact + его source/build manifests.**
  Не запускать прежний recursive `check_time.preserved_inputs()` как единственный
  dependency collector: он закономерно требует старый collision API из N10.
- Итоговый gate:101 unit,1450 renderer,73044 world,50 router,107 collision
  regression и9909 routes=84661 checks, дополнительно123 wrapper checks;
  439 preserved/89 source pins/34 commands. Linux9 static links+2 header compiles,
  без Linux execution. ARM9 shared1601=101+1450+50, failures0.
- Native oracle:14400 door/base/sensor,336 C2,2166 Use,768 direct displacement,
  6180 floor+6 отдельных controlled guards,3440 crop,16 mirror pairs,4590 geometry.
  Phase0:65192 poses/4937991 checks, прежние packet bytes/counts сохранены.
  Legal phase>=extent после нового Use обрабатывается корректно; source
  state machine не ограничивается ради renderer. Полностью открытое полотно
  скрывается без source residual strip как явная NDS-коррекция.
- Runtime PASS:4669 frontend frames,10641 traces,3383 snapshots,2417 input,
  1181 hero/979 door callbacks,1203 presentations,9 Use.992 source operations,
  16 queue deadlines/992 operand bindings,1181 floor lookups/36 IDIV/7 conveyor,
  1181 motion callbacks/0 ordinary substeps,29 finish predicates/4 sensor entries;
  9658368 MMap+1207296 counter bytes exact. Неподвижные door fixtures не выдаются
  за новый movement playthrough; движение покрыто host-регрессией N09/N10.
- Clock10635 exact transitions,C2 updates2372,camera1203,fixture resets27;
  pause/resume49 VBlank заморозила clock/fraction/callbacks. FAT SHA одинаков
  до/после unload. RAM430080+2293760+290816+655360=3670016 B;
  world273336,scratch105052,scene184408,assets high-water385024,events36864.
- ROM228352 B SHA`83ef8d4d629427febd96c0035ded9a255d4c93f908e43bbc8495ed9780e3e804`.
  Полные ROM/ELF/map/report hashes — в паспорте.20 capture sets/60 PNG;
  start/middle/open/closed для четырёх дверей просмотрены. Средние фазы24/72,
  32/96 mirror,58/173 vertical,30/88; отражённые полотна расходятся, вертикальное
  движется вверх, полностью открытая дверь не оставляет полосы. Pause/HUD проверены.
- Команды: `check_interactions.py`, `run_interactions_emulator.py --core <pinned DLL>`;
  offline-only `verify_interactions_trace.py <result.json> <snapshot-worlds.bin>`.
  Все outputs — `build/nds/n11/`, постоянные доказательства —
  `research/evidence/nds/n11/`. Итоговая сверка подтвердила source instructions,
  полные snapshots, queue/deadlines, C2, camera и EXE SHA до/после. Независимый
  `final-review.json` PASS:738 pin records/531 unique files и повтор992 source
  operations без нового гостевого запуска. Архив68 files/60 PNG byte-verified,
  manifest — `emulator/archive-manifest.json`, повтор — `offline-source-replay.json`.

После принятой N11 работа остановлена; цель приостановить по просьбе пользователя.
Следующий допустимый ID — **N12, не начата**; запуск только после нового указания.
Первый шаг после возобновления: прочитать N12 и принять модель объектов,
анимаций и перекрытий по исходникам, опираясь на N07/N11. Итоговое железо —
N24; аппаратные FPS/latency/IRQ/stack peak не подтверждены эмулятором.
Диагностические keys/score/relocation/occupancy — явные fixtures, не кампания.
Игровых saves и gameplay writes на microSD нет. GBA/SDL2 не менялись.
