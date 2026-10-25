# Передача контекста между чатами

Обновлено 25 октября 2026 года: G00–G09 готовы по уточнённым критериям.

## Текущая граница G09

**Следующая задача — G10.** G09 завершён: Start/Use открывает двери в общей
GBA-сцене с коллизиями. Замки проверяют любой совпавший бит ключа; ключи пока
выдаются только тестовым сценарием, подбор предметов — G12. Door scheduler,
горизонтальный/вертикальный crop, проходимость, повторные активации, занятость
и общие объекты по glyph подключены. Инерция G07 не возвращалась.

Спецификация, ABI, исходные адреса и ограничения: [doors-triggers.md](doors-triggers.md).
Новые `include/dok/interactions.h`, `src/interactions.c`; direct displacement
в collision для class12. `runtime.door.extent` использует прежний padding,
Object36/union20/раскладка maps не изменились; пакет ресурсов пересобран.
Class6 действительно имеет базовый no-op Use, рабочий toggle ему не выдуман.
Class13: LEVEL2 `f`→`h`, LEVEL3 `f`→`k`, event0x800, sensor обходит замок.
Повторный glyph в соседней/другой клетке — тот же объект и не новый enter edge.
Class12: conveyor100/102 двигает X/Z с коллизиями; category1000 отдаёт явный
FINISH при score==world total. G18 должен потребить событие; health=−1 и
смена уровня здесь намеренно не эмулируются. Прочие stat events — seam для G12.

Native door берёт число фаз из измеренного FPS (DS:C2), callbacks interval0
зависят от опроса. Явная политика порта: 30 фаз по сетке333,333,334 ticks,
независимо от рисования; герой/зоны —416 ticks. Delay авто-двери применяется
дважды, занятость повторно проверяется через тот же delay. Crop делит размер
до умножения на phase, поэтому открытая дверь может оставлять тонкую полоску.
Текущий renderer поддерживает два реально используемых door crop-режима;
полная анимация/billboards остаются G11, сообщения/звуки — G16/G17.

Ghidra-проекты/журналы/ASM/bytes: `build/interactions/{ru,en}/`,
`InspectInteractions.java`, `analyze-interactions.ps1`. SHA EXE до/после совпали.
На язык6000 Door/base-векторов,622 sensor routes,120 crop-векторов Unicorn
совпали с C/формулой. Sound/UI/scheduler перехвачены; crop без перехвата callees.
Это не полный DOS-session. Инвентарь всех шести исходных LEVEL —
`analysis/g09-original-level-inventory.json`; runtime-сценарии обеих локализаций:
108 door definitions,30 class6,20 sensor-клеток,2006 class12-клеток.

13 Windows suites, Linux static/Thumb compile прошли. Общий suite исполнен
на ARM. mGBA: RU/EN lock/crop/open, проход и occupancy/отложенное закрытие,
реальный Start, sensor LEVEL2/3 и повторный вход, conveyor четырёх направлений,
finish-event, смена языка при активной двери, guards/poisoned reset.
12 динамических кадров по32736 scene pixels совпали с host; все1455 камер,
HUD/палитра,443520 strips/linked payloads и time regression прошли.
Физическая GBA, оригинальный BIOS, Linux execution и CMake/MSVC не проверялись.

ROM/ELF/map/PNG: `build/g09/`; ROM32 838 040 байт, SHA
`defb27a26a0d9aff5a36444d45efb4f90b7342fa0bd0933e30fb91946b3746dc`.
EWRAM143 672 (+14 444 к G08), IWRAM code17 292/data1232,
main stack2352/запас5584. Startup449 760, worst статическая камера624 719 cycles.
Live/test-batch измерения разделены в `analysis/g09-review.json`.
Бюджет400 000 остаётся отложенным,30FPS полной игры не подтверждены;
остаток ROM716 392 байта не резерв всей игры.

Команда воспроизведения:
`powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build-gba-interactions.ps1 -CrossChecks`.
`-OriginalEvidence` добавляет Ghidra/Unicorn; библиотека read-only из общего
`C:/Source/wasdify/build/research-deps`. Все части команды выполнены отдельно;
итог `scripts/report_interactions.py` связал текущие исходники/ROM/evidence.
Новые тесты/probes/inventory/build/report-скрипты перечислены в паспорте G09.
G10 в этом проходе не начинался.

## Граница G08 до взаимодействий

**Следующая задача — G09.** G08 выполнен в своих границах; предыдущие разделы
ниже — история этапов. В общей GBA-сцене LEVEL1 герой ходит с коллизиями.
Исправление G07 без поступательной инерции сохранено, разгон поворота сохранён.
Модуль `include/dok/collision.h`, `src/collision.c`; подключён callback
`dok_collision_move` вместо free-flight в `gba/movement_probe.c`.

Правила, native ABI и защитные отличия: [collision.md](collision.md).
В обоих EXE квадратный footprint Min Dis238, Cell Side480; четыре угла
MMap, затем TMap; разрешает flags bit0x10 по object+0x15. FCMap/y/bounds
в этом пути не участвуют. Сохранены скольжение и необычная перезапись Z-center
в RUS0A90…0AD7, три SAR2-подшага, удаление/запись glyph MMap и door+0x56.
Адреса Ghidra: RUS1098:0448/0901/0E13,1068:0EB1;
ENG1030:052C/09E5/0EF7,1020:13BB; это не DPMI-селекторы/файловые offsets.
Подмена far pointers и границы арифметики явно описаны в collision.md.

Двери пока закрыты и блокируют клетку; события/ключи/открывание — G09.
Невидимые пока монстры блокируют по исходным flags (billboards — G11).
Неизвестный glyph закрыт; безопасный adapter отклоняет выход footprint за
мир, слишком большой подшаг, стирание чужого glyph и занятый итог resolver.
Это намеренная защита порта, не утверждение о точных инструкциях оригинала.
Кнопки и416-tick fixed steps из G07 сохранены, Select меняет только язык.

Ghidra-проекты/журналы/ASM/байты: `build/collision/{ru,en}/`, скрипты
`InspectCollision.java`, `analyze-collision.ps1`, `verify_collision.py`.
SHA оригинальных EXE до/после совпали. 55 152 x86-проверки на исходных
LEVEL1–3 каждой версии,1200 native-обёрток с тремя подшагами: позы/MMap/door
counters совпали. Wrapper far world-move соединяет два контролируемых CPU;
footprint/resolver исполняются без перехвата callees. Это не полный DOS-session.

12 Windows suites, Linux static/Thumb compile прошли; Linux execution и
CMake/MSVC не запускались. Общий collision suite исполнен на ARM: углы,
узкие проходы, границы, MMap/TMap/FCMap, дверь, safety. mGBA:972 fixed steps
в RU и EN на общей G03-карте с одинаковыми полями/картами, реальные кнопки,
удержание у стен, остановка без дрейфа, сохранение mutable world при языке,
guards/poisoned reset. Все1455 камер, HUD/палитра,443 520 strips/linked payloads
и G06 time regression прошли. Физическая GBA/оригинальный BIOS не проверялись.

ROM/ELF/map/контрольные кадры — `build/g08/`; ROM32 833 692 байта, SHA
`7e643146b765003bc90778f00fe78b7a1cf88ee5972075462f7a997cd1a64a64`.
EWRAM129 228 (+4 к G07), IWRAM code16 852/data1232, main stack2352/запас5584.
Startup443 062, worst статическая камера619 952 cycles (LEVEL2 offset60°);
живой motion/input/camera до37 959, CPU-кадр286 336…659 455 в пробах.
Test-only batch128 updates измеряется отдельно. Цель30FPS полной игры пока
не подтверждена; бюджет400 000 остаётся отложенным. Остаток ROM720 740 байт
не считается резервом всей игры. Качество120 колонок сцены/UI240 сохранено.

Воспроизведение: `powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build-gba-collision.ps1 -CrossChecks`.
`-OriginalEvidence` добавляет Ghidra/Unicorn; общий Unicorn читается из
`C:/Source/wasdify/build/research-deps` без изменений. Итог
`analysis/g08-review.json`, остальные отчёты `analysis/g08-*.json`.
Новые пути: collision header/source, shared cases/test, `tools/collision_{probe,replay}.c`,
скрипты analysis/build/verify/report collision; обновлены GBA integration,
native/GBA/CMake сборка, паспорт G08 и план. G09 в этом проходе не начинался.

## Граница G07 до подключения коллизий

Герой управляется GBA-кнопками в общей сцене LEVEL1. **По указанию пользователя
удалена поступательная инерция**: сразу рабочая скорость, сразу остановка,
немедленная смена направления; исходный разгон поворота пока сохранён.
Это намеренная правка оригинала, не выдавать её за точную реконструкцию DOS.
Подробности, исходные адреса и ограничения: [movement.md](movement.md).

Общий модуль `include/dok/movement.h`, `src/movement.c`; input adapter там же.
`dok_motion_original` — проверяемая исходная арифметика, `dok_motion_update` —
исправленный игровой путь. `gba/movement_probe.c` подключён к общему renderer;
квант416 native ticks (≈24,103Гц), G06 hardware clock, долг сохраняется.
Направления/округления, три quarter-substeps сохранены. y/bounds не меняются.
Коллизий и occupancy update пока нет, свободный ход сквозь стены — граница G07.
G08 подключает `dok_motion_move` вместо `dok_motion_free`; исправление инерции
сохранять. Подмена native far/huge pointers и таблиц явно описана в movement.

Кнопки: D-pad↑/↓ ход, ←/→ поворот, L/R стрейф, B бег; A Fire (G13),
Start edge Use (G09), Select RU/EN, Start+Select диагностический panic.
Состояния held/pressed/released, Start до ближайшего fixed step не теряется.
Камера следует runtime player; test-only camera/motion mailboxes после reset0.

Проверки: Ghidra обеих версий, SHA оригиналов до/после совпали;
`scripts/InspectMovement.java`, `analyze-movement.ps1`, `build/movement/{ru,en}/`.
Unicorn исполнил по2667 векторов/язык; clock/collision и wrapper перехвачены,
не считать это полным запуском DOS. Windows11 suites, Linux/Thumb compile,
shared suite на ARM, точная input/ticks траектория, реальные GBA-кнопки,
остановка без дрейфа, Use edge, RU/EN, guards/poisoned reset прошли.
Все1455 камер LEVEL1–3, независимые HUD/палитра,443520 strips/linked payloads
и G06 time suite прошли. Нового DOS-session, физической GBA/оригинального BIOS,
Linux execution и CMake/MSVC в G07 не было.

Команда: `powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build-gba-movement.ps1 -CrossChecks`.
Ghidra/Unicorn отдельно указанными скриптами либо с `-OriginalEvidence`.
Итог `analysis/g07-review.json`; остальные evidence/regression — `analysis/g07-*.json`.
ROM/ELF/map/PNG: `build/g07/`, ROM32 830 976 байт,
SHA `9bb6fd9f83ea4689893e1e166c6ea7168881a2fb39c53c41800af4e223f0baf1`.
EWRAM129 224 (+168), IWRAM code16 852/data1232, main stack2352/запас5584.
Motion/input/camera до5942 cycles в live-пробах; startup442 726, worst619 898
(LEVEL2 `(19920,−300,2160)`,60°). 30FPS полной игры ещё не подтверждены.
Остаток ROM723 456 байт не резерв всей игры. Качество G06A сохранено.

**Следующий этап — G08.** Ниже — результаты прежних проходов.

Уточнение частот оригинала: [original-cadence.md](original-cadence.md).
В RUS callback героя `1068:2127` назначается с периодом416 ticks ≈41,49мс
(номинально24,10Гц), включает движение; фактические вызовы зависят от кадров,
пропущенные периоды не догоняются. В игровом presentation-пути фиксированный
FPS/vsync wait не найден; ожидание3DAh относится к палитре. Read-only Ghidra,
скрипт/журнал/ASM сохранены, SHA оригинала до/после совпал. Это уточнение
исследования, не завершение G07.

Ранний бюджет renderer не достигнут и отложен по решению пользователя.
Тогда следующим этапом был G07; его результат теперь приведён выше.

Оценка приоритета перед G07, 25 октября: рекомендовалось переходить к G07, продолжая
замеры и локальные оптимизации при интеграции. Основания и контрольные точки
G07/G08, G09–G11 и полной игры — в конце `docs/gba-renderer-review.md`.
Это был анализ существующих отчётов без новых запусков; G07 тогда не начинался.

## Текущая граница G06A

Дополнительный проход завершён 25 октября 2026. После сравнения пользователь
явно принял **120 независимых колонок сцены**, каждая выводится парой пикселей
в поле 240×144. HUD и сообщения остаются 240 пикселей по ширине. Это согласованное
различимое изменение; незаметные упрощения также разрешены. Геометрия и исходные
текстуры сохранены. G07 в этом проходе не начинался.

Актуальная методика, формат банка, эксперименты и выводы по GBADoom:
[дополнительный проход](gba-renderer-review.md). `gba-performance.md` помечен
историческим снимком предыдущего варианта 240. ROM/ELF/map/банк/PNG —
`build/g06a-fast/`. `scripts/build-gba-early.ps1 -CrossChecks` завершился успешно.
Бюджет 400 000 отложен (`budget_met=false`, `performance_status=deferred`),
цель 30 FPS полной игры не отменена и пока не подтверждена.

ROM 32 824 608 байт, SHA
`ddda4054a8f16d3fda2e56c5d96783b085f07ce2cea0af3ec1aace69207f2741`.
Startup 440 812 вместо 599 925; worst 617 307 вместо 993 984 (−26,5%/−37,9%).
Новый worst — LEVEL2 offset `(19920,−300,2160)`,60°,58 faces.
RU/EN/reset со сменой языка 437 041…447 949. На стартовом виде обычная
presentation-проба показывает 29,86375 FPS, два VBlank на кадр. Худший CPU-вид
36,8 мс, поэтому стабильные 30 FPS на всех камерах не подтверждены.
EWRAM 129 056, workspace91 132, IWRAM-код 16 852+данные 1232;
стек 2344/нетронутый запас 5592. До 32 MiB ROM осталось 729 824 байта.

Приняты WAITCNT0x4317 без разгона EWRAM, ARM-семплер с развёрткой 16 и повторным
использованием неизменившегося texel, признаки непрозрачности исходных колонок,
первый covered interval/счётчики/face_order в IWRAM, обход сортированных индексов
без перестановки faces, заполнение незакрытого фона для больших стен.
При 240 колонках старт стал 562 426, прежний тяжёлый вид 818 490 вместо 993 984.
В режиме 120 вычисляются чётные X, записываются halfword-пары.

Банк LIGHT row 0 span 1…140 —29 762 068 байт,443 520 проверенных сочетаний,
339 821 уникальная запись. V-start объединены между views, opaque-only strips
не хранят маски. Span144 всё ещё не помещается в 32 MiB; факторизованные
крупные масштабы 382…1024 отвергнуты за слабый выигрыш при расходе около 4 MB.
Большие span/отсутствующие V-start сохраняют точный fallback. G10/G11 потребуют
пересмотра банка под свет/анимации; остаток ROM не считается резервом всей игры.

Все 1455 камер LEVEL1–3 прошли, RU→EN→RU сохраняет мир/сцену каждого уровня.
Reference рисует far-to-near без банка/новых culls; его чётные колонки явно
дублируются, затем добавляется полный HUD. Это не lossless относительно 240
и не доказательство эквивалентности DOS. Пройдены native/cross compile,
linked payload/strips, guards/poisoned reset, стек, независимый начальный
HUD/палитра, ARM time/scheduler suite.

Тестовый `g_camera_capture_hold` исправляет пропуск короткой фазы готовности
при шаге libretro в один video frame: удерживает показанный кадр ещё один
VBlank вне CPU-таймера. По умолчанию 0, обычные cadence-пробы его не включают.
Текущие отчёты: `analysis/render-early-{startup,benchmark}.json`,
`analysis/g06a-{regression,host-verification,time-regression,column-verification,memory-budget,provenance,review}.json`.
Исходная точка прохода — `build/g06a-review-before/`, эксперименты —
`build/g06a-review-*/`, сравнение — `build/g06a-column-comparison/comparison.png`.
G06 baseline не заменён. Оригиналы не изменялись; физическая GBA не проверялась.

Предел оптимизации без новых потерь графики **не доказан**. Гипотезы: статическая
геометрия/связность ортогональной карты в ROM, объединение плоскостей с учётом
дверей/масок, прямой вывод пар в скрытую VRAM. Это не измеренный выигрыш.
Backface culling ранее отвергнуто после изменения видимых пикселей.
На границе G06A движение/коллизии были впереди; G07 выполнен выше, далее G08.

## Цель и порядок

Пользователь поручил выполнять этапы последовательно без остановок между ними;
при необходимости разрешены субагенты. Целевой результат — **один GBA-ROM
с переключением RU/EN, общим балансом/геометрией и целевой частотой 30 FPS**.
Различия данных разрешаются по `docs/gba-balance-proposal.md`: стартовый гибрид
принят как руководство, неподтверждённые ветви геометрии остаются гипотезами.
База карт пока RUS, переключение языка не меняет мир/баланс.

Последнее уточнение 25 октября — сохранить качество до оценки полной игры.
G06A завершён по обновлённым критериям; недостигнутый бюджет явно записан выше.

Уточнение UI: [gba-ui.md](gba-ui.md). Сохраняем пропорции исходного игрового
поля; нижний остаток экрана (~15 px) целиком под строку сообщений. Жизнь и
заряд числами, ключи — в углах поля поверх сцены. Учесть viewport уже в G05.

Реестр и зависимости — `docs/gba-plan.md`, паспорта — `docs/tasks/`.
**G06 завершён**: модель времени и scheduler проверены на host/ARM,
подключены к общему статическому LEVEL1-ROM. **G06A завершён**: камеры и
ранние оптимизации проверены. **G07 завершён**, следующий этап — G08;
затем строго G09–G21 по порядку. Оценка после G05:
`docs/plan-review-g05.md`. Пользователь потребовал линейный план без развилок.
План и паспорта последующих задач приведены к последовательным зависимостям.
Ориентир 400 000 cycles не достигнут и отложен до оценки полной игры. Новые результаты
и ROM перечислены выше; данные G05/G06 ниже — исторические.
Общие Ghidra-проекты нельзя одновременно изменять двумя агентами одного языка.

Текущие артефакты G04: `docs/rendering.md` и тематические geometry/assets/reference;
`scripts/InspectRendering.java`, `scripts/analyze-rendering.ps1` (34 роли на язык),
`include/dok/projection.h`, `src/projection.c`, `tools/projection_probe.c`.
`scripts/probe_projection.py` прошёл: 31216 native-векторов + Thumb compile,
сохранены wrap32, SAR15 и signed IDIV. В G05 модуль реально исполняется на ARM;
полного renderer/всей игры пока нет.
Итоговый `scripts/verify_rendering.py` прошёл:34 роли каждого образца,
462 изображения,6 LIGHT,9 synthetic,1444 trig values,4 точных door vertex
records. `analysis/rendering-verification.json`. UI — **240×144 + 240×16**:
это сохранение DOS VGA pixel aspect5:6, raw clip оригинала 320×160.

## Выполненные результаты

- **G06:** `docs/time-events.md`, `src/{scheduler,clock}.c`, соответствующие
  headers, `gba/{clock,time_probe}.c`, `tests/{time_cases,test_time}.c`.
  `scripts/InspectTime.java`, `analyze-time.ps1`, `verify_time_evidence.py`;
  Ghidra exports/logs/manifests — `build/time/{ru,en}/`, проекты сохранены.
  SHA обоих оригинальных EXE до/после совпали. Новые DOS-запуски
  `build/time/dos-{ru,en}/` дали по 21 HTTP time samples, PIT period65536,
  около 10 тыс. ticks/с. `analysis/g06-time-evidence.json` хранит инструкции,
  provenance/hashes и сами samples. Точные динамические deadlines DOS,
  rollover и callback mutation в DOS не проверены; модель этих краёв —по ASM.
- Native time —PIT IRQ count +16-bit latched phase, unsigned DIV119,
  номинально 1193182/119≈10026,74 ticks/с, uint32. Сохранены literal unsigned
  deadlines (включая ошибку rollover), negative interval bits, ring order,
  first-slot interval helper, source-slot reschedule после compaction,
  shrinking owner loop bound. Не «исправлять» эти свойства без отдельного решения.
  Destroy current/deferred new owner/reentrancy исправлены явно в time-events.
- GBA timers2/3 —постоянный clock /64+cascade, timers0/1 —cycles renderer.
  Rational remainder исключает зависимость времени от render FPS;
  probe fixed quantum100 ticks, максимум 128 шагов/poll, debt не отбрасывается.
  Это сетка порта, не найденная частота native loop. Движение и полноценные
  игровые callbacks ещё не реализованы. Bind/лимит owners предстоит интеграции:
  owner56 байт,384 owners=21504 байта, прежний резерв 16384 не доказан достаточным.
- `build-native.ps1 -CrossChecks`: все 10 Windows-тестов, Linux static builds
  и Thumb compile прошли. Linux binary не запускался. ARM исполнил тот же suite
  в standalone и общей ROM. `verify_time_gba.py` проверил exact rational
  snapshots, fixed-step callbacks/debt, timer low-word rollover за 600 VBlank,
  poisoned-RAM reset. `analysis/g06-gba-verification.json`,
  `analysis/g06-renderer-time.json`; физическая GBA/оригинальный BIOS не проверены.
- Текущий standalone `build/gba-time/dok-diagnostic.gba`:6988 байт, EWRAM 204,
  SHA `72eea5c229c7d396b64b597409d89c936b14a28420523463343f39480fd2b550`.
  Текущий общий `build/gba-renderer/dok-diagnostic.gba`:2787036 байт, EWRAM 89940,
  SHA `ffb2d8b6c6018c1a7759ed61f23b00d59c7050dc294e9b85eb3bc4e804d9697d`.
  G05 guards/RU→EN→RU/reset и независимый host oracle прошли повторно.
  Полный CPU-кадр теперь включает time-poll/input/language switch; свежие
  `analysis/render-benchmark.json` и `render-host-verification.json` относятся
  к этой ROM. Исторические G05-хэши/цифры ниже —предыдущая сборка.
  Новый полный CPU-замер 10189214…17568022 cycles (max со сменой языка),
  steady37 VBlank≈1,614FPS. Main stack high-water2392, untouched5544 байта;
  IRQ/BIOS исключены. Оптимизация ещё не выполнена —это задача G06A.

- **G05:** `docs/gba-renderer.md`, тематические geometry/assets/verification;
  `include/dok/renderer.h`, `src/renderer.c`, `tools/render_tool.c`,
  `gba/renderer_probe.c`, `gba/divmod.S`. Реальная статическая сцена LEVEL1,
  class2 walls/class4/7 center planes из общего G03; cameraRU(15120,−300,22800),
  heading90.28 faces,28699 pixel writes,3 near clips; ворота совпали с G04.
  Mode4,240×144 поле +240×16 сообщения, FONT8 CP866, HP100/E0 в углах.
- `scripts/build-gba-renderer.ps1 -CrossChecks` прошёл полностью:
  9 native тестов, Linux/Thumb compilation, runtime/packing oracle,
  mGBA и независимый host oracle. Последняя полная сборка дала тот же ROM
  `build/gba-renderer/dok-diagnostic.gba`,2782924 байта,
  SHA `d41d04f79a380dd3fae8b9f9691a8acd1344634b597b5c5c572dce554db53177`.
  EWRAM 89776, renderer workspace54284. Watermark main stack 2400 байт,
  untouched5536; IRQ/BIOS выше 03007F00 не измерены.
- `analysis/render-benchmark.json`, `analysis/render-host-verification.json`:
  RU→EN→RU сохраняет все 33864 байта мира,1344 байта faces и 34560 scene pixels.
  Все 38400 индексов с независимо составленным HUD и 512 palette RAM bytes
  совпали для трёх captures. Guards, capacity1 отказ, poisoned-RAM reset passed.
  В ELF/ROM исполняются 16 edge divmod vectors через ARM/Thumb wrappers.
- **30 FPS не достигнуты:**10175200…10187755 cycles за render+HUD+VRAMcopy
  против 561792; steady36…37 VBlank≈1,637 FPS,18,13× бюджета. Для будущей
  интеграции требуется существенная оптимизация; этот результат не считать
  приемлемой конечной частотой. G05-критерий первого кадра и измерения выполнен.
  Пол/потолок diagnostic flat, LIGHT row 0, Q7-U approximation, heading90 only;
  полный raster, billboard, анимации, движение, игровой цикл и HUD пока впереди.

- **G03:** `scripts/pack_gba_assets.py`, `include/dok/rom_assets.h`,
  `src/rom_assets.c`, `tools/rom_assets_tool.c`, `tests/test_rom_assets.c`,
  `scripts/verify_packing.py`; спецификация `docs/gba-assets.md`.
  Детерминированный пакет 2763046 байт,143 resource IDs,286 исходных payload
  вариантов,3 общих уровня×2 языка. Профиль —39 изменений от RUS по proposal;
  прочие 85 исходных numeric differences/87 map cells явно отнесены к RUS базе.
  `analysis/packing-report.json` подтверждает все 6 views, исходные переводы,
  binding IDs/диапазоны,23 corruptions и побайтный детерминизм пакета/manifest.
- `scripts/build-gba-assets.ps1 -CrossChecks` прошёл целиком:
  **9 Windows-тестов**, Linux/Thumb compile, runtime/packing oracle, ARM ROM.
  `build/gba-assets/dok-diagnostic.gba` —2776104 байта, EWRAM 34572;
  пакет в cartridge совпадает с offline assets.bin побайтно.
  Все 6 original-derived views реально reset на ARM. Select меняет RU/EN
  textpool; все 33864 байта mutable мира сохраняются, poisonedRAM reset проходит.
  `analysis/g03-gba-verification.json`, PNG `build/gba-assets/assets-{ru,en}.png`.
  Это технический asset экран, не игровой renderer/HUD.
- AssetsProbe резервирует **8 KiB IWRAM stacks/BIOS** через LLD override:
  локальные 580-байтные views reader могут вкладываться. Default G01/G02 —1KiB.
  Глубина стека пока не измерена; `gba/memory.c` добавляет проверенные AEABI
  copy/clear/unsigned-division helpers для freestanding ROM.

- **G00:** `scripts/dos_reference.py`, `docs/reference-runs.md`,
  `analysis/reference-manifest.json`. Пять неизменённых DOS-запусков
  в `build/reference/{ru-01,ru-02,ru-03,en-01,en-02}/`.
  Стартовые VGA-индексы побайтно повторяются внутри языка. Реальные позы:
  RU `(15120,-300,22800)/90`, EN `(15120,-300,23280)/90`, HP100, energy0, lives3.
  Input — исследованная таблица игры через localhost API, не IRQ1;
  PNG — grayscale индексы, DAC palette пока не собрана. Поворот/движение
  наблюдались, wall-clock удержания не дают идентичной конечной позы.
  Стена/дверь/подбор/оружейный выстрел/AI/смерть/переход пока pending.
- **G01:** `gba/startup.S`, `gba/gba.ld`, `gba/main.c`, build/run/verify scripts.
  ARM startup/IRQ, Thumb C, Mode4, кнопки, счётчики, controlled panic/reset.
  Диагностический ROM 2560 байт; native `.data/.bss`40 байт EWRAM.
  `docs/gba-platform.md`, `analysis/g01-verification.json`.
- **G02:** `include/dok/runtime.h`, `src/runtime.c`, `tools/runtime_tool.c`,
  `tests/test_runtime.c`, `scripts/analyze_runtime.py`.
  Immutable ROM descriptors/typed initial states отдельно от RAM runtime.
  Один объект на glyph, последнее размещение повторов, explicit unknown
  неразмещённых актёров. Все 6 исходных миров/780 definitions/69623 known bytes
  и полные masks/все class fields/resources/texts/maps сохранены.
  `analysis/runtime-verification.json`, `docs/gba-memory.md`,
  `analysis/gba-memory-budget.json`.
- ARM runtime **33864 байта**: objects256×36, actors128×64, player60,
  maps4×4096 плюс указатель/счётчики. Max исходных defs158, actors106,
  field records1655, textpool8849. Actor reserve22 — инженерный запас,
  не доказанный лимит активных снарядов/врагов оригинала.
- `gba/memory_probe.c` + `scripts/runtime_memory.py`:
  `build/gba-memory/` ELF/map/layout/ROM 7640 байт. С реально размещёнными
  резервами будущих buffers EWRAM 178800, свободно 83344.
  Резервы 144896 — не готовые подсистемы. Верхний 1KiB IWRAM пока reserved
  stacks/BIOS; глубина стека не измерена. `analysis/g02-memory-run.json`
  подтверждает ARM synthetic reset/position/release, startup, keys, IRQ,
  panic, poisoned-RAM reset. Это не запуск всех 6 игровых миров на ARM.
- Прошли **7 Windows-тестов**, Linux static builds и Thumb compile checks.
  Cross binaries не исполнялись. Для Zig0.13 нужен явный
  `thumb-freestanding-eabi`: прежний `arm-freestanding-eabi -mthumb` давал ARM.

## Команды и общие инструменты

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build-native.ps1 -CrossChecks
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build-gba-assets.ps1 -CrossChecks
C:/Source/wasdify/.venv/Scripts/python.exe scripts/analyze_runtime.py
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build-gba.ps1 -Verify
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build-gba.ps1 -MemoryProbe -OutDir build/gba-memory -Verify
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/run-gba.ps1
```

- Общий Zig `%LOCALAPPDATA%/Programs/Zig/0.13.0/zig.exe`, LLD18.1.6.
  Build scripts временно задают кеш `build/zig-cache`, PATH не менялся.
- Python `C:/Source/wasdify/.venv/Scripts/python.exe`, stdlib.
- Общий mGBA standalone0.10.5 — `%LOCALAPPDATA%/Programs/mGBA/mGBA-0.10.5-win64/`.
  Автопроверки в отдельном ядре 0.11-212-7a12d6d
  `%LOCALAPPDATA%/Programs/mGBA/libretro-20261024/mgba_libretro.dll`;
  источники/хэши `analysis/gba-tools.json`.
- Общий DOSBox Staging0.83.0(7b400):
  `%LOCALAPPDATA%/Programs/DOSBox-Staging/wasdify-webserver-20261024/`,
  источник read-only стенд WASDify, exe SHA
  `67a907289bad3e25a9be7dd4dc38d882c1c7e74949f8daa103a52476d99f24f5`.
  normal/Pentium/60000cycles,32MiB,svga_s3,VGA320×200 без -r,SDL dummy.

## Исходная база и ограничения

Оригиналы `originals/pkr/{RUS,ENG}` не изменялись; EXE/RES SHA в
`analysis/inventory-{ru,en}.json`. Полные runtime данные и packages только
в ignored `build/`. GBA RAM не должна содержать host converter storage70716
или полный `dok_world_object.bytes/known`; это материал инструментов.
G03 использует отдельные языковые ROM pools/views без двух mutable миров.
Пакет/manifest/generatedassets находятся только под ignored `build/gba/assets/`.

Общая Ghidra `C:/Users/dspev/AppData/Local/Programs/Ghidra/ghidra_12.1.2_PUBLIC`,
проекты `build/ghidra/{ru,en}/Kremlin.gpr`, экспорт `decompiled/`.
Java/headless настройки — `scripts/annotate-stage6.ps1`.
Существенные доказательства этапов 2–6: `docs/resources.md`, `animations.md`,
`sound.md`, `levels.md`, `world.md`; различия/параметры — `enemy-parameters.md`,
`enemy-tables.md`, `weapon-parameters.md`, `level-version-comparison.md`.
Материалы `C:/Source/wasdify` только для чтения, происхождение/ограничения
`docs/wasdify-findings.md`. LEVEL и world имеют разный порядок плоскостей.

Физическая GBA, оригинальный BIOS, GUI/GDB-сессия и
производительность полной игры пока не проверены. Исторические~20 обновлений
текста диагностики не являются оценкой рендера игры или достижением 30FPS.
G06/G06A/G07 закрыты; продолжать с G08 по линейной очереди. Снижение качества отложено.
