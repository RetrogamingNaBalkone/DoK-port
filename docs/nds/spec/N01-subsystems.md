# N01: матрица подсистем и источников N03–N22

Проверка доступности выполнена 28 октября 2026 года. Это карта входов для
независимого восстановления NDS, а не готовая архитектура движка и не заявление
о полной семантической эквивалентности DOS. Реализации GBA не копируются.
Реестр образцов, новая сверка SHA-256 и покрытие DOS-сценариев находятся в
[N01-evidence.md](N01-evidence.md); здесь собраны подсистемы и адресные опоры.

## Образцы, адреса и сила доказательств

Все адреса `xxxx:xxxx` ниже относятся к **NE-loader Ghidra** для образцов:

| Образец | Ожидаемый SHA-256 |
|---|---|
| `originals/pkr/RUS/PKR.EXE` | `12f78afc0eba05f76b5dc0bb99da38d7daa20585024c0ba958fe828aa3864d28` |
| `originals/pkr/ENG/PKE.EXE` | `cce7bc5993d0734ad87e93db37a70b8b52faedfff14ac48d6e317e5ac996a37b` |
| `originals/pkr/RUS/PKR.RES` | `af7135fbcfc765c1e963682a29b108ffe90d652d96fa130cf9db6b217277b816` |
| `originals/pkr/ENG/PKE.RES` | `865da340621fa6f90548a84e9ae9bf0a85071befb32a40ca1d359e488f74d458` |

Это не файловые смещения EXE, не индексы NE-сегментов и не runtime-селекторы
DPMI. Смещения полей `+0xNN` относятся к конкретному native-классу; glyph —
байт определения LEVEL. Адреса между RU/EN не переводятся общим смещением.
Дисковые RES-смещения отдельно даны в
[сравнении уровней](../../originals/level-version-comparison.md).

В матрице различаются четыре основания:

- **ASM** — сохранённый Ghidra-листинг и данные с документированным образцом.
  Он подтверждает инструкции конкретного пути, но не полноту всех вызовов.
- **x86-срез** — прежнее контролируемое исполнение экспортированного кода
  оригинала в Unicorn. Перехваченные зависимости и область fixtures указаны в
  соответствующем исследовании; это не полная DOS-сессия.
- **RES** — разбор исходных блоков с хешами и независимыми проверками формата.
- **DOS** — снимок или трасса работающего оригинала; общий статус сценариев
  учитывает N01-evidence. Старые host/GBA-сравнения сами по себе не являются DOS.

Автоматический C используется для навигации. Успех декомпиляции, смысловая
метка `function-roles.tsv` и наличие файла не доказывают правильность сигнатуры.
До переноса каждой функции остаются обязательными проверка signed16/32,
far/huge pointers, `LES`, DS/SS, cleanup стека, `RETF`, переполнений, сдвигов и
значимых ASM-эффектов. Неизвестные байты native-классов не становятся padding.

## Фактическая доступность

При проверке существовали оба `research/ghidra/projects/{ru,en}/Kremlin.gpr`,
оба индекса `decompiled/{ru,en}/functions.tsv` и оба `listing.asm`.
121 локальная ссылка из разделов «Конкретные входы» паспортов N03–N22
разрешается в существующий файл. Это проверка доступности, не исполнение
каждого инструмента и не проверка всех производных файлов внутри отчётов.

Для обоих языков существуют `evidence.asm` в
`research/evidence/{stage2,stage3,stage4,stage5,stage6,rendering,surfaces,time,movement,collision,interactions,sprites,items,combat,ai,audio,ui,flow,enemy-fields,weapon-fields}/`.
Для `stage2`–`stage6` и `rendering` существуют также `roles.tsv`.
В `research/evidence/automap/{ru,en}/` листинг называется **`automap.asm`**;
есть `manifest.json`, бинарные сегменты, `automatic.c` и `ghidra.log`.
Cadence-выдержки `research/evidence/cadence/ru/` имеются только для RU.

Выборочно проверены реальные строки:

| Проверка | Текущий файл и найденная инструкция |
|---|---|
| Scheduler | `research/evidence/time/ru/evidence.asm`: `1038:0284 MOV AX,SS` |
| LIGHT | `research/evidence/rendering/ru/evidence.asm`: `1088:0544 MOV AX,SS` |
| Карта RU/EN | `research/evidence/automap/{ru,en}/automap.asm`: входы `1098:1290` / `1030:1374`, `MOV AX,SS` |
| Flow/death | `research/evidence/flow/ru/evidence.asm`: `1068:193E MOV AX,SS` |
| Отражение двери | `research/evidence/nds/n01/door-mirror/ru/evidence.asm`: `10B0:03EE CMP word ptr [BP + 0x1C],0x1` |
| Настоящий EN input | `decompiled/en/listing.asm`: `1050:199C MOV AX,SS`; индекс даёт `game_poll_input`, 915 байт |

Новые Ghidra-анализы и x86-прогоны при составлении этой матрицы не выполнялись.
Оригинальные EXE не дизассемблировались вне Ghidra и не изменялись.

### Найденные ограничения воспроизведения

1. В [movement.md](../../gba/movement.md) и текущем
   `tools/ghidra/InspectMovement.java` указан ошибочный EN input `1068:199C`.
   В сохранённом movement-листинге по этому адресу находится `MOV EAX,[BP-0x3E]`
   внутри другого тела. `analysis/originals/function-roles.tsv`, EN functions.tsv
   и полный EN listing.asm согласуются на **`1050:199C`**. **N09** должен исправить
   вход экспортёра и получить новую проверенную выдержку перед переносом ввода.
   Ошибка не отменяет остальные адреса movement автоматически.
2. Сохранившиеся доказательства DoorMirror перенесены копированием из
   `build/gba/g20b-playtest2/original/{ru,en}/` в постоянный
   `research/evidence/nds/n01/door-mirror/{ru,en}/`: доступны ASM, журнал и
   неизменный исторический manifest. `preservation.json` фиксирует источник и
   идентичные SHA шести файлов (91889 байт). **`center.bin`, названный в manifest,
   отсутствует**. Полный старый x86-прогон без восстановления этого входа повторить
   нельзя. **N11/N13** должны заново экспортировать требуемые данные в постоянный
   `research/evidence/nds/` и записать новые SHA/журнал; ASM остаётся адресной опорой.
3. `tools/originals/analyze_automap.py` и `analyze_door_mirror.py` передают
   `-scriptPath ports/gba/scripts`, тогда как `InspectAutomap.java` и
   `InspectDoorMirror.java` находятся в `tools/ghidra/`. Их текущие команды
   нельзя считать проверенными после реструктуризации. Исправление пути и
   проверка read-only экспорта нужны до новых прогонов: владельцы **N20** и
   **N11/N13** соответственно. Исторические manifests содержат старые `scripts/`
   и иногда `build/ghidra/`; это происхождение старого запуска, не текущие пути.

## Матрица N03–N22

В колонке RU/EN перечислены существенные различия источника либо граница
сравнения. Отсутствие известного различия не объявляет все ветви одинаковыми.
Коды источников раскрыты в следующем разделе.

| ID и подсистема | Ghidra/ASM и существующие доказательства | RU/EN | Что относится к GBA/NDS и что остаётся проверить |
|---|---|---|---|
| **N03 — контракты и память** | S1/S2: world init `1098:004F` / `1030:0079`, owners `1038:0000` / `1010:13AA`, player `1068:1657` / `1020:1B61`; ASM/RES описывают поля и владение оригинала, S3 — набор render records. | Разные сегменты, размеры тел и размещение данных; единый native layout предполагать нельзя. | N03 принимает собственные `PlatformServices`, `InputFrame`, `GameState`, `SceneSnapshot`, `AssetCatalog`, `BalanceProfile`, логические ID и lifecycle. Compact GBA runtime, лимиты owner/actor и ROM views не являются контрактами NDS. До реализации механик закрепить границы ядра, render-side-effects/AI, время и бюджет 4 МиБ с резервом ≥10%; VRAM/DMA/TCM отдельно. |
| **N04 — пакет ресурсов** | S1: RES loader `10E0:0000/046B` / `1058:0C02/1085`; image reader `10F8:02D9` / `1060:034D`; animation `1050:026B` / `1018:11B1`; sound reader `10E8:007C` / `1058:1254`. RES и stage2–stage4 ASM. | 146 имён, 134 одинаковых блока; различаются тексты, LEVEL1–3 и POPAL. | DOKGBA, baked banks и GBA runtime flags не являются форматом NDS. N04 задаёт версию пакета, происхождение, дедупликацию, границы и рабочий набор microSD; проверяет тексты отдельно от изображений, FONT, LIGHT и PCM. |
| **N05 — мир и баланс** | S2/S4: placement `1098:0EF7` / `1030:0FDB`; enemy fields `1068:36B1/3747/37CF/3845` / `1020:3E14/3EB0/3F38/3FAE`; исходные LEVEL и `balance-tables.json`. | LEVEL1 отличается картами и числовыми параметрами; в LEVEL2/3 параметры врагов одинаковы, некоторые другие определения/TMap различны. | Только явный профиль NDS-плана принимается поверх RU-миров. В отчётах различать 33 определения бояр LEVEL1 и 31 размещение; мышей 30. N05 даёт old/new/reason, не переносит дополнительные GBA balance/aim/distance изменения. |
| **N06 — проекция и макеты** | S3: camera `1100:00F5` / `1060:1025`, plane/billboard `10B0:0000/0CCB` / `1040:0000/0CE3`; ASM low32 IMUL/SAR/IDIV, DOS LEVEL1 и `projection-probe.json`. | Стартовые позы LEVEL1 по Z отличаются на 480; адреса констант не имеют общего сдвига. | Сцена 240×144, 120 колонок и GBA-HUD не масштабируются как готовый NDS UI. N06 отдельно принимает DOS 4:3 → 256×192, scene154/HUD38, округление и три уровня; требует эталоны низких потолков/высот и статических экранов. |
| **N07 — ранний renderer** | S3/S7: builder `1098:30C1` / `1030:3571`; горизонтали `10B0:1007/15F4` / `1040:102B/1624`; column visibility `1100:0301` / `1060:128F`; поверхности и ASM. | Семантические роли сопоставлены; EN builder больше и включает дополнительный lifecycle. | GBA software/BG/OBJ/BSP trials и NDS feasibility — исследовательские варианты и прогнозы. N07 принимает геометрию, fixed-point масштаб, cache/VRAM/geometry budgets и измеряет настоящую DS. Доказательств NDS FPS сейчас нет. |
| **N08 — scheduler и часы** | S5: PIT reader `1130:015D` / `1078:17F3`; schedule `1038:0108` / `1010:14BE`; dispatch `1038:0284` / `1010:163A`. ASM обеих, G06 x86/host evidence и грубые DOS time-probes; cadence callback отдельно RU. | Основные очереди описаны для обоих; подробный hero/present cadence-pass только RU. | 100-native-tick fixed steps, catch-up/debt и GBA timers2/3 — адаптации. N08 не объявляет 24,1 Гц частотой всей логики, проверяет период/порядок каждой группы событий, паузу, wrap и lifecycle; опасные случаи и выбранные исправления документирует отдельно. |
| **N09 — ввод и движение** | S6: input RU `10C0:1639`, **EN `1050:199C`**; movement `1068:0A85` / `1020:0F8F`, callback `1068:2127` / `1020:26D7`; ASM и `g07-movement-evidence.json`. | Настоящий EN input требует исправления ошибочного targeted export, см. выше; movement роли не переводятся общим delta. | Удалённая GBA поступательная инерция — сознательная правка G07. N09 восстанавливает DOS-разгон/затухание и отделяет DS-кнопки, edge latch/consume и camera prediction. Replay/input timing нуждаются в привязке к N08. |
| **N10 — collision** | S6: footprint `1098:0448` / `1030:052C`; resolve `1098:0901` / `1030:09E5`; three substeps `1068:0EB1` / `1020:13BB`; ASM, x86-срезы и `g08-collision-evidence.json`. | Обе версии имеют адресные выдержки; исходные карты/стартовые состояния различаются. | Дополнительные защитные guards compact GBA/лимиты входов не объявляются DOS. N10 проверяет occupancy, скольжение, знаки высот, порядок трёх подшагов, края, углы и узкие места на RU-мире; динамика двери интегрируется в N11. |
| **N11 — двери/триггеры** | S6/S8: Use `1068:1B10` / `1020:20C4`, door `1068:347E/3262/334E/3427` / `1020:3B33/3917/3A03/3ADC`, sensor `1068:091E` / `1020:0E1C`; ASM/x86 interactions и DoorMirror. | Срезы обеих, тексты ключей различны; независимые карты проверяются отдельно. | Crop не заменяет геометрическое отражение полотна. N11 восстанавливает default/no-op, ключи, occupancy, conveyor/sensor/finish, отражение и отсутствие полосы при полном открытии. Finish пока событие; N22 принимает flow. Недостающий center.bin и пути экспортёра — выше. |
| **N12 — sprites/animation/occlusion** | S1/S3/S7: sequential/clamped `1050:0054/00DF` / `1018:0F62/0FED`; base callback `1068:0421` / `1020:0901`; billboard `10B0:0CCB` / `1040:0CE3`. 154 кадра/язык, G11 x86 900 projection vectors. | POPAL отличается тремя пикселями последнего кадра; остальная классификация — `animations-comparison.json`. | GBA программная сортировка/масштаб и lease-политика не являются DS billboard. N12 закрепляет frame/pivot/crop/scale, post-LIGHT transparency вместе с N13, перекрытия и overflow lists; анимация следует событиям, не FPS. |
| **N13 — LIGHT/вода/отражения** | S3/S7/S8: light `1088:0544/0498` / `1028:196E/18C2`, spans `1010:0000/00D2` в обоих; crop `10F8:05C9` / `1060:06D9`; surfaces ASM/x86. | LIGHT1–3 и GAMEP соответствующих уровней совпадают; near offsets RU `DS:0DFA`, EN `DS:0EA2` (+A8). | Нужен полный реестр эффектов и проверка динамического near/palette; зеркальность UV двери не доказывает эффекта отражения мира. Аппаратная RGB-модуляция, far cutoff и GBA draw-distance trials не эквивалент LIGHT. N13 принимает LUT variants/cache, поверхность по дистанции и одинаковые poses/фазы DOS/DS. |
| **N14 — items/stats/inventory** | S9: stats `1068:1CC2` / `1020:2276`, register weapon `1068:1EEC` / `1020:249C`, item `1068:2F8A` / `1020:362D`, hero callback `1068:2127` / `1020:26D7`; ASM/RES, 5274 x86 stats vectors и 16 item callback по G12. | Локализованные сообщения, отличия исходных определений перечислены в item inventory/LEVEL comparison. | GBA изначально открывавшая всю карту логика флага была ошибкой. N14 восстанавливает caps, категории, попытки/счёт, ключи и порядок pickup→zone→movement; предмет карты лишь разрешает исходные объекты в visited. Состояние отделяет от текста N19 и карты N20. |
| **N15 — оружие/снаряды** | S4/S9: select `1068:1FCD` / `1020:257D`; clone `1068:2368` / `1020:2960`; spawn `1068:2669` / `1020:2D0C`; flight `1068:26E8` / `1020:2D8B`; g13 x86/ASM. | Значения оружия/снарядов сопоставлены в weapon-parameters; различия текстов не должны менять урон. | N15 отдельно фиксирует безопасную проверку слотов и исправление owner-ring ошибки; DOS опасное чтение не копируется буквально. Не наследует GBA aim assist/изменения оружия; проверяет темп, charge, falloff, collision/reclaim и события целей для N16/N17. |
| **N16 — AI wake/course/motion** | S4/S10: wake `1068:3747` / `1020:3EB0`, active `1068:3845` / `1020:3FAE`, getter `1068:3ECA` / `1020:4633`, integer angle `10C8:01C5` / `1058:01C5`; ASM/x86 G14. | Исходный LEVEL1 N10/N24, мыши N8/N4 и wake/speed различаются; NDS применяет принятый N05 профиль. | GBA consideration leases предыдущей сцены — собственная sampling policy. N16 восстанавливает render/wake связь, строгое N+1 и порядок RNG; не меняет wake из-за far culling и не заменяет исходные условия произвольным LOS. |
| **N17 — атаки/смерть** | S10: active `1068:3845` / `1020:3FAE`, damage `1068:37CF` / `1020:3F38`, death entry `1068:3D98` / `1020:4501`, completion `1068:3E8D` / `1020:45F6`; g15 x86 fixtures/ASM. | Шаблоны/ресурсные связи врагов обеих версий сопоставлены; числовая база — N05. | G14 промежуточное прекращение motion при HP0 не является полным G15 поведением. N17 проверяет sleeping-first-hit, melee/ranged серии, hurt/death, corpse/owner removal и отправку смерти героя в N22; дисковых checkpoints нет. |
| **N18 — audio** | S1/S11: sound wrappers `1108:01F7/0232/026D` / `1060:173D/1778/17B3`; mono admission `10F0:005E` / `1058:186A`; DMA `1118:046E/0829` / `1070:046E/083D`; stage4/audio ASM и g16 x86 admission/loop fixtures. | Все 23 пары звуковых payload совпадают; локализованные ресурсы/привязки нельзя объявлять различными без данных. | GBA DMA/timer quantization и одноканальная реализация не переносятся как NDS-тракт. N18 принимает ARM7/IPC/buffering, priority/loop/pause, splash music; нужны аппаратный capture и прослушивание DS. GBA listening не закрывает DS. |
| **N19 — HUD/text** | S12: glyph `1058:0158` / `1018:153E`; string `1058:02AC` / `1018:1692`; HUD `10C0:0BA1` / `1050:0F04`; message `10C0:1278/12D0` / `1050:15DB/1633`; ASM/ui evidence. | Полные CP866-тексты и длины различаются; FONT8/FONT16 общие ресурсы. | GBA обрезки/сокращения сообщений, статические кеши и раскладка 240px не разрешают обрезать NDS-тексты. N19 реализует принятый N06 HUD, numeric HP/charge/icons left, dirty updates и смену языка без пересоздания мира. |
| **N20 — automap** | S13: draw `1098:1290…30C0` / `1030:1374`; automap ASM, original x86 24 сравнения и arrow fixtures. | Примитивы проверялись на всех трёх исходных мирах каждого языка; NDS показывает RU-геометрию. | GBA pause-map и visited-веер не являются DOS discovery. N20 восстанавливает render records→visited, символы/цвета и ограничения map-item; DS touch/zoom/follow принимает по плану при работающем мире. Исправить воспроизведение exporter path. |
| **N21 — splash/menu/settings** | S12/S14: splash `1008:01BA` / `1008:023E`; selection `1008:02F7` / `1008:044B`; music calls RU `1008:0275/0294`, EN `1008:0304/0323`; исходные ресурсы и frontend original verifier. | Имена уровней/тексты различаются, исходный splash payload общий; год исходно многотональный. | GBA одноцветный год/копирование copyright 1:1 — адаптации. N21 использует полную исходную картинку/музыку, DS-два экрана/полные страницы; settings-only microSD — новый контракт. Проверить короткий ввод при I/O, блокировку до отпускания и повреждённые settings. |
| **N22 — flow/retry/exit** | S14: startup RU `1008:0719`, EN `1008:086D` с render entry `1008:0AA9`; death `1068:193E` / `1020:1EFC`; restore `1098:125B` / `1030:133F`; flow ASM/x86, RES exit audit. | Три независимых LEVEL в обоих; FCMap B2 exits 2/2/4, category99 total=0 во всех шести исходных LEVEL. | GBA меню/пауза, карта и строки страниц — адаптации. N22 не добавляет кампанию/score goal, восстанавливает retry pose30 и сохраняемые поля текущего мира; проверяет cleanup нового мира, все ветви RU/EN и обычное прохождение трёх уровней. |

## Каталог источников матрицы

Пути доказательств ниже локальные и не включаются в поставку. Отчёты
`analysis/gba/` описывают исследования оригинала и проверки GBA раздельно;
их имя не превращает весь отчёт в первичное доказательство DOS.

| Код | Документы, постоянные доказательства и воспроизведение |
|---|---|
| **S1** | [resources](../../originals/resources.md), [animations](../../originals/animations.md), [sound](../../originals/sound.md), [levels](../../originals/levels.md), [binary-format](../../originals/binary-format.md). `research/evidence/stage{2,3,4,5}/{ru,en}/`, `resources/`, `animations/`, `sounds/`; `analysis/originals/{resources,animations,sounds}-{ru,en}.json` и comparisons. Анализаторы `tools/originals/analyze_{resources,animations,sounds,levels}.py`, Ghidra `AnnotateKremlin.java` и stage-specific скрипты. |
| **S2** | [native-state](../../originals/native-state.md), [world](../../originals/world.md), `research/evidence/stage6/{ru,en}/`; `analysis/originals/stage6-verification.json`, `worlds-{ru,en}.json`. `tools/ghidra/AnnotateWorld.java`, `tools/originals/analyze_worlds.py`. |
| **S3** | [rendering-geometry](../../originals/rendering-geometry.md), [rendering-assets](../../originals/rendering-assets.md), [rendering-reference](../../originals/rendering-reference.md). `research/evidence/rendering/{ru,en}/{roles.tsv,evidence.asm,analysis-manifest.json}`, `render-reference/`, `reference/{ru-03,en-02}/`. `tools/ghidra/InspectRendering.java`, `analyze-rendering.ps1`; `analysis/gba/{projection-probe,render-layout-probe,render-reference}.json`. |
| **S4** | [enemy-tables](../../originals/enemy-tables.md), [enemy-parameters](../../originals/enemy-parameters.md), [weapon-parameters](../../originals/weapon-parameters.md), [LEVEL comparison](../../originals/level-version-comparison.md), [принятый профиль NDS](../plan.md). `research/evidence/{enemy-fields,weapon-fields}/{ru,en}/`, `analysis/originals/{balance-tables,level-version-comparison,level-map-differences}.json`; `tools/originals/compare_level_versions.py`. `ports/gba/scripts/pack_gba_assets.py::apply_profile` проверяется только как источник чисел принятого профиля. |
| **S5** | [time-events](../../gba/time-events.md), [original-cadence](../../originals/original-cadence.md). `research/evidence/time/{ru,en}/`, `cadence/ru/`; `analysis/gba/g06-time-evidence.json`. `tools/ghidra/{InspectTime,InspectCadence}.java`, `analyze-{time,cadence}.ps1`, `ports/gba/scripts/verify_time_evidence.py`. |
| **S6** | [movement](../../gba/movement.md), [collision](../../gba/collision.md), [doors-triggers](../../gba/doors-triggers.md). `research/evidence/{movement,collision,interactions}/{ru,en}/`; `analysis/gba/g07-movement-evidence.json`, `g08-collision-evidence.json`, `g09-original-verification.json`. `tools/ghidra/Inspect{Movement,Collision,Interactions}.java`; corresponding `analyze-*.ps1` и `ports/gba/scripts/verify_*_original.py` (для движения — отдельные evidence verifiers). |
| **S7** | [surfaces-lighting](../../gba/surfaces-lighting.md), [sprite-rendering](../../gba/sprite-rendering.md), [dark-distance](../../gba/gba-dark-distance.md). `research/evidence/{surfaces,sprites}/{ru,en}/`; `analysis/gba/g11-original-verification.json`, `g10-review.json`. `tools/ghidra/Inspect{Surfaces,Sprites}.java`, `analyze-{surfaces,sprites}.ps1`; `ports/gba/scripts/verify_{surfaces,sprites_original}.py`. Dark-distance audit описывает подготовленный GBA-пакет и не является новой проверкой EXE. |
| **S8** | [DoorMirror](../../gba/ui01-playtest2.md), позднее дополнение [rendering-geometry](../../originals/rendering-geometry.md). Постоянная копия `research/evidence/nds/n01/door-mirror/{ru,en}/evidence.asm`, журналы, исторические manifests и `preservation.json` доступны; **center.bin отсутствует**. `tools/ghidra/InspectDoorMirror.java`, `tools/originals/analyze_door_mirror.py`, `ports/gba/scripts/verify_door_mirror_original.py`; воспроизведение ограничено путём export и отсутствием binary, см. выше. |
| **S9** | [items-stats](../../gba/items-stats.md), [combat](../../gba/combat.md), [WASDify ограничения](../../originals/wasdify-findings.md). `research/evidence/{items,combat}/{ru,en}/`; `analysis/gba/g12-item-inventory.json`, `g13-original-verification.json`. `tools/ghidra/Inspect{Items,Combat}.java`, `ports/gba/scripts/verify_{items,combat}_original.py`. |
| **S10** | [ai-movement](../../gba/ai-movement.md), [ai-combat](../../gba/ai-combat.md); `research/evidence/ai/{ru,en}/`; `analysis/gba/g14-original-verification.json`, `g15-original-verification.json`. `tools/ghidra/InspectAI.java`, `ports/gba/scripts/verify_{ai,ai_combat}_original.py`. |
| **S11** | [gba-audio](../../gba/gba-audio.md), S1 sound. `research/evidence/audio/{ru,en}/`; `analysis/gba/g16-original-verification.json`, `g16-listening.json`; `analysis/originals/sounds-comparison.json`. `tools/ghidra/InspectAudio.java`, `ports/gba/scripts/verify_audio_original.py`. |
| **S12** | [ui-localization](../../gba/ui-localization.md), [HUD](../../gba/ui-hud-refresh.md), [читаемые уведомления](../../gba/ui01-readable-notifications.md). `research/evidence/ui/{ru,en}/`; `tools/ghidra/InspectUI.java`, `ports/gba/scripts/verify_ui_original.py`. UI-сокращения отделяются от исходных строк. |
| **S13** | [DOS map](../../gba/ui01-dos-map.md), S2 world. `research/evidence/automap/{ru,en}/automap.asm`, `execution.json`, `arrows.json`; `analysis/gba/ui01-dos-map.json`. `tools/ghidra/InspectAutomap.java`, `tools/originals/analyze_automap.py`, `ports/gba/scripts/verify_automap_original.py`; свежий export требует поправки scriptPath. |
| **S14** | [game-flow](../../gba/game-flow.md), [frontend](../../gba/gba-frontend.md), [story](../../gba/ui01-story.md). `research/evidence/flow/{ru,en}/`; `analysis/gba/g18-original-flow.json`, `g18-exit-audit.json`. `tools/ghidra/InspectFlow.java`, `analyze-flow.ps1`; `ports/gba/scripts/{verify_flow_original,audit_flow_exits,verify_frontend_original}.py`. |

## Обязательные семантические границы

### Scheduler и архитектурный порядок

ASM обеих версий описывает кольцо owners и исходную ёмкость `capacity-1`
(при capacity4 доступны три события), порядок вставки/обхода и unsigned
deadline. `dispatch(0)` вызывается перед scene build. Просроченные периоды не
догоняются: новый deadline ставится от времени **после callback**. Удаление
первого события, compaction/source-slot update, first-slot interval и уменьшение
owner-count во время sweep имеют значимые исходные особенности.

416 native ticks относится к конкретному callback героя; шкала
`1193182/119` ticks/s не является частотой всей логики. GBA fixed grid100,
catch-up и безопасная pending-owner публикация — отдельные решения. **N03**
должен оставить ядру возможность выразить эти события и render-side effects,
а **N08** принять часы/dispatch/pause/lifecycle до N09–N17. Новая архитектура
не обязана хранить native 16-byte записи и DOS callback pointers.

### Карта: draw доказан, discovery остаётся открытым

Из исходного draw проверены шаг3, обращённая X, цвета byte marker, GAMEP,
контуры с исследованной стороны, классы и указатель героя. Map-item разрешает
часть предметов/живых врагов, не открывая неизвестные клетки. x86 fixtures
проверяли отрисовку при уже заданной visited, а не происхождение visited.
Неинициализированный ранний указатель героя в native draw исправлялся явно.

**N20** должен восстановить записи visited, их связь с render records и
условиями видимости. GBA-веер не заменяет это исследование. **N03/N07** должны
предусмотреть доступ к нужным результатам видимости, не выдавая чистый
`SceneSnapshot` за доказанную границу, если оригинальный draw меняет мир.
Touch/масштабы/постоянный нижний экран и работающий мир — NDS-адаптация;
GBA-пауза при карте ей не соответствует.

### LIGHT и вода

Индекс texel → LIGHT row → mapped index → VGA6 palette → DS RGB555 — отдельные
этапы. У вертикальных прозрачных ветвей ноль проверяется **после LIGHT**;
opaque и горизонтальные ветви пишут также нули. LIGHT15 не тождественно чёрная.
near=1920 наблюдался в стартовых RU/EN LEVEL1; формулы SAR16, signed16 SUB,
signed compare, IDIV120 и clamp15 подтверждены сохранённым ASM/x86-срезом.
Это не доказательство отсутствия динамического изменения near/палитры.

Для воды найдены циклические Sprite flags0408/0808/1008/2008 и фазы0…17;
деление размера на18 предшествует умножению на phase. Скорость задаётся
событиями/анимацией, не номером отрисованного кадра. **N12/N13** восстанавливают
её с исходными интервалами. Маска4000 с неинициализированным stack delta не
найдена в исходных Sprite-директивах и не должна становиться придуманным
эффектом. **N13** принимает поддерживаемые ветви и явные ограничения.

### Отражения

Восстановлено отражение **полотна двери** через аргумент constructor `BP+1C`.
Для theta0 его выбирает стена класса2 к западу, для theta90 — к северу.
Constructor переставляет экранные X/Y концов, сохраняя поля глубины; crop
затем сдвигает текстуру в её собственных координатах. Один glyph может иметь
несколько клеток-полотен при одном общем объекте/фазе. Это объясняет обратное
движение UV, но не доказывает отражение окружающего мира на воде или зеркале.

В доступных индексах/целевых исследованиях отдельный полный реестр эффекта
отражения мира не найден. Это **пробел N13**, а не вывод, что эффекта в игре
нет: нужно инвентаризировать flags, palette/raster и реальные DOS-сцены,
разделить существующие эффекты и гипотезы, назначить кадрам pose/phase.
Требование NDS-плана сохранять отражения нельзя закрывать DoorMirror-тестом.

### Flow, retry и общий мир RU/EN

Исходный путь — selection → BINTRO → новый мир → PLAY → EINTRO → selection;
ENDTXT относится к выходу. Category1000 имеет код проверки score==total,
но в шести исходных LEVEL нет category99-предметов и total=0. Реальные B2
выходы FCMap — 2/2/4 на LEVEL1/2/3 каждого языка; собирать очки ради выхода
не требуется. Old Gold не является образцом игрового цикла NDS.

При HP0 уменьшаются попытки, при ненулевом остатке восстанавливается native
диапазон pose `+26…+43` (**30 байт**) и HP=max. Это не пересоздание мира:
остальные actor поля, предметы/двери, inventory/keys/score, враги,
events/projectiles сохраняются. HP<0 — finish sentinel; signed16 lives
включает исходный DEC wrap. **N22** проверяет точную границу восстановления
в новых типах и cleanup нового мира, а не копирует весь compact GBA actor.

NDS намеренно использует RU геометрию и профиль N05 при обоих языках.
134/146 одинаковых RES и одинаковые sounds не отменяют исходных отличий
карт/текстов/POPAL. Имена и локализованное представление меняются отдельно
от мира. Игровые сохранения и импорт DOS/GBA не входят в архитектуру;
settings-only хранение принимает **N21**.

## Проверка этой матрицы и передача

Выполнены чтение обязательных документов N01, выборочные исходные спецификации
и адресные листинги, проверка 121 входной ссылки N03–N22 и наличия перечисленных
каталогов/отчётов. Все 20 будущих задач имеют источники либо конкретно
назначенный пробел. SDK, host/ARM9/GPU/audio реализации здесь не создавались;
физическая DS/DS Lite, NDS FPS, новый полный DOS-плейтест и новые x86-срезы
этим документом не проверены.

Приоритет следующих архитектурных решений: **N03** — границы/ownership/память,
**N04** — пакет и рабочий набор, **N06/N07** — проекция и реальный аппаратный
renderer, **N08** — время и порядок событий. Точечные GBA-оптимизации,
альтернативные банки и изменения draw distance не принимаются автоматически.
N09, N11/N13 и N20 должны закрыть перечисленные адресные/экспортные ограничения
перед использованием соответствующих новых доказательств.
