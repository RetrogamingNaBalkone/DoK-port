# Передача контекста между чатами

27 октября: крупные подробные JSON исключены из Git; их сводки —
`analysis/summaries/`, политика — `analysis/report-policy.json`, пояснение —
`analysis/README.md`. Полные локальные отчёты остались по прежним путям для
существующих скриптов. После нового запуска обновлять сводки
`scripts/summarize_reports.py`. Статусы игровых задач не менялись.

27 октября: хранение исследований вынесено в `research/`; после уточнения
пользователя промежуточные сборки и повторы удаляются. Паспорт и границы —
[tasks/RESEARCH-STORAGE.md](tasks/RESEARCH-STORAGE.md), итоговый учёт —
`analysis/storage-cleanup-20261027.json`. Старые отчёты могут ссылаться на
удалённые выходы; для повторных проверок нужна новая сборка. Игровые статусы
не меняются; текущая UI01 и необходимые контрольные образцы сохраняются.


## Текущая UI01: второй плейтест,27 октября2026

`artifacts/gba/DoK-port_UI01.gba`,16012100 bytes, SHA-256
`ca27ff500f799d2391eba1f8e4ed1550184a07bda2a38ca66a501d482e0a3a4a`.
ROM/ELF/map/PNG/WAV — `build/g20b-playtest2/`; repeat побайтно точен.
Паспорт [G20B](tasks/G20B.md), описание [ui01-playtest2.md](ui01-playtest2.md),
сводка `analysis/g20b-ui01-playtest2.json`. Дальше — G21; G20 budget открыт.

Текстовые экраны: x8,28 столбцов внутри рамки. Год одноцветный.
Двери: восстановлен native-признак отражения размещённого полотна при
соседней стене. Исходный crop сохранён.16 исполнений x86 и mGBA подтвердили
противоположное направление. Прежнее заявление об одинаковом сдвиге в DOS
неверно. Карта — Select, выход B/Select; Start+Select — пауза, включая
Select-first. MAP=7, часы остановлены, музыки меню нет. Существующая
visited-plane; компоновка и веер раскрытия — адаптации GBA.

Host/ARM/UI/flow/audio/time/water/sprites/latency/map прошли.294 transitions,
5008 callbacks,0 mixed/18 целых чёрных.136 из396 PPU совпадают,260 меняет
отражение центральных плоскостей. `--unreflected-control` отключает только
эту ветку в приватном C:396/396 exact с предыдущей UI01. EWRAM261436/free708,
IWRAM24424/free152 до8KiB stacks; main2584/margin5352, IRQ80/margin80.
Физическая GBA/оригинальный BIOS и полный DOS-плейтест не проверены.

## История: UI01 / G20B, первый плейтест27 октября2026

Итог — [gba-frontend.md](gba-frontend.md), паспорт — [G20B](tasks/G20B.md),
сводка — `analysis/g20b-ui01-feedback.json`. Следующая задача — **G21**, плейтест
`artifacts/gba/DoK-port_UI01.gba`. ROM16010864 bytes, SHA-256
`4c1b136e3abde3cbc67d9e30a1b9a86d1a41c3c88e39f16d44a50775bf69ad91`.
Выбранный renderer2775 сохранён; полный повтор в `build/g20b-fixes-repeat/`
побайтно точен. Контроль2775/96/72 и прежние ROM не перезаписаны.

Boot показывает исходную заставку240×160, A/Start открывает отдельное меню.
«Начать игру» → три исходных названия RU/EN → новый мир/BINTRO.
«Продолжить» только в паузе; после EINTRO открывается выбор уровней.
FON/МЕНЮ_ПАЛ/FONT8/FONT16 исходные. Музыка ЗАСТАВКА_ЗВУК повторяется без
рестарта между экранами меню; перед BINTRO/ENDTXT останавливается, в паузе
не запускается. Сохранений нет. Полные правила — в новом документе.
После пользовательского плейтеста «Показатели» удалены из обоих меню.
Копирайт NewCom перенесён исходными81×11 пикселями без уменьшения.
Короткие A/Start сохраняются VBlank IRQ; статический экран остаётся в VRAM.
16 фазовых случаев с однокадровыми импульсами прошли, обычные переходы
обновляют UI не позднее4 кадров, справка —5. Boot до первого UI-кадра —
10 VBlank/0,167с вместо404 VBlank/6,76с. Старые held-проверки потерю нажатий
не выявляли. Мир создаётся только после выбора уровня, выбранная RU/EN пара
проходит полную проверку профиля. Boot suites отделены от обычного запуска;
часы дополнительно проверены отдельной ARM-ROM `time-selftest`.

Свежие Ghidra exports/logs и manifests — `research/evidence/ui/{ru,en}/`, оба EXE SHA
до/после совпали. DOS boot VGA-индексы совпали с ЗАСТАВКА_СПР целиком;
запуски — `build/g20b/dos-{ru,en}/`. Native once/stop и адреса имён
отделены от согласованных адаптаций GBA.

Host/ARM UI, flow всех6 миров,23 PCM, два повтора музыки, time/sprites/water
и ARM vectors прошли.396 игровых PPU exact против2775. Исправлен старый
Pause/Resume: повторные записи affine reference посреди bitmap-вывода.
294 перехода/5005 callbacks без смешивания;18 полных чёрных кадров layout
transition учтены отдельно. PNG RU/EN просмотрены; WAV/ELF/map/отчёты
сохранены в `build/g20b-fixes/`. EWRAM261428/free716, IWRAM24376/free200 до8KiB stacks;
наблюдавшиеся main2584/margin5352, IRQ80/margin80.

Физическая GBA, полный DOS-плейтест и новое человеческое прослушивание
не заявляются. G20 budget открыт; G22/Performance ожидает G21.
Более ранние указания об ожидании G20B ниже заменены этим результатом.

27 октября выполнена [очистка экспериментального мусора](tasks/CLEANUP.md).
В `research/experiments/g20a-*` больше нет `.o`, кэш Zig удалён; ROM/ELF/map, отчёты,
журналы и снимки исходников сохранены. Базовые объекты `select-menu-fix`,
`level1-fixes`, `g20` и общие assets сохранены. Сборщики автоматически
удаляют собственные объекты; для новой базы/ручной перелинковки нужен
`--keep-objects`. Проверка упаковки удаляет временные повреждённые/повторные
пакеты. Отчёт: `analysis/artifact-cleanup-20261027.json`. Очередь ниже не менялась.

## Актуальный бэклог после оптимизационного исследования, 27 октября

По прямому запросу пользователя порядок: **[G20C](tasks/G20C.md) →
[G20B](tasks/G20B.md) → [G21](tasks/G21.md) → [G22](tasks/G22.md)**.

- **G20C готово:** [native sparse posts](gba-sparse-posts-trial.md) реализован
  и отклонён. Heavy5 RU/EN CPU+9,67–28,32%, spatial440 на каждом языке:
  медианы CPU+1,03–5,64%; turn120/walking120 на3 уровнях×2 языка медленнее.
  Все880 spatial,10 heavy,396 detail PPU и48 dynamic mutation пар exact;
  ARM4096 sparse/512 rectangle, sprites/interactions/AI-combat/UI и282 transitions
  прошли. Known Pause/Resume intermediate issue не закрыт; физической GBA нет.
  Контроль2775 и96/72 сохранены. Trial ROM7dfe60…99e94,16438720B,
  EWRAM261300/free844, IWRAM24544/free32 до8192 stacks;
  main2528/margin5408, IRQ80/margin80. Три ROM в `research/experiments/g20c-sparse-posts/`
  и `research/experiments/g20c-sparse-repeat/` byte exact; команды/границы — в новом документе,
  итог — `analysis/g20c-sparse-posts.json`. Native отказ относится к этой
  реализации; новый перебор kernels в G20C не требуется.
- **G20B готово:** оригинальная заставка/музыка и главное меню; результат выше.
  Использован контроль2775, sparse-путь не включён.
- G21 готова к началу: плейтестинг и регрессия текущей сборки, воспроизводимая
  сборка/инструкция. Открытый бюджет G20 не блокирует плейтест и не считается
  выполненным; выпуск требует явного решения о качестве и ограничениях.
- G22 после UI и плейтестинга: спроектировать, реализовать и отдельно проверить
  Performance-релиз общего движка с сохранённым геймплеем, цель30FPS.

Первоначальная постановка очереди была только документацией; последующий
проход G20C уже реализован и проверен, как указано выше.
G20A остаётся завершённым исследованием; G20 budget открыт, Performance нет.
Ниже сохранена история; её старые указания следующего шага заменяет эта очередь.

## Итог текущего goal,27 октября

**Начинать с [итогового handoff](gba-optimization-handoff.md)** и
`analysis/g20a-final-handoff.json`. Текущий goal закончен в уточнённой
пользователем границе: архитектурное сравнение, разумная оптимизация основной
ветки, замеры и анализ остатка. **Performance-сборка не реализована.**
G20A готова как анализ/передача этого этапа; G20 открыта по бюджету,
После последующей G20C задача G20B готова к началу; G21 ожидает UI.
Численные цели15+/24,1/30 универсально не достигнуты.

Последний wide affine/PC shear trial проверен и не выбран:58 native poses,
46 GPU-active,8 nonzero-PC,6 Mode4 cycles,18 weapon states/full PPU/source/clip.
Все5 heavy views уходят вCPU fallback, CPU2,10–2,38×2775. Pressure diagnostic
показал реальное исчерпание PPU в4 видах; пятый не проходит fit bound.
Loss силуэта не принят как minor. Плановые крупные сравнения закрыты,
основной путь выбран2775, **не объявлен абсолютный предел GBA**.

Свежая сборка `research/experiments/g20a-final-costs/hw_v128_entity_rect/` byte exact к2775;
игровые128/96/72 artifacts сохранены. Новый диагностический128 профиль
разделяет masked/opaque/потолки: L1 дороги пониженные плоскости, L2 masked
грани. На завершении G20A sparse posts были проверены только offline;
последующая G20C отдельно реализовала native путь и получила отрицательный
результат без изменения принятого2775. Подробные оставшиеся расходы, measured quality
варианты и ограничения — в итоговом документе. AI/draw distance не менялись.

Ниже — история контрольных точек. Их «следующий шаг» и «goal открыт» относятся
к моменту записи; актуальный результат и граница указаны выше.

## Уточнённая граница активного goal

Прямое указание пользователя27 октября: goal **не включает реализацию
Performance-сборки**. Он заканчивается выбором/разумной оптимизацией основной
архитектуры, итоговыми замерами и handoff с анализом оставшихся расходов и
возможных графических сокращений. Будущий Performance сохраняет геймплей;
AI radius автоматически не менять. Цели15+/24,1/30 сохраняются как критерии
оценки достигнутого результата, но не повод скрыть недостижение или бесконечно
продлевать доводку. Native OBJ hybrid и два sector clip/span backend проверены
отрицательно; диагональный sector-fed GPU checkpoint проверен и не выбран,
широкие affine/shear фрагменты ещё требуют отдельного решения;
текущий этап открыт. Подробности в `gba-architecture-selection.md`.

## Последний опыт: sector-fed Mode2 GPU checkpoint

Читать [GPU checkpoint](gba-sector-gpu-checkpoint.md),
`analysis/g20a-sector-gpu-checkpoint.json`. Native backend: CPU tile foreground
на affine BG2/PA128/PD228, исходные базовые planes на BG3, clipped source-mip
OBJ dynamics, HBlank OAM multiplex выключен, DISPCNT1c42/1210-cycle OBJ budget.
Ближние стены/уступы/низкие потолки пока CPU. Это не full-GPU static renderer.
Исходные16 LIGHT rows применяются ARM source-copy; unshaded mip bank1 912 576B.
All-prelit mip bank135,6MB был отвергнут до ROM. Whole flat billboards plus
узкие perspective strips; PA/PD, PC/PB0. Per-face clip вычисляется один раз;
fully hidden faces отсекаются до GPU setup, без old alpha/coverage prepass.

`research/experiments/g20a-sector-gpu-v8/`, repeat `research/experiments/g20a-sector-gpu-repeat/`:
`hw_v128_sector_gpu_thumb`, ROMb2b16b…ca3d6e,18 754 460B;
`hw_v128_sector_gpu_off`, ROMfe51e2…52adcc,16 365 548B. Все3 ROM с2775
byte exact в обоих деревьях, artifacts2775/96/72 сохранены. Ранние drafts
physical/logical X/admission не использовать. Source/current hashes проверены
collector `scripts/report_gba_sector_gpu.py`.

58 native poses RU/EN,6 Mode4 cycles с активным GPU,18 weapon states:
dynamic geometry/leases и native HUD/weapon exact против2775.46 поз реально
рисуют OBJ. Для GPU и CPU Mode2 по2 227 200 actual PPU pixels/source base pixels
прошли; GPU ещё56 444 source texels и70 446 candidate pixels по static clip
contract. Это не independent dynamic completeness proof. Alpha mask comparison
с Mode2 CPU: max278 missing/96 extra physical pixels; послеradius2 max62 missing
interior/2 extra beyond tolerance. Loss картинки измерен, не принят как minor.
Нет new map edits/full440/full gameplay/physical GBA acceptance.

**Не выбран.** Все5 heavy views отклоняют GPU admission: scan budget у4,
atlas уL2/2871. Затем CPU dynamic fallback; это не successful-GPU heavy FPS.
CPU ratios2,29/2,06/2,52/2,60/3,12, FPS4,98/5,43/4,27/3,98/5,43.
Short live24 RU/EN10,70/8,33/12,91; CPU Mode2 11,03/10,24/14,63;
2775 19,37/19,11/27,57. PCM underruns0, time-conservation error0, debt retained.
Held profile без forced PCM: attempted GPU setup0,792–1,274M cycles.
EWRAM261324/free820, IWRAM24196/free380 до8192 stacks; main2544/margin5392,
IRQ80/margin80. GPU state19640 plus sector13496 вcovered[], fresh каждый draw.

**Следующий ограниченный крупный опыт:** широкие perspective fragments с
affine PC shear и измеримым ограничением ошибки, вместо текущих узких
диагональных полос. Это меняет OAM/canvas pressure и аппроксимацию; текущий
checkpoint ещё не отвергает всё семейство sector-fed GPU. Оценить также
оставшийся бюджет для static output, затем выбрать основную архитектуру и
закончить goal итоговым handoff оставшихся расходов. Локально не доводить
диагональные kernels, не строить Performance ROM, не менять AI/draw distance.
Goal/G20/G20A активны. Следующие разделы — предыдущие этапы.

## Предыдущий опыт: sector clip/span и прямой Mode4 bitmap

Читать [sector trial](gba-sector-clip-trial.md), `analysis/g20a-sector-trial.json`.
Оба варианта реализованы и проверены, но не выбраны.
`research/experiments/g20a-sector-clip-final/`, измерения в `research/experiments/g20a-sector-clip-v4/`,
repeat `research/experiments/g20a-sector-clip-repeat/`. ROM8d46e3…ae3ba и0cd601…ff2dc4;
все три дерева byte exact, контроль2775 exact. Sector+HW: CPU1,37–1,87×,
heavy5,69–11,95FPS; sector+bitmap:1,86–2,35×,4,59–8,53FPS. Short live24 RU/EN
11,65/11,20/17,07 и7,20/7,43/8,74 против контрольных19,37/19,11/27,57.
Низкие потолки/свет/исходные leases/AI сохранены.

По40 native поз/6 Mode4 cycles/12 map edits на вариант. Original dynamic
geometry/leases, независимые wall-ray/roof-mask checks,27992 roof pixels,
97590 dynamic source/alpha/order pixels прошли; static clip contract не имеет
отдельного полного dynamic visibility oracle. Исключения посчитаны отдельно:
6 endpoint columns,10188 world-grid rows,1102 adjacent roof rows и4 opaque
edge rows. Bitmap:202580 software base pixels и1 536 000 actual
Mode4/OBJ PPU pixels прошли.18 PPU overlays гибрида exact, scene max17314RGB
changes. Нет full440/full gameplay/physical acceptance новых вариантов.
У bitmap V2 были PCM underruns; V3/final с IRQ service window проходит heavy/live
с underruns0. RAM/стеки и ограничения подробно в новом документе.

**Не доводить локально эти два проигравших пути.** Кэш exact-camera картинки
не выбран как главная архитектура по audit9×120: при движении1–6 возможностей
reuse/120, при стоянии120. Это не native cache и не reprojection benchmark.
Главная оставшаяся проверка — pure/sector-fed GPU output, который не опровергнут
прежним CPU+OBJ coverage hybrid. После его обоснованного disposition — выбор
основной ветки, ограниченная доводка, итоговые замеры и handoff оставшихся
расходов. **Performance ROM не делать.** G20/G20A/goal активны;
2775/96/72 artifacts сохранены. Следующие записи — предыдущие этапы.

## Предыдущий опыт: native world OBJ hybrid

Читать [native trial](gba-world-obj-native-trial.md),
`analysis/g20a-world-obj-native.json`. `hw_v128_world_obj`, ROM807f3e…3916c,
`research/experiments/g20a-world-obj-native-final/`; чистый repeat `...native-repeat/` exact
для native,2775 и probe.128/96/72 artifacts сохранены, нового default нет.
Исходные16×32 texture strips/16 LIGHT rows, перспективные стены и static masked
objects; CPU сохраняет потолки и fallback.32 slots×512B в16KiB, HBlank CPU
slots4..7/matrix0, world slots8+/matrices2+, matrix1 reserved под DMA padding.
DISPCNT3561h, законный bit5/954 scanline budget;8-line conservative admission.

40 poses RU/EN с6 Pause/Mode4/Resume cycles: original projected geometry/leases
exact, row maps/VRAM residency/coverage/independent PPU composition passed.
1 091 448 foreground pixels проверено,36 308 непосредственно из OBJ; максимум
scanline bound938/954.18 PPU overlays exact, max449 scene RGB changes.
5 heavy PCM CPU ratios2,67/1,49/2,16/4,41/2,54, FPS4,27/7,47/4,98/2,49/6,64.
Short live24 RU/EN12,36/8,33/11,20 против2775 19,37/19,11/27,57. Underruns0,
time-conservation error0; retained debt до406 не budget pass. Нового440/full
gameplay acceptance нет. Профиль без forced PCM: setup/admission0,37–3,46M,
opaque coverage0–0,083M, masked0,14–0,96M. Warm texture upload0B во всех5 видах.

V1–V8 **не использовать для решения**: Mode4 перезаписывал часть row maps в
covered[] без изменения magic. Теперь non-native draw сбрасывает owner pointer;
следующий native полностью создаёт state. Вся first/last table проверяется
в каждой QA позе. Ранее asset opacity пропускал definition.sprite_flags;
финал учитывает их, ambiguous resources получают fallback. МакросHW_WORLD_OBJ,
файлы `renderer_world_obj.inc`, `gba_world_obj_assets.py`, `hardware_world_obj.inc`,
`include/dok/gba_world_obj.h`. `g_world_obj_work` счётчики, `...ticks` optional
g_render_work profile, `...trace` отключённый по умолчанию pixel owner probe.
EWRAM261424/free720, IWRAM24072/free504 до8192stack; main2528/margin5408,
IRQ80/margin80. State зарезервирован в4KiB tail covered[], static cache урезан
перед ним. Guards/linker/повторяемость прошли в scope, physical GBA нет.

**Не доводить этот проигравший гибрид локальными kernels. Следующая крупная
проверка — genuine sector top/bottom clip + shared planes/spans**, с оценкой
software bitmap и сохранения аппаратных базовых плоскостей. Чистый GPU или
GPU output из секторного clip этим гибридом не опровергнуты: здесь дорого
связывать старый CPU raster и OBJ через coverage. Image cache остаётся запасным
кандидатом. Goal заканчивается итоговым архитектурным handoff, не Performance ROM.

Source-art probe отдельно перепроверен:54/56 exact с фактическим DISPCNT bit5,
2 non-multiplex pressure failures по100pixels остаются отрицательными.
ROMc1903e…8a09, `research/experiments/g20a-world-obj-bringup-final/`,
`analysis/g20a-world-obj-bringup.json`. V4/c83cea без bit5 заменён. Стенд с
IRQ/PCM hold не использовать как FPS evidence; native trial выше работает с PCM.

## Предыдущий опыт: полный колоночный foreground

Читать [gba-column-pipeline-trial.md](gba-column-pipeline-trial.md) и
`analysis/g20a-columns-trial.json`. `hw_v128_columns` V6, ROMe9bae2…04b22,
`research/experiments/g20a-columns-v6/`, repeat `research/experiments/g20a-columns-repeat-final/` exact.
Не выбран: пять тяжёлых PCM видов CPU3,55–4,31× контроля,2,71–4,12FPS,
short live24 RU/EN5,08/4,86/5,67. Нового sweep440/full gameplay acceptance нет.
36 poses/108 column snapshots/13752 rows passed (72 grid-boundary rows excluded),
все dynamic candidate geometry/visibility leases совпали с2775, nearest owner,
alpha/vertical/ceiling pixels и independent segment-ray/world-cell oracle прошли.
18 PPU overlays exact, coverage2/18 exact, max17298 scene RGB changes вследствие
world UV/raster differences; physical/moving visual acceptance не заявлена.
PCM underruns0, live time-conservation error0; retained motion debt не budget pass.
Профиль без forced PCM; per-row visibility доминирует. EWRAM261280/free864,
IWRAM20976/free3600 до8192stack reserve; main2528/margin5408.

V1–V5 superseded: depth проверяется по реальным концам, после freeze нужен
cold world reset, V6 убирает6400 cap и сохраняет исходный envelope. Harness
`benchmark_gba_hardware.py` теперь также cold-reset до frozen capture. Последний
verifier проверяет и ceiling pixels; source SHA совпадает с visibility report.
Коллектор `scripts/report_gba_columns.py` завершён. Контроль2775, quality96/72 и
streaming BSPbfdba7…25184 сохранены/повторены. Оригиналы не менялись.

Не доводить этот DDA локальными kernels. Следующие главные проверки — прямые
аппаратные world OBJ и настоящий sector top/bottom clip + planes/spans с оценкой
bitmap против аппаратных базовых плоскостей. DDA nearest-row-owner не закрывает
проверку Doom подхода. Image cache при движении — третий, менее приоритетный
кандидат. Draw distance/AI не менялись; дальность в темноте проверять отдельно,
AI rebalance отдельным gameplay profile. G20/G20A/goal открыты.

## Последняя native архитектурная контрольная точка

Читать [gba-stream-bsp-trial.md](gba-stream-bsp-trial.md),
`analysis/g20a-stream-bsp-trial.json`. Native hybrid streaming segment BSP
реализован в `src/renderer_stream*.inc`, packed tables в `scripts/gba_stream_scene.py`.
Сборка `research/experiments/g20a-stream-bsp-v3/hw_v128_stream_bsp/`, ROMbfdba7…25184,
чистое повторение `research/experiments/g20a-stream-bsp-repeat/` exact. Контроль128 byte-exact2775;
2775/96/72 artifacts неизменны. **Вариант не выбран, новых artifacts нет.**

Spatial440: median CPU ratio1,507/1,573/1,361;0 быстрее/424 медленнее/16 равны
по FPS. Min7,96/6,64/11,95 против9,95/9,95/14,93. Short live24 RU/EN:
13,40/12,15/19,11 против19,37/19,11/27,57. Это не turn120/walk120.
18 PPU coverage/overlay exact, max272 scene pixels changed;48 native fragment
ownership/alignment/visibility leases/TMap fallback/guards fixtures прошли.
Full gameplay/water/AI suites, moving transition acceptance не заявлены.
EWRAM261296/free848; IWRAM23336/free1240 до8192stack; main2528/margin5408.

V1/V2 имели невыравненный XY overlay covered[] и **не используются** для выводов.
V3 выравнивает scratch; новый verifier проверяет alignment и сохранение каждой
динамической экранной колонки. Existing rectangle/map comparison helpers
переиспользованы; новые ASM kernels не писались. Статика идёт near-to-far без
общей сортировки, но coverage samplers и cell ceilings прежние. Поэтому это
не отрицательная проверка полного sector/column/visplane renderer.
**Следующее действие: полноценное представление видимых колонок/spans, затем
direct world OBJ. Не доводить этот BSP/quad путь локальными kernels/порогами.**

Новые уточнения: [gba-dark-distance.md](gba-dark-distance.md),
`analysis/g20a-dark-distance-audit.json`. Почти чёрные различия могут быть
незаметны на маленьком экране; оценивать240×160 и движение, а не только RGB diff.
Проверено в actual GBA package: LIGHT15 starts3720, native static-art максимум
3/2/2 из31 по мирам. BG base planes ужеcap3600. Graphics-only draw distance
с прежними AI/leases — отдельный quality axis; пока не реализован.
Идею меньшего wake radius рассмотреть отдельным balance prototype:0xBA только
initial wake поChebyshev metric, уже активных не останавливает; combat wake
обходит радиус. Не менятьAI автоматически. Goal/G20/G20A остаются открыты.

## Актуальное управление работой: сначала архитектура

Прямое уточнение пользователя27 октября: не уходить в детальные правки,
пока не проверены крупные варианты; цель отредактирована соответственно.
**Этот порядок заменяет прежние указания «следующий sampler/пороговый опыт».**
Читать [gba-architecture-selection.md](gba-architecture-selection.md) и
`analysis/g20a-architecture-checkpoint.json`. Batch/visible split/alpha-empty/
feedback сохранены как опыты, но дальнейшая их доводка заморожена. Feedback v3
spatial44028 быстрее/36 медленнее против96; общего выигрыша нет, не выбран.
Ни новый artifact, ни release default не назначались;2775/96/72 сохранены.

Начат отдельный `scripts/compile_gba_render_scene.py`, вход SHA3efa5d…674351.
`research/experiments/g20a-scene-compiler-v1/`: шесть представлений статических отрезков и
floor/ceiling rectangles.1998/3242/1364 исходных отрезка →649/1148/255 групп;
segment BSP564/853/302 nodes, глубина16/11/10. Сохранены исходные UV pieces,
все lowered rims, высоты и материалы; проверено полное покрытие данных.
**Runtime renderer/visibility/FPS/изменяемая карта этим не проверены.**
Следующее действие — native прототип крупной архитектуры: секторный segment
BSP/span pipeline (software direct framebuffer и hybrid affine BG), затем
полный layered column pipeline и прямой hardware OBJ/strips; gate в документе.
Не считать прежние grid-BSP/ray addons проверкой новых конвейеров. Картинку,
двери, actors и низкие потолки сохранить в representative prototype.

GBADoom primary README:≈35 только простые места,≈10 сложные; distance lighting
убран, mipmapping. Его clip/span pipeline отличается от нашего; нижние потолки
сами по себе не аргумент за неизбежную медленность DoK. Источники и границы
вывода в документе. Чужие35FPS здесь не воспроизведены. Goal/G20/G20A открыты.

## 96/72 строки CPU-слоя — последний законченный эксперимент

Читать [gba-row-quality-trial.md](gba-row-quality-trial.md),
`analysis/g20a-row-quality-trial.json`. Artifacts rows96/rows72:
97f0b3…57f81 /176969…8888a. Оба чисто повторены;2775 и оригиналы сохранены.
96 — умеренный quality candidate,72 — только сравнение; default не менялся.
Turn12096:21,20/18,24/23,12; walking12096:19,85/20,13/22,61.
Spatial440 min96:11,9455/11,9455/19,9092;72:13,7833/11,9455/19,9092.
Goal/G20/G20A открыты.396 overlays exact/ARM/row-color/UI/flow/sprites/water/
AI/28combat/items/combat/time/interactions прошли в scope; G09 native pixel
oracle выключен.282 held transitions прошли;294 Pause/Resume остаётся открытым.
96 all-awake stress0,07–0,38FPS, это не приемлемый бюджет.

96 использует WIN0 для clipping OBJ к144 строкам; mapping учитывает OAM overlap.
Скрипты используют `scene_source_y` вместо только128/144. EWRAM261292;
IWRAM24540 у96 (free36 до8192 stacks),24364 у72. Main2520/margin5416,
IRQ80/margin80. Производственные ROM в `research/experiments/g20a-hw-row-quality-v3/`;
72 spatial/396/water/ARM/transitions изv2 валидны по тому же SHA.
Пути входных reports перечислены сборщиком. Adaptive grid4 отложен:15 быстрее/
62 медленнее из440. Простому opaque64×64 OBJ пути не подошла ни одна грань
в17 видах: wall source96×192, прочие в основном masked; сложный OBJ backend
не отвергнут. Новый96 профиль показывает L2 vertical0,66–0,69M из1,33–1,37M.
Следующий узкий участок — sampling/visibility вертикальных граней.
Per-primitive profiling теперь берёт отдельный completed held draw после FPS
samples, чтобы не читать counters следующего кадра; ticks×64=cycles.

## Аудит железа и адаптивный потолок — текущая контрольная точка

Читать [gba-hardware-audit.md](gba-hardware-audit.md) и
[gba-adaptive-ceiling-checkpoint.md](gba-adaptive-ceiling-checkpoint.md).
Нельзя считать, что аппаратные/архитектурные возможности GBA исчерпаны:
прямые OBJ для игровых объектов/подходящих стен ещё не сравнивались. Voxel
heightmap не решает автоматически интерьеры с нависающими потолками.

Новый v2 ROMeacf37…e55c в `research/experiments/g20a-hw-adaptive-v2/` не выбран и не опубликован
в artifacts. Spatial440:26 быстрее/66 медленнее/348 равны по FPS, минимумы
11,9455/9,9546/14,9319.397 Mode1 PPU exact,43 Mode2 имеют отличия;18 обычных RGB
exact. Короткий live24:18,145/18,145/27,567 RU/EN, не turn120/walk120.
Нет полного regression/стеков/переключений нового backend. Water harness ещё
требует поддержки compact single-frame внутри dual slot. Допустим один дешёвый
estimator trial; если общего выигрыша нет, отложить адаптивную ветку.
Основной кандидат2775 остаётся прежним; G20/G20A/goal открыты.

## Аппаратный низкий потолок — следующий исследовательский проход

Читать [gba-lowered-ceiling-trial.md](gba-lowered-ceiling-trial.md),
`analysis/g20a-lowered-ceiling-trial.json`. Эксперимент4f96b7…44d80,
`research/experiments/g20a-hw-lowered-v6/hw_v128_lowered/`,16 567 124B; чистое повторение
`research/experiments/g20a-hw-lowered-final/`. **Не выбран и не копировался в artifacts**;
рекомендуемый ROM остаётся2775. Цель/G20/G20A открыты.

Mode2 BG3 рисует source-derived32×32 потолок на исходной высоте−561/inset400,
CPU публикует прежнюю coverage без UV/LIGHT. UI/weapon native normal OBJs,
сцена мультиплексируется в4 OBJs HBlank DMA3. DMA3 immediate config атомарна
относительно VBlank IRQ; UI-only396 exact. Дубликаты near/far BG2 maps заняты
низким потолком: screen20 near/far,26 mid; BG2 far теперь читает near map с
другим charblock. Карт по-прежнему6, нет отдельного дорогого low-map upload.
Два dual frame11592B занимают covered[] tail; отдельный front5796B удерживает
GPU при подготовке Mode4, чтобы scratch reuse не портил старый видимый кадр.
EWRAM261276/free868, IWRAM23296/free1280 до8192stacks; full stress ещё нет.

В L1-cell520/3640 CPU−21,1/−31,1%; spatial440 minima11,9455/9,9546/14,9319.
Но live turn12017,57/15,03/19,69 и walking12015,96/16,90/19,27 RU/EN хуже2775.
Поэтому постоянный Mode2 не продвигается. Следующая обоснованная проверка —
выбор пути по стоимости видимого низкого потолка, с быстрым Mode1 для остальных
видов. Материалы обоих путей могут оставаться резидентными благодаря reuse maps;
сам adaptive switch пока не реализован. L2 всё равно требует ускорения vertical raster.

396 PPU: max1009 changed background pixels; foreground/overlay0, coverage exact.
ARM1024 coverage-span vectors и прежние vectors прошли; UI/flow/sprites/144water
прошли.282 held transitions5134callbacks mixed0/overflow0. Полная AI/combat/items/
clock/stress приёмка нового backend открыта. Более строгий294 Pause/Resume test
**failed также на2775**, counts переходных кадров после исправления front lifetime
совпали: RU Pause3/Resume2; EN L1 1/1, EN L2/L3 3/2. Не выдавать его заpassed.
Original22SHA unchanged; source control7bab. Surface clip prepass и OAM RAM
table не окупились, отложены. Схема памяти и точные размеры приведены в
документе и header `dok/gba_hw_scratch.h`.

Критерии остановки уточнены в `gba-render-costs.md`:30; либо устойчивые15+
при несоразмерной цене дальнейшего ускорения; либо зафиксировать недостижение
после исчерпания обоснованных направлений в допустимом качестве и вынести
конкретный выбор уступок. Отдельные ветки без заметной экономии полного кадра
или перехода черезdeadline откладывать. Наличие идей не является основанием
для бесконечного продолжения.

## Кэш и текущая стоимость кадра — продолжение27 октября

Последний измеренный результат — [gba-render-costs.md](gba-render-costs.md),
`analysis/g20a-hardware-cache.json`. Новый отдельный
`artifacts/gba/DoK-port_hardware_cached_prototype.gba`, SHA2775bd…edaab,
15 938 936B. Build/QA `research/experiments/g20a-hw-entity-rect-v2/hw_v128_entity_rect/`,
чистое повторение `research/experiments/g20a-hw-entity-final/`, source control7bab…7ea8.
Предыдущие опубликованные ROM сохранены. Ни SDL2, ни оригиналы не менялись.

Кэш статической геометрии использует свободный legacy interval payload;
двери/актёры не замораживаются. Direct gaps, ненулевые столбцы, prelit16 для
декораций, masked tile batches и точные постоянные прямоугольники ускорили
отрисовку без новых потерь относительно прежнего128.396+440 PPU кадров exact;
48 cache mutation cases,512 rectangle ARM vectors и полные hardware suites
прошли;282 transitions/4811 callbacks безmixed. Исправлена синхронизация
самого transition harness: дождаться предыдущего действия до следующей мутации;
методика проверена и на e803. Все callbacks после мутации проверяются.
EWRAM261292/free852, IWRAM24408/free168 до8192 stacks; main2528/IRQ80.

Runtime turn120 RU/EN19,85/16,86/21,79; walking12017,96/17,92/20,54 RU,
EN L1 18,10; стояние/огонь19,91/19,91/29,86. Но новый sweep440 пространственно
распределённых неподвижных видов даёт minimum9,954583/9,954583/14,931875
без live AI. В тех же440 видах e803 —7,465938/6,636389/11,945500; PPU exact.
Худшие клетки проверены на связность с исходным стартом при открываемых дверях.
Стартовый72-heading sweep18,86/17,92/19,91 более благоприятен и не заменяет440.
**Goal/G20/G20A открыты**, universal15+,24,1/30 не достигнуты. Artificial
all-awake stress сохраняет тяжёлый долг; guard/audio pass не является FPS pass.

Текущая цена: короткий живой L2 raster~54%, geometry~25%; в тяжёлых frozen
видах raster71–81%. Низкие потолки ещё поклеточные CPU polygons; в L1 могут
стоить половины кадра, но в тяжёлом L3 CPU-потолков вообще нет. Следующий
приоритет — общие настройки строк/объединение пониженных потолков и удешевление
вертикальных граней. BSP reuse/крупные листья/Thumb всё ещё медленнее обычного
обхода; ROM column cache/zero-span bitmap/дополнительные div и affine-U не выбраны.
Procedural/сокращение LIGHT не продвигать без нового измеримого основания.

Уточнение пользователя к профилям ROM: Fidelity бессмыслен для игрока и
исключён из предлагаемых игровых версий. Точный программный вариант — только
внутренний эталон регрессий. Текущая аппаратная линия — Balanced; Performance
с целью 30 FPS пока предложение для эксперимента, а не достигнутый результат.

27 октября: по вопросу пользователя подготовлена
[оценка нескольких GBA ROM для разных FPS](gba-rom-profiles-assessment.md).
Предложены профили общего движка и отдельный эксперимент Performance;
достижимость 30 FPS не доказана. Новых сборок/замеров и выбора release quality
нет; G20/G20A остаются открытыми.

## Быстрый128 — последний результат27 октября

Читать [gba-hardware-fast.md](gba-hardware-fast.md),
`analysis/g20a-hardware-fast.json`. Новый отдельный
`artifacts/gba/DoK-port_hardware_fast_prototype.gba`, SHAe803f8…4308c,
10 845 692 B; build/QA `research/experiments/g20a-hw-projection/hw_v128_projection/`.
Чистое повторение `research/experiments/g20a-hw-fast-final/`, source control7bab…7ea8.
Предыдущие ROM сохранены, изображение396 PPU RGB exact относительно16ba83…3961.

HW_COVER4 уменьшил128-row bitset до4words/column (C и оба ARM surface kernels,
включая linear fallback). Вернули coverage_edge в IWRAM; HW_DIRECT_RUN убрал
обратное деление tiled адреса на240; HW_PROJECT_RECIP ускорил signed projection
для denominator1..16384 с прежним fallback/error contract. EWRAM260944/free1200,
IWRAM24240/free336 до8192 stacks. Main2528/margin5408, IRQ80/margin80.

Runtime PCM RU/EN: turn24 —18,38/15,25/24,30; turn120 —19,37/15,15/20,90;
walking120 —17,11/16,75/19,58 (EN L1 17,15). **Goal/G20/G20A открыты**:
новый sweep72 fixed headings (15°,6frames, RU, замороженная симуляция+PCM)
всё ещё даёт14,931875FPS на части L1/L2; L3 minimum19,909167.
Не округлять native4-refresh plateau до универсального15+. Whole-level/physical
GBA/original BIOS не проверены,24,1/30 для всех миров нет.

Полные ARM/UI/flow/sprites/water/AI28combat/interactions logic/time/reset,
G12items и G13combat прошли. Independent effects теперь проверяют actual PPU
444 source texels, pickup disappearance — actual PPU в исходном item footprint.
G12/G13 harness regression на Mode4 control passed. В G13 поправлен старый
pre-G15 state oracle: synchronous wake/hurt дают1/3, live AI freeze не отключает
notifications. Game code не менялся.282 transitions/1601 callbacks безmixedframes;
32811 projection/прежние sampler vectors passed. Original22SHA unchanged.
Artificial all-awake stress всё ещё0,07..0,38FPS/debt; passed —guards/logic/audio,
не бюджет. Original affine interaction visual oracle намеренно omitted.

Face-midpoint LIGHT (до4724 changedpixels), hot edge_start и U quotient tracker
не выбраны по live результатам; details/privatepaths в новом документе.
Главный остаток — wall/side raster тяжёлых неподвижных видов. Процедурные/BSP/
opaque старые варианты не повторять как неиспытанные, quality selection не закрыта.

## Геометрия и128 строк — последний результат27 октября

Читать [gba-hardware-rows.md](gba-hardware-rows.md),
`analysis/g20a-hardware-rows.json`. **Goal/G20/G20A открыты**.
Новый отдельный `artifacts/gba/DoK-port_hardware_128_prototype.gba`,
SHA16ba83…3961,10 846 124 B; ELF/source `research/experiments/g20a-hw-face-box/hw_v128_box/`.
128 CPU строк растянуты в144,120 columns; GPU floor144/UI/weapon прежние.
Column cache/class filter/base surface seed/covered face box:396 exact PPU
против исходного128, overlay0differences против144, clean repeat/source control passed.
Runtime PCM: turn24 —17,70/14,78/22,40 RU (EN L3 22,05);
turn120 —18,71/14,75/20,13; walking120 —16,55/16,00/19,27 RU/EN.
15+ в LEVEL2 turning всё ещё нет. Старые ROM не заменены, quality choice не сделан.

Прошли ARM/clock/reset, UI22 actual PPU, flow, sprites8 playback/18 captures,
AI/28combat/interactions logic,282 transitions/1633PPU callbacks без mixedframes.
Новый water verifier:144 phase cases,3LIGHT bands exact source RES и362232PPU
pixels; все18 phases×4 moving materials×RU/EN. Independent combat-effect/item
presentation pixel regressions ещё открыты; original affine interaction oracle omitted.
Stressguards/audio passed, но искусственный all-awake0,07..0,37FPS и растущий
debt — реальное ограничение, также наблюдалось до этой оптимизации.
EWRAM260944/free1200, IWRAM24500/free76 до8192 stack; main2528/margin5408,
IRQ80/margin80. Original22SHA unchanged. Старый144 ROM240669…f014 теперь также
прошёл UI/flow/sprites/water (частные JSON в его QA folder); sprite harness
прошла и на исходном Mode4 ROM7bab…7ea8.

Не повторять как новые гипотезы: rims/affineU с coverage EWRAM/animation cache/
ceil prelit/near skip/contains-call/unsigned/wide divide/affine divide.
Они измерены, не дали устойчивого общего выигрыша, остались gated experiments.
Следующий шаг — L2 wall/low-ceiling raster/geometry и оставшиеся hardware pixel
regressions. Процедурный/BSP варианты отложены по прежним сравнениям.

## Отсечение и предъявление — текущий результат27 октября

Читать [gba-hardware-culling.md](gba-hardware-culling.md),
`analysis/g20a-hardware-culling.json`. **Goal/G20/G20A не завершены**:
walking120 frames RU/EN15,03/15,12/18,01 FPS, но turn/PCM24 frames
14,93/13,27/17,48 (EN LEVEL3 17,70). Не объявлять15+ по одному walking-проходу.
Новый `artifacts/gba/DoK-port_hardware_culling_prototype.gba`,10 843 464 B,
SHA`240669c13508acba18fa08a68dd0bc793c02366561ee0b617d7c1e62d976f014`.
ELF/map/source: `research/experiments/g20a-hw-early-qa/hw_early_present/`; clean repeat совпал.
Старый hardware4dfb…581d/pre-alpha/exact/coarse/BSP сохранены.

Новые flags: BACKFACE стен, BACKFACE_SIDES, SIDE_SETUP (static side mask и
повторное использование projection factor), HW_MASKED (ARM tiled alpha sampler),
HW_MAP_CACHE (пропуск неизменной BG0 map), HW_PRESENT/HW_EARLY_PRESENT. Отсечение
изменило максимум176 RGB pixels в18 видах; setup/masked/raster из последующих
шагов exact в контрольных comparisons. Пол/потолок остаются как в прошлом
hardware32×32/3 LIGHT rows, без процедурных текстур и банка масштабов.

Upload допускается с line144: сцена/оружие закончились на143, ниже полный
непрозрачный BG0 HUD. Bottom UI data ждёт160. Раннее окно допускается только
если elapsed>=1, обычное VBlank —elapsed>=2; minimum present gap2 VBlank.
Budget учитываетdirty tiles/map commands/water blocks, запас для HDraw.
Финальные observed margins>=3640 cycles, upload overflows0/cold loads0/underruns0,
coherent motion conservation0 во всех12 live fixtures. Это не universal bound.
Большой seek может превысить любое окно: теперь forced-blank переход вместо
бесконечного ожидания. Это реальная ошибка, найденная и исправленная AI tests.

На финальном ROM прошли:396 full RGB exact относительно pre-early renderer;
282 transitions(camera/36 weapon combinations/messages) и все1750 video callbacks
совпали со старым либо новым целым изображением, смешанных0. ARM1536 tiled,
512 masked,256 map и прежние arithmetic/animation vectors passed. Clock/poisoned
reset, AI perception/motion/wake,28 AI combat, interactions/doors/collision/
sensors/conveyors/finish/reset logic passed. Original affine pixel oracle в
interactions намеренно omitted для quality prototype. **Full water/sprites/UI/
flow/stress и физическая GBA ещё не проверены**. Не наследовать старую приёмку.

EWRAM260936/free1208; IWRAM24056/free520 до stacks8192. Main observed2536/
margin5400, IRQ80/margin80. Source control7bab…7ea8 и22original SHA unchanged.
`verify_ai_*` теперь ставит capture hold после1draw до первоначального phase2
wait; раньше ускоренный presenter мог всегда скрывать это короткое окно.
FPS harness не менялся на hold. Probe.capture для hardware пишет actual PPU
RGB, а не выдаёт tiled VRAM за bitmap. Новая transition harness сохраняет все
callbacks и проверяет их, не только конечные screenshot.

Новые частные варианты `hw_profile`, `hw_backface`, `hw_backface_sides`,
`hw_side_setup`, `hw_opaque_gaps`, `hw_gaps_masked`, `hw_masked`, `hw_present`,
`hw_present_map/plain`, `hw_early_present` (выбран). Gaps ARM вектор512 exact,
но full-face opaque путь всё ещё медленнее в live; не выбран. Pixel layout60
колонок/Mode2 rewrite не реализовывались как будто допустимое minor loss.
Следующий шаг — further wall/side raster/geometry и полный hardware acceptance.
LEVEL2 turn остаётся ниже цели; процедурная генерация по-прежнему отложена.

## Аппаратные пол и потолок — текущий результат 27 октября

Читать [gba-hardware-planes.md](gba-hardware-planes.md),
`analysis/g20a-hardware-planes.json`. **Goal/G20/G20A не завершены:**15+ во всех
мирах нет, full hardware regression/stress/flow ещё не выполнен. Старые ROM
сохранены. Новый отдельный `artifacts/gba/DoK-port_hardware_planes_prototype.gba`,
SHA`4dfbbe0e8a14352fcef5c182baef3ffdf0483b81345c8837b1530ee09f7f581d`,
10 841 864 B. ELF/map/source — `research/experiments/g20a-hw-final/hw_hud_cache/`.

Прямой tiled foreground убрал packing400k; Mode1 BG2 рисует пол/базовый потолок,
BG0 — полный HUD/оружие,72 affine OBJ — CPU scene120×144. Source-derived32×32
textures, все18 water crop/wrap phases, LIGHT0/7/15 только GPU-плоскостей;
стены/акторы/уступы сохраняют16 rows. Inset0/10 merged−951, depth cap3600,
lowered400 ceiling и vertical strips CPU. Native plane/foreground boundaries
не pixel-exact (до584 foreground-mask pixels в378 frozen cases).

Direct/software planes378 PPU RGB совпали с native; кеш HUD396 PPU RGB совпал
с hardware без кеша, включая четыре страницы сообщений/возврат из помощи.
Final relink18 RGB точны. ARM1536 tiled/256 map + прежние arithmetic/animation
vectors passed. Native source_control7bab…7ea8.22 originals и старые3 ROM SHA
не изменились. EWRAM260916/free1228, IWRAM24396/free180 до stacks8192;
main observed2544/margin5392, IRQ80/margin80. ROM не считается прошедшим full QA.

Существенное уточнение измерений: прежний `live_measure` держал `test_camera`,
несмотря на активные turn/fire/PCM. Новый `follow_camera=True` освобождает её.
24 follow/PCM frames: **13,52/11,20/17,27 FPS**.120 walking/turn/fire frames:
**13,22/12,60/16,48**, координаты действительно менялись, все720 PLAY, audio
не обязательно непрерывно активен. Ошибка сохранения времени0 после добавления
`g_hw_completed_logic` snapshot: иначе libretro иногда возвращает управление
во время логики следующего кадра и смешивает diagnostics. Не менять cadence.

Первый rowlut вариант имел cold flashes и upload overruns. Исправлено:
удержание16×16 window до выхода envelope; prepared packed map commands во
frame30720…37887, ARM пишет их только в VBlank; UI/weapon dirty tiles;
неизменившийся HUD вообще не пересоздаётся. На всех12 follow/walking fixtures
новых cold loads0, upload overflows0, underruns0. Не обобщать это на весь мир.

Новые отрицательные опыты: C/ARM opaque kernels хуже, paired ceiling rows почти
не влияют, exact raster-divide хуже. Новая атрибуция показывает: LEVEL2 тратит
много на **вертикальные уступы**, горизонтальный lowered ceiling дешевле. Поэтому
не начинать большой Mode2 HUD/двухплоскостной rewrite по прежней гипотезе без
измеренного основания. Процедурные текстуры остаются отложенными по результату,
не из-за отсутствия разрешения. Следующий шаг — wall/side raster и geometry/
coverage, затем полный hardware fallback/doors/grates/water/stress suite.

Builder variants: `hw_direct`, `hw_background`, `hw_rays32`, `hw_lease`,
`hw_rowlut`, `hw_stream`, `hw_hud_cache` (текущий), `hw_ceiling2`, `hw_div`,
`hw_opaque`, `hw_opaque_arm`. Непринятые прототипы/исходники сохранены в build.
Новые modules: `gba/{hardware_direct,hardware_bg}.inc`, `gba/tiled_scene.S`,
`src/ui_tiled.inc`, `scripts/gba_hw_bg_assets.py`, report collector. Startup
VBlank callback сохраняет pending audio IRQ bits; активные row tables вне frame.
Полные commands/input-report SHA — в документе/analysis; SDL2 не менялась.

## Подготовка аппаратного вывода — продолжение 27 октября

Читать [gba-hardware-floor.md](gba-hardware-floor.md),
`analysis/g20a-hardware-{ppu,bringup}.json`. **Цели FPS ещё не выполнены**;
лучшие опубликованные exact/coarse ниже не заменены.

Новый `research/experiments/g20a-hw-pack-ready/hw_obj_arm/`, SHA
`cb35b35c3eadac987a8f6487e0a7a96912778f97d0272cf0eaea1f1caee2386e`:
Mode1,72 affine OBJ для сцены120×144, BG0 для UI240. Это подготовка к BG2 floor,
**пол/потолок пока считает CPU**, affine/HBlank ещё не реализованы.18 настоящих
RGB PPU кадров RU/EN всех миров совпали с `sides_exact`;768 ARM pack/transpose/UI
vectors/guards/ABI прошли. В обычном index benchmark HW запрещён: нужен
`benchmark_gba_hardware.py`, который сохраняет настоящий video callback.
`AudioProbe.video` по умолчанию только считает кадры и не сохраняет RGB;
аппаратный harness явно заменяет callback. Нельзя выдавать пустые RGB за oracle.

Обнаруженные12 пропавших красных пикселей HP исправлены: palette1 не резервируется
под чёрный;0 заменяется ненулевым дубликатом **GBA15-bit** чёрного —223/255/255
для LEVEL1/2/3. Полные18 RGB comparisons теперь0. Многопиксельный оружейный overlay,
все water phases, полный regression/UI/flow/stress ещё не проверены.

На C упаковка стоила910k cycles, ARM —400k; upload после однократных OAM/map/
palette setup —48k вместо77k, конец околоVCOUNT199 вместо222. Она всё ещё
слишком дорога: live5,97/5,97/6,64 FPS. Не выбирать этот prototype как ускоренный.
EWRAM257044/free5100, IWRAM23372/free1204 до stacks8192. Обычный текущий main
и renderer перекомпилированы как `main_control`: ROM **7bab…7ea8** побайтно,
так что изменение `gba/renderer_probe.c` за `HW_OBJ` не меняет обычную сборку.

**Следующее действие:** прямой вывод foreground в OBJ tile layout, чтобы убрать
упаковку400k; пока оставить программные плоскости и доказать совпадение RGB,
включая оружие/прозрачность/clip. Подробная адресная формула и риски UI записаны
в документе. После этого — affine BG floor/material window/row LIGHT/HBlank DMA.
Нельзя хранить активную DMA-таблицу в очищаемом следующем `frame`; нужны двойные
буферы/перезапуск каждый VBlank, включая длительный CPU draw. Первые hardware
тесты тоже должны оставаться отдельными ROM; уменьшение фактуры оценивать по
измеренному выигрышу. Процедурные текстуры по-прежнему отложены.

Новые файлы: `gba/hardware_obj.inc`, `gba/hw_obj_pack.S`,
`scripts/benchmark_gba_hardware.py`, `scripts/report_gba_hardware_bringup.py`.
Все команды выполнялись,22 original SHA и три опубликованных ROM неизменны.
Lookup/vertex/ray-sides пробы этого прохода медленнее, не выбраны; отчёты
`analysis/g20a-{lookup,vertex,ray}-trial.json`. Не повторять их как новые гипотезы.
GEOMETRY_CACHE остаётся только исследовательским flag;5120 B не окупились.

## Общие спаны, HUD и потолочные уступы — 27 октября 2026 года

Актуальные результаты/команды: [gba-span-prototypes.md](gba-span-prototypes.md),
`docs/tasks/G20A.md`, `analysis/g20a-span-{prototypes,validation}.json`.
**G20/G20A в работе: 30/24,1/15+ FPS не достигнуты.** SDL2 не менялась.
Процедурный вариант остаётся отложенным по измеренному соотношению скорости
и качества, не получает приоритета из-за предложения пользователя.

Текущий точный кандидат `research/experiments/g20a-sides/sides_exact/`:
`artifacts/gba/DoK-port_optimized_prototype.gba`, 9 954 008 B,
SHA `b36472a0701f19a41fc009e003f0f78a4493921e1cf19f2b51c266cdfeb42501`.
488 кадров совпали с `build/select-menu-fix/`, CPU парная медиана1,324×,
live PCM RU/EN7,17/7,47/8,53 FPS. Static setup/cache, exact plane division,
offline prelit всех16 LIGHT rows без scales, точный HUD-атлас и prelit
вертикальных потолочных уступов. EWRAM254704/free7440,
IWRAM23064/free1512 до отдельного резерва stacks/BIOS8192.

Отдельный quality-кандидат `research/experiments/g20a-sides/sides_coarse/`:
`artifacts/gba/DoK-port_floor_4x2_prototype.gba`, 9 888 624 B,
SHA `04dfe358654cc865a22ec9e674217eb4cde7583bde4f61d57d361fa1e61b5d11`.
World-coordinate ARM spans/crop/wrap, исходные текстуры/16 LIGHT rows,
выборка пола/потолка4×2; геометрия стен/дверей/акторов120×144, HUD240×160.
Live11,95/9,95/14,93 FPS:14,93 ниже15. 488 кадров совпали с прежним coarse-ROM,
не с оригиналом; affine native oracle к coarse не применяется. Полная геометрия
материалов/высот может сдвигаться примерно на строку. EWRAM255828/free6316,
IWRAM22396/free2180. На release default не назначен.

Оба ROM прошли ARM/renderer/sprites/interactions/time/28AI/UI/flow и
integrated normal/frozen/capacity/all-awake ×6; audio underruns0, main stack
2368/margin5568, IRQ80/margin80. Чистая отдельная повторная сборка обоих и базы
дала те же ROM. All-awake overload сохраняется: worst585798080/468682048 cycles,
simulation debt и callbacks не отбрасываются. 22 originals SHA совпали.
Физическая GBA, original BIOS, целый уровень и самостоятельные23sound
correlation в этом проходе не проверялись.

Код: `renderer_world_planes.inc`, `gba/world_spans.S`, `ui_panel_cache.inc`,
`gba/ui_panels.S`, side-resource table в builder и side path в
`renderer_surface_raster.inc`. Общая сборка `scripts/prototype_gba_bsp.py`;
собирать выбранные `baseline sides_exact sides_coarse` в **новую** папку,
не поверх загруженных тестами файлов. Замеры — `benchmark_gba_prototypes.py`;
атрибуция — `profile_gba_world_spans.py` (с timestamp overhead, не headline FPS);
aggregate/delivery — `report_gba_spans.py`. Проверенные файлы/снимки исходников
остаются в `research/experiments/g20a-sides/`; второй проход — `research/experiments/g20a-second/`.
Существующий `DoK-port_pre_alpha.gba` не заменён. Старые report scripts
копируют архивные v1/v2 имена, не перезаписывают актуальный exact/coarse.

Дополнительные WORLD_DIV/RASTER_DIV/EAGER_CLEAR/MASKED_ARM пробы не выбраны:
выигрыш мал либо есть регрессия; negative summary с ROM SHA в aggregate.
Они прошли только сравнительный smoke/live, не полную приёмку. В частности,
общий `gba/memory.c` уже оптимизирует делители<65536 через reciprocal;
не повторять без нового профиля попытки получить большой выигрыш такой заменой.
Следующий шаг — сокращение traversal/geometry и стоимости покрытия/спанов;
крупного ускорения ещё нет, G20B/G21 ожидают. BSP ниже — сохранённый первый опыт.

## Первый проход BSP/plane/light — 27 октября 2026 года

Читать [gba-quality-options.md](gba-quality-options.md), `docs/tasks/G20A.md`,
`analysis/g20a-prototypes.json`, `analysis/g20a-prototype-validation.json`.
Это GBA; SDL2 не менялась. **G20/G20A остаются в работе: 15+ FPS ещё нет.**
Ориентиры пользователя теперь явно 30 / 24,1 / 15+ FPS; небольшое ухудшение
картинки допускается. Процедурный пол/потолок не получает приоритета из-за
авторства идеи: измеренный результат не оправдал картинку, вариант отложен.

Лучший проверенный на первом проходе `research/experiments/g20a-prototypes/exact_planes/`:
`artifacts/gba/DoK-port_exact_planes_v1.gba`, 9 938 648 B, SHA
`ef4237c9699c140adca2e0dbb951938194331d1664a36f6d1d27ea2f7efed999`.
Static setup/cache + точный ARM reciprocal plane divide + offline prelit
16 LIGHT rows без scale path/банка. Все 488 кадров побайтно совпали с
`build/select-menu-fix/`, CPU парная медиана 1,20×; live стартовые RU/EN fixtures
с PCM22 050 дают 6,64/6,64/7,47 FPS. Цели не выполнены; это не полный уровень.
Прежние `DoK-port.gba`/`DoK-port_pre_alpha.gba` не заменены.

Отдельный BSP `research/experiments/g20a-prototypes/bsp_setup/`, копия
`artifacts/gba/DoK-port_BSP_prototype.gba`, SHA `735ba244…eb76297`.
8191-node/view grid BSP, conservative Y/depth occlusion, sprite/AI lease
сохранён. 488 кадров точные, но обычно медленнее `setup`: prepass дублирует
geometry. Не делать BSP default только по названию алгоритма.

Код и команды: `scripts/prototype_gba_bsp.py` (исходники/flags/base-object SHA),
`scripts/benchmark_gba_prototypes.py` (9 frozen камер, live6worlds),
`scripts/report_gba_prototypes.py` (связывает проверки/SHA и копирует ROM).
Данные только из текущего G03 package; EXE не анализировались. 22 originals
SHA совпали с pre-task inventory. Baseline ROM воспроизведён побайтно.
Все частные ROM/ELF/map/source/PNG в `research/experiments/g20a-prototypes/`; не включать в Git.

Проверки exact-варианта: 65 536 ARM divide vectors + sampler4096/coverage2048/
cell30 720, 48 animation resources/308 frames; mGBA renderer/reset, sprites/water,
doors/sensors, time, 28 enemy variants, UI/все сообщения, flow6worlds/14texts.
Integrated normal/frozen/capacity/all-awake ×6, PCM actual, underruns0.
EWRAM254 704/free7440, IWRAM22 888/free1688 до stack reserve8192;
main2368/margin5568, IRQ80/margin80. All-awake worst651 488 832 cycles,
retained simulation debt остаётся, callbacks не dropping.
Full23sound correlation suite и physical GBA не повторялись.

Стенд исправлен под прежний Select/menu fix: interactions меняет язык через
mailbox; camera/UI ждут свежий завершённый кадр; synthetic UI snapshots держат
VBlank после live keypad/pause проверок. Capture hold в FPS замерах выключен.
Пять градаций света, Q31 без correction, 120×72 и procedural/world mapper
сохранены отдельно, не выбраны: качество/выигрыш хуже exact-варианта.

Следующее действие остаётся G20A: shared spans/visplanes с исходными материалами
и дешёвой видимостью либо единый front-to-back BSP без повторного prepass.
Не повторять C per-pixel world lookup как предполагаемое бесплатное ускорение:
первый прототип медленнее. Готовность G20/G21 по бюджету не объявлять;
G20B/G21 ожидают. Ниже сохранены исторические записи.

## FPS, BSP и Select — 26 октября 2026 года

Читать [gba-fps-hypotheses.md](gba-fps-hypotheses.md),
`analysis/g20a-fps-audit.json`, `scripts/research_gba_fps.py`.
G20A начата и в работе; G20 открыта по бюджету, G20B/G21 ожидают.
Аудит использует baseline после level1 fixes, побайтно воспроизведённый из
сохранённых объектов: SHA `d5bdcd17…7957e`. Частные ROM/ELF/map/source/PNG
в `research/experiments/g20a-audit/`; production renderer/AI не изменены.

Девять frozen камер и live fixtures RU/EN трёх миров с PCM: отказ от scales
в live даёт примерно 2–8% FPS, без pixel differences на девяти frozen кадрах;
unlit 4–17%, но заметно меняет дальний план. AI radius /2 почти не помогает
стартам (L1 около2%, L2/3 около0). Полные замеры и ограничения в документе.
BSP требует раннего отказа до geometry/surface setup, не только замены sort.
Измеренного BSP-прототипа нет; следующий эксперимент — conservative static
occlusion и сокращение настройки floor/ceiling quads, с сохранением AI lease.

Select в игре больше не меняет язык; меню и Start+Select pause проверены.
`build/select-menu-fix/`: ROM32891192, SHA
`7babd40f913eb12f77c93c9df917cea432b0cbe3bd83e11da45ba08dec917ea8`.
Host UI, ARM UI, renderer и flow прошли, отчёты `analysis/select-menu-*.json`.
Готовая копия: `artifacts/gba/DoK-port_pre_alpha.gba`. Экспериментальные
усечения в неё не включены. Физическая GBA и полный G20 suite не проверены.
Старые записи ниже сохраняют исторические статусы/ROM, а не текущий следующий шаг.

## Правки первого уровня — 26 октября 2026 года

После проверки пользователем уменьшены иконки, исправлено центрирование
нижних сообщений; длинные сообщения целиком помещаются в одну строку за счёт
уплотнения FONT8. По дополнительному уточнению иконки уменьшены **offline**,
хранятся в ROM готовыми 12×8/8×8/10×12 и выводятся без runtime resize/делений.
Нижняя подсказка полноэкранных текстов и номер страницы тоже в одной строке.
У полностью открытой двери устранена остаточная полоса исходного crop;
промежуточные фазы и игровая логика сохранены. Это отдельная коррекция
представления GBA, подробно — [level1-fixes.md](level1-fixes.md).

Актуальный готовый ROM — `artifacts/gba/DoK-port.gba`, из `build/level1-fixes/`;
32 891 196 байт, SHA `d5bdcd171cfbca8c95d3965757a3c586c07622ce74243f5edfdce3b84e27957e`.
Host UI, Linux compile check, ARM UI/flow/interactions/renderer/time прошли.
Проверены 88 различных LEVEL-сообщений/532 ссылки и 14 системных строк,
12 парных дверных сцен со старым ROM, готовые ROM-пиксели иконок.
Результаты — `analysis/level1-*.json`; визуальные кадры просмотрены.
EWRAM 253668/free8476, IWRAM 24392/free184 до 8 KiB stack reserve.
Новый полный performance/AI/audio проход и физическая GBA не проверялись.
Прежние ROM сохранены; G17/G09 готовы, G20 открыта по бюджету;
следующий основной шаг остаётся G20A, затем G20B и G21. SDL2 не менялась.

## Компактный HUD и бэклог UI — 26 октября 2026 года

По запросу пользователя готовый ROM также выделен в
`artifacts/gba/DoK-port.gba`: копия проверенного `build/g17-ui/dok-diagnostic.gba`
с тем же SHA-256. ROM исключён из Git, промежуточные файлы остались в `build/`.

По запросу пользователя реализованы компактные HP/заряд с исходными иконками,
оригинальная сине-серая рамка и зелёный текст. W/L полностью удалены из HUD.
Медный/железный/золотой ключи появляются маленькими исходными иконками только
при наличии. Сцена/полоса сообщений сохранены. Источники, геометрия и команды —
[ui-hud-refresh.md](ui-hud-refresh.md); исходные GBA-изменения рабочей копии
сохранены, SDL2-линия не менялась.

Текущий ROM с новым UI — `build/g17-ui/dok-diagnostic.gba`, 32890948 байт,
SHA `e9895ed2037fc970529111ff7a22897b94451a5543bcba7193312e3631bda573`.
Frozen G18 и прежняя G20 сохранены. Host UI и ARM UI/flow/renderer/time прошли,
включая 8 масок ключей × RU/EN и точные пиксели из RES; реальные экраны
просмотрены. Отчёты — `analysis/g17-ui-*.json`. EWRAM 253668/free8476;
IWRAM24308/free268 до резерва стеков. Новый полный performance/AI/audio проход
и физическая GBA не проверялись, 30 FPS не объявлены достигнутыми.

**Новая задача основного плана [G20B](tasks/G20B.md): полноценная заставка
и главное меню, после G20A и до G21. Ожидает, не начата; нынешний интерфейс
остаётся временным. G20B — обязательная зависимость релизной регрессии G21.**
G17 готова, G20 остаётся открытой по бюджету, следующий основной шаг — G20A.

## Пересборка текущей GBA-версии — 26 октября 2026 года

По запросу пользователя выполнена команда
`powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build-gba.ps1 -RendererProbe -HalfColumns -OutDir build/g20`.
ROM/ELF/map и `build-report.json` обновлены в `build/g20/`.
ROM: 32 890 604 байта, SHA-256
`a56330bf11dc6f3e5906d71b8d2dbb20b2c36a55c9f3e63c8c8eb5baa44c5c94`;
побайтно воспроизведена ранее проверенная G20. Дополнительно выполнена
`scripts/verify_flow_gba.py --build-dir build/g20 --report build/g20/rebuild-flow-verification.json`
с общим Python и `DOK_PROBE_FAST_VIDEO=1`: все шесть миров/локализаций,
14 текстовых ресурсов, смерть/повтор/Game Over и Continue прошли в mGBA.
Полный профиль G20 повторно не запускался. Исходники и статусы этапов не изменены;
следующий шаг остаётся G20A.

## G20: оптимизация без усечений — 26 октября 2026 года

**G00–G18 готовы. Проход оптимизации G20 выполнен и проверен; G20 открыта
по 30 FPS. Следующий ID — G20A, определение усечений.** Прямое уточнение
пользователя: в G20 не урезать заметную на целевом разрешении графику,
а при превышении бюджета вынести выбор усечений в следующий шаг.
G20A оформлена в `docs/tasks/G20A.md` и не начата; G21 ожидает закрытия бюджета.

Читать [gba-performance.md](gba-performance.md), [tasks/G20.md](tasks/G20.md),
`analysis/gba-final-budget.json`. Прежний документ G06A сохранён без потери
содержимого в `docs/gba-performance-g06a.md`; его числа не относятся к G20.

`build/g20/dok-diagnostic.gba`: **32 890 604 bytes**, SHA-256
`a56330bf11dc6f3e5906d71b8d2dbb20b2c36a55c9f3e63c8c8eb5baa44c5c94`.
ELF/map рядом. Frozen G18 с SHA `46e4bfa4…eeb3c0` сохранён отдельно:
он нужен для paired pixel oracle, не пересобирать его из новых исходников
под видом старого эталона. Оба ROM и оригиналы остаются вне Git.

Сохранены 120 колонок сцены → 240×144, UI 240×160, texture/LIGHT sampling,
геометрия и видимость, прозрачность, cadence и object/projectile limits.
488 полных кадров трёх миров × RU/EN (5° sweep, near/corners, четыре направления
воды × 0/1/9/17) побайтно совпали с G18. Парная медиана ускорилась в 1,80×;
стартовый CPU без PCM 4 772 864 → 2 531 520 cycles. Обычные живые стартовые
сцены с PCM дают ≈5,4–6,7 FPS вместо ≈2,8–3,1. Цель 561792 cycles/30 FPS
не достигнута. Лучший непустой стартовый обзор 1376640 cycles, худший 3203904.

Полный профиль: normal live, frozen/PCM, native pool-capacity 141/139/98
и all-awake live для каждого мира/языка. В обычных сценах motion debt <416 ticks,
cadence сохраняется. В all-awake stress 57–100 AI owners CPU до 651634240 cycles
и большой retained debt; это не обычная сцена и не подтверждение реального
времени при любой нагрузке. Не скрывать проблему dropping ticks. Возможность
иного алгоритма без усечений не опровергнута; все переписывания не исчерпаны.

EWRAM 253376/free8768; IWRAM 24268/free308 до отдельного 8 KiB stacks/BIOS
reserve. Main 2368/margin 5568, IRQ 80/margin80. Mode4 pages 76800, high address
offset 79360, ROM free663828. Heap0; guards/linker/watermarks без overflow
в проверенных сценариях, audio underruns0, включая длительные overloaded кадры.

Изменения:

- `src/renderer*.{c,inc}`, `gba/columns.S`, `gba/divmod.S`: bitmap coverage,
  lazy intervals/bit scans, exact ARM floor spans, active edges, vertex cache,
  перенос startup-only данных из IWRAM. Epoch rollover проверен.
- `src/ui.c`: word fill с byte edges, исходные FONT8/FONT16 pixels сохранены.
- Исправлена ошибка порта: texture index клона снаряда не используется для
  чтения за границей LEVEL definitions при проверке door crop. Это не изменение
  исходных правил PKR/PKE. После исправления повторён полный pipeline.
- `src/ai.c`, `src/collision.c`, `src/combat.c`, `include/dok/cell_coordinate.h`,
  `scripts/render_tables.py`: immutable fields и точные cell/collision lookups.
- `src/animation.c`, `include/dok/animation.h`, `scripts/pack_animation_index.py`,
  `scripts/build-gba.ps1`: validated immutable ROM index, 24 resources/157 frames,
  1016 bytes, checked mutable-buffer fallback. Новых native/DPMI адресов нет.
- `gba/renderer_probe.c`: completed stage snapshot, hash only on changed camera.
  Старые mailbox/diag/world/object/player ABI сохранены. Добавлены
  `g_render_completed_stages[7]`, optional `g_render_work[5]`
  (enable, wallticks, surfaceticks, wallcount, surfacecount), `g_ai_work[4]`
  (lookupcalls, sightcalls, longestray, cappedrays). Timing quantum 64 cycles.
- Scripts: `build-gba-performance.ps1`, `verify_performance_arm.py`,
  `verify_performance_gba.py`, `profile_game_gba.py`, `profile_renderer.py`,
  `report_performance.py`; evidence — `analysis/g20-*.json` и final budget.

Полный driver **выполнен одним вызовом**:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build-gba-performance.ps1 -CrossChecks
```

Лог `build/g20/g20-validation-final.log`. Windows host/UI/flow прошли;
Linux static и Thumb compile checks прошли, Linux не запускался. Unicorn:
4096 sampler + 2048 bitmap + 30720 cell vectors, 48 original animation resources/
308 frames, ABI/stack/error-output checks. mGBA: flow 6 worlds/14 texts,
UI/fonts, renderer, sprites/water, interactions, time/reset, 28 AI variants,
23 PCM actual capture/correlation. Root просмотрел 3 PNG, SHA —
`analysis/g20-visual-review.json`; это не новое пользовательское listening.
Промежуточные эксперименты сведены в `analysis/g20-experiments.json`.

Все 22 original SHA совпали до/после и с pre-G20 cleanup inventory. EXE-анализ
в этой задаче не проводился. Physical GBA, original BIOS, whole DOS session,
Linux/MSVC/CMake execution не проверялись. Общие Zig/Python/mGBA прежние,
Unicorn — `C:/Source/wasdify/build/research-deps`, копий toolchain нет.
Параллельные SDL2/Old Gold планы и их commits к G20 не относятся; их не менять.

Ниже — историческая передача до G20.

## Очистка артефактов — 26 октября 2026 года

По запросу пользователя удалены 10 661 файлов (5 165 518 037 байт, 4,81 GiB):
локальный кэш Zig, экспериментальные сборки G06A, исторические сборки G07–G15
и G17, промежуточные object-файлы и дубли банков G16/G18, старые host binaries,
одноразовые файлы в корне `build/`, Python cache и случайный корневой `debug.log`.
Полный перечень — `analysis/artifact-cleanup-20261026.json`.

Сохранены оригиналы, текущие ROM/ELF/map G18, сборка G16 без промежуточных
объектов/банка, принятый listening WAV, отчёты `analysis/`, проекты/журналы
Ghidra и экспорты, DOS reference captures, материалы Old Gold и исходники проверок.
SHA-256 всех 22 файлов `originals/`, трёх итоговых файлов G18 и принятого WAV
проверены до/после; изменений нет. Исторические ссылки на удалённые `build/`
теперь обозначают результаты прежних запусков, а не наличие файлов на диске.
Для их получения нужна пересборка соответствующими скриптами; кэш создастся заново.
После очистки сборки/эмуляторные проверки не запускались. G00–G18 завершены,
следующий этап по-прежнему G20; очистка его не начинает.

## Текущая граница G18

26 октября 2026 года: **G00–G18 готовы; реализация, независимые проверки и review
G18 завершены. Следующий ID — G20; его не начинали.**
См. [game-flow.md](game-flow.md), [tasks/G18.md](tasks/G18.md),
`analysis/g18-review.json`. Ниже оставлена историческая передача G17 и ранних этапов.

Важное уточнение исходного gameplay: PKR/PKE выбирают уровни1–3 независимо.
BINTRO → игра → EINTRO → выбор уровня; автоматической кампании и переноса
инвентаря в другой мир нет. ENDTXT показывается при выходе из выбора, поэтому
на GBA доступен пунктом «Выход». В EXE есть score==world total, но во всех6
исходных LEVEL нет category99 предметов и total=0: собирать очки для выхода
**не нужно**. Подтверждение — свежие Ghidra/x86 и `g18-exit-audit.json`, не Old Gold.

New Game (title focus4, Left/Right выбирает LEVEL1–3) полностью сбрасывает мир,
инвентарь, keys/map/visited, input/debt, UI queue, overlay, все owners/events,
снаряды и PCM/DMA/Timer0/1. Перед сменой borrowed context снимается
`dok_collision_overlay(0,0,0)` и освобождается occupancy старых снарядов.
FINISH только ставит pending, disposal выполняется после callback dispatch.
Finish/END/Game Over очищают завершённую сессию; фон runtime остаётся валидным.
Retry того же мира сохраняет события/снаряды/предметы/инвентарь; native copy
восстанавливает только modeled часть hero+26…43 и HP=max. Lives — wrap16 DEC.
Не копировать весь actor64 при retry. Отрицательный HP — исходный finish sentinel.
Continue сохраняет suspended world и все законные события, New Game заменяет его.

Title6rows: Continue/Language/Controls/Statistics/New Game/End; пауза прежняя.
Игровые A/B/D-pad/L/R/StartUse/SelectLanguage/Start+SelectPause сохранены.
Текстовые экраны: A/Start дальше, B назад/в меню, Select язык; held переходная
кнопка блокируется до отпускания на **каждом** sample. Тексты из существующего
ROM, CP866 FONT8,30×14 y20, CR/LF/`$`, без дублирования/усечения; заголовок FONT16.
Временные экраны останавливают simulation ticks/remainder/deadlines без catch-up,
hardware clock и текущий PCM продолжаются до disposal.

Frozen `build/g18/dok-diagnostic.gba`:32881620bytes,
SHA `46e4bfa412dd1abb6b729d4a9ed014802e99430b1dbc47ffb70d2de077eeb3c0`.
EWRAM248688/free13456; IWRAM20684/free3892 до8KiB stack reserve.
Main2360/margin5576; IRQ80/margin80;32MiB ROM remaining672812.
Renderer4735872…5234880 cycles;30FPS остаётся G20, качество не снижали.

Пройдены host flow/UI и cross compile, SHA до/после Ghidra, original x86
16stat+14death+2helper, аудит6 исходных LEVEL, независимый ARM всех6 уровней/
14text resources, real stats death/retry/3lives/Game Over, реальные выходы,
seeded door/AI/visual/projectile/loopPCM retain/dispose, Continue/END/NewGame,
точные исходные FONT8 pixels всех страниц;8 representative PNG просмотрены root.
UI/renderer/interactions/time/28AI/23PCM регрессии прошли на final ROM; underruns0.
`analysis/g18-review.json` связывает11 отчётов и source/ELF/ROM/map.

Воспроизведение: `scripts/build-gba-flow.ps1 -CrossChecks -OriginalEvidence`.
Шаги реально выполнены отдельно; полный driver одним вызовом не повторялся.
Shared Zig/Python/mGBA — прежние пути [building.md](building.md), копий нет.
Legacy harness helper явно вызывает op3 `g_flow_probe` и обходит только flow;
полноценный ARM flow verifier использует настоящий boot/кнопки/production callbacks.
ABI `g_flow_diag`14word/`g_flow_probe`4word указан в game-flow. Старые world/object/
player и mailboxes ABI сохранены. Save/load отсутствует; G19 удалена пользователем.

Физическая GBA, оригинальный BIOS, целая DOS-сессия, Linux/MSVC/CMake execution
не проверены. Accepted G16 listening остаётся прежним, G18 PCM проверен численно.
В параллельной внешней работе появились `analysis/old-gold/` и OldGold scripts;
они не относятся к G18 и должны сохраняться отдельно, без включения в его commit.

## Текущая граница G17

Обновлено 26 октября 2026 года: **G00–G17 готовы, следующий ID — G18**.
См. [ui-localization.md](ui-localization.md), [tasks/G17.md](tasks/G17.md),
`analysis/g17-review.json`. `src/ui.c` и `gba/ui_backend.c` реализуют original
CP866 FONT8/FONT16, HUD, title/pause/help/statistics, очередь уведомлений
предметов+58, дверей и завершённой смерти монстров+BE. Сцена240×144/120колонок,
UI240wide, полоса сообщений240×16. HP/E — signed числа; W/L и Khex — в углах.
Ключи/максимумы/очки/карта объяснены на экране показателей.

Start=Use, Select=язык, **Start+Select=пауза** (прежний panic удалён).
Up/Down выбирают пункт, A/Start выполняют; B/Select закрывают паузу/подэкран.
Menu-closing held key блокируется до отпускания. Title Continue и возврат
через главное меню **сохраняют существующий мир**. G18 должен добавить
настоящий new game/reset и полный game flow; не считать их уже реализованными.
Пауза сохраняет ticks/remainder/deadlines без catch-up; hardware Timer2/3
и текущий PCM-голос продолжаются. Язык меняет только presentation view:
mutable world33864bytes, карты и общий balance сохраняются.

Frozen `build/g17/dok-diagnostic.gba`:32876132bytes,
SHA `b63ae8087b6f4cff5f121bd919b982ecbf750a2d786c581ae85166c2f7b7aefb`.
EWRAM248564/free13580; IWRAM20684/free3892 до8KiB stack reserve.
Старый HUD из IWRAM удалён, новый UI исполняется из ROM.
Main stack2360/margin5576, IRQ80/margin80. ROM32MiB remaining678300bytes.
Host UI и Linux static/Thumb compile прошли; fresh Ghidra RU/EN SHA совпали,
original x862048 glyph vectors/18 message-copy vectors прошли. Независимый
ARM проверил кнопки, pause/fraction/input, исходные FONT8/FONT16 pixels,
границы,4 страницы, real pickup+death+BE обоих языков. Root проверил PNG.
Renderer/interactions/time/28AI/23PCM regressions прошли; audio underruns0.
Команда `scripts/build-gba-ui.ps1 -CrossChecks -OriginalEvidence` (PowerShell);
pipeline выполнен отдельными шагами, весь driver одним вызовом не повторялся.

Legacy `Probe` явно пропускает title через UI mailbox; UIProbe использует
`preserve_title=True`. Прямые renderer/time/interactions harnesses адаптированы.
Renderer snapshot ждёт первый simulation step (startup parameter98:1→-1).
Idle message band теперь пустая; локализация текста проверяется отдельным UI
тестом. `DOK_PROBE_FAST_VIDEO=1` откладывает Python RGB conversion до PNG,
не отключая ARM rasterization/raw callback. UI mailbox/diag ABI — в тематическом
документе. `g_render_world`/object36/player60 и старые mailboxes сохранены.

Renderer regression4735552…5222592cycles;30FPS остаются G20.
Физическая GBA/оригинальный BIOS/целая DOS-сессия и Linux/MSVC/CMake execution
не проверены. Слуховая оценка относится к принятой записи G16, не новому
пользовательскому прослушиванию G17. Далее исторические передачи.

## Текущая граница G16

Обновлено 26 октября 2026 года: **G00–G16 готовы, следующий ID — G17**.
Спецификация — [gba-audio.md](gba-audio.md), результат — [tasks/G16.md](tasks/G16.md).
`src/audio.c` хранит один заимствованный PCM-голос; `gba/audio_backend.c`
стримит original unsigned PCM8 из ROM с signed conversion в два16-byte буфера.
DMA1/FIFO A и Timer0/1 обслуживают аудио; Timer2/3 сохраняют rational clock.
Renderer profiler теперь использует его квант64cycles. Present DMA3 ограничен
256word transfers: большой transfer блокировал C refill, хотя DMA1 продолжала
читать. Разрешение/число передаваемых пикселей не уменьшены.

Native mono admission/stop/query/global stop и loop latching восстановлены:
rejected same-object play меняет object flag, но не текущий hardware latch.
Sound36 выделяется отдельно от cached PCM; definition identity=(object<<16)|slot,
dynamic projectile identity=40000000|(slot+1), standalone test voice=80000000|PCM ID.
Нельзя заменять identity одним resource ID: другая дверь/clone с тем же PCM
не имеет права останавливать текущий звук. Source proof RU `1060:0145`,
`1068:2368`, EN bind `1018:1965`, clone`1020:2960`; complete fresh Ghidra
exports/manifests — `research/evidence/audio/{ru,en}/`, оригинальные SHA до/после совпали.

AI WAKE/default, CONTACT/RANGED/F0, DYING/globalstop+F4, DEAD/stopF4;
player damage/A6, shot/weapon default, projectile impact/removal, item accepted
force/default и door moving/loop/stop/locked consumers подключены. AI HURT звук
не придуман. Death text+BE и остальной UI — **G17**, hero death/game flow — **G18**.
Bindings разрешаются по текущему language/level. Смена языка сохраняет звучащий
ROM view; level reset останавливает голос. C=255/D=128 не объявлены volume/pan.

Frozen ROM `build/g16/dok-diagnostic.gba`,32867908bytes,
SHA-256 `99b4e21fabba12543bffa9217adf7e2a4d78655a45b7b0e57de1b0a0a022273e`.
EWRAM248136/free14008; IWRAM23980/free596 до8KiB stack reserve. ARM world33864,
object36/player60 сохранены. Voice/cursor/staging128bytes. IRQ stack80used/80margin,
main2360used/5576margin. Actual mGBA65536Hz stereo capture всех23 звуков:
whole correlation0.98793..0.999947, underruns0/81888DMA IRQ, max gap26368cycles,
C refill/rearm max1280cycles (не весь ISR). Long474361samples:43.028755s при
ожидании43.033209s, renderer123frames. Paired fixed-scene CPU elapsed overhead
14.48% для22050 nominal,6.77% для11025; framebuffer одинаков.

Пользователь прослушал actual short-loop excerpt и ответил «Звучит нормально»;
provenance/WAV SHA — `analysis/g16-listening.json`. Прослушанный файл сохранён
от ROM5f65... после DMA3 fix; последующие правки меняли identity, не PCM.
Это не physical GBA/Sound Blaster equivalence и не слуховой аудит всех23 звуков.

`analysis/g16-review.json` связывает source/ROM/ELF/map, host/native/ARM/listening
и renderer/interactions/time/28enemy regression отчёты. Все прошли на99b4... .
Команда: `powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build-gba-audio.ps1 -CrossChecks -OriginalEvidence`.
Части pipeline выполнены отдельно. Windows host исполнен; Linux static/Thumb
cross compiled, Linux/CMake/MSVC execution и физическая консоль не проверены.
Silent renderer regression4671616..4693696cycles; **30FPS остаются G20**.

Далее исторические передачи предыдущих границ; их указания «audio/UI не
исполняются» относятся к состоянию до G16.

## Текущая граница G15

Обновлено 26 октября 2026 года: **G00–G15 готовы, следующий ID — G16**.
`src/ai.c` и `gba/ai_probe.c` подключены к общему GBA-ROM: состояния0–4,
контактная attack animation, melee5 HP только после полного цикла, strict
burst timer, лимит серии/reset, native glyph3/4 projectile, hurt и death.
Подробные адреса/ABI/политики и матрица — [ai-combat.md](ai-combat.md).
Табличный release balance общий для языков; исходные RU/EN проверены отдельно.

Wake по consideration или первому sleeping hit сохраняет HP и создаёт owner.
DAMAGE ставит state3, callback при HP<=0 запускает death frame0. Каждый кадр,
включая последний, выдерживает свой interval; следующий callback завершает
смерть. Corpse получает visual height0 и flags|0x10 **без0x400**: slot и
последний кадр сохраняются, owner удалён, projectile hits проходят сквозь него.
БОЯР_У/МЫШЬ_У заканчиваются прозрачным кадром4×3; БАБА_У оставляет видимое
тело. Empty TMap class0 получает corpse в TMap, иначе corpse остаётся в MMap
при сохранённой TMap. FCMap/visited не удаляются. RECLAIM остаётся отдельным
переходом перед reuse flagged slots.

Native burst условие: unsigned elapsed/10000 строго больше signed delay,
shot_count<limit и front class не8/9, затем native 100-unit sight march.
Успешная попытка расходует место серии до allocation; неуспешные guards
перезапускают timer и shot_count=0. AI синхронизирует combat.now перед spawn,
первый projectile deadline=ai.now+416. Dynamic MMap front lookup использует
реальный combat.occupied и slot template ID; static/dynamic lookup смотрят
одну clamped клетку. Реальный летящий front projectile подавляет выстрел.

Scheduler AI по actor_index имеет128 owners. Display o.frame и native
nextcounter раздельны: event.parameter хранит следующий кадр; scalar+50
активного class9 теперь unknown. Старые scalar+B0/+B6 сохраняют G02
startup constants, не активный DOS clock; engine использует compact timer.
Native clock storage equivalence не заявляется. Object36/player60/ARM world33864 сохранены;
burst timer/count/corpse используют запас существующего union. AI connect и
collision overlay заимствуют context одного активного world. Перед выходом
context снять `dok_collision_overlay(0,0,0)`; production context статический.

Ghidra exports/manifests — `research/evidence/ai/{ru,en}/`, EXE SHA до/после совпали.
Native x86 на язык:648 burst,6 melee/hurt,12 player damage,4 corpse случая.
Перехвачены внешние sight/animation/sound/allocation/stats/text/owner boundaries;
полный callback с DOS scheduler closure и целая DOS-сессия не эмулировались.
Windows host прошёл14 исходных групп×RU/EN; Linux static/Thumb скомпилированы.
Root ARM проверил28 вариантов: wake, каждый melee/death frame и deadline,
hurt, strict timer, нулевые/полные серии/reset, projectile selection/deadline,
оба corpse planes, owner cleanup, wall/monster/front-projectile guards и
allocation failure. Renderer RU/EN/guards/reset/stack, interactions и rational
clock ARM-регрессии прошли; полный1455-camera sweep не повторялся.

ROM/ELF/map/PNG — `build/g15/`; ROM32864108 байт, SHA-256
`a7d0dc7d938a4e076ba8f2be0d863bdd56c1600e0e2dd712828cf7d5e3e2fa3f`.
EWRAM247872/free14272; IWRAM23980, до8KiB stack reserve596 байт.
ARM AI stack high-water2360/margin5576.
Static renderer4668642…4690694 cycles;30FPS не достигнуты, G20.

Команда: `powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build-gba-ai-combat.ps1 -CrossChecks -OriginalEvidence`.
Части выполнены отдельно; `analysis/g15-review.json` связывает source/ROM,
Ghidra evidence и host/ARM проверки. Физическая GBA/оригинальный BIOS,
полный DOS и Linux/CMake/MSVC execution не проверялись.

Следующий **G16** должен подключить звуковой consumer к existing AI events:
WAKE — idle sound; CONTACT/RANGED — attack sound с native is_playing guard;
DYING — restart death sound; DEAD — stop и death_text для будущего G17 UI.
Sound bindings +F0/+F4 и text field+BE разрешаются по object ID; DOS handles
не переносить. Сейчас эти уведомления доступны через sink, audio/UI не
исполняются, игровые transitions/cleanup от consumer не зависят. G17 — HUD,
G18 — game flow/hero death. Язык не меняет баланс/геометрию.

## Текущая граница G14

Обновлено 26 октября 2026 года: **G00–G14 готовы, следующий ID — G15**.
AI perception/motion подключён к общему GBA-ROM: `src/ai.c`,
`include/dok/ai.h`, `gba/ai_probe.c`. Getter class9 рассматриваемого renderer
объекта использует строгий Chebyshev-порог; глобальной проверки всех sleeping
монстров нет. GBA берёт consideration lease предыдущего draw в simulation
time. Точное DOS render/event interleaving не заявляется. RNG в ветвях нет:
повтор определяется runtime/ticks/input/lease. Курс использует native coarse
angle table (ctor scale1000), до10 collision-resolved проб полуклетки и
счётчик N+1. AI использует original acceleration, герой сохраняет исправление G07.

Спецификация/адреса/ABI/политики — [ai-movement.md](ai-movement.md).
Ghidra exports/manifests — `research/evidence/ai/{ru,en}/`, SHA EXE до/после совпали.
Оригинальные x86 probes исполнили на язык1049 углов,200 perception и25 course
cases. Полный active callback, FPU table ctor и scheduler closure целиком не
эмулировались; abs/wake boundaries перехватывались. Host original RES трассы
разные: RU `d5b9deed`, EN `97ed0ec7`; общий согласованный release profile
даёт `a20c2856` в обоих языках. Переключение языка не меняет механику.

Combat WAKE подключён: первый контакт sleeping сохраняет HP и создаёт AI
owner. RECLAIM снимает AI и visual owners до slot reuse. CONTACT с героем
останавливает обычную ветвь движения и передаёт seam G15; пока его interval416.
G15 должен восстановить attack/hurt/death, таймер серии, анимационный цикл и
completion; visual completion не заменяет игровые переходы. При health<=0
G14 прекращает motion event. Displayed frame остаётся валидным после wake;
native nextcounter хранится в scheduler event.parameter как следующий кадр.

Scheduler AI имеет128 owners по actor_index, object ID получается через
actor.glyph→ROM glyph_index. Повторный glyph не дублирует owner. Runtime33864,
Object36/player60 сохранены. Dynamic collision borrowed overlay по-прежнему
принадлежит combat одного активного world; перед уничтожением context нужен
`dok_collision_overlay(0,0,0)`. Host явно снимает overlay; production статичен.

Windows suites, Linux static/Thumb compilation прошли. Host проверил repeated
traces,4 world corners, CONTACT, owner removal/reuse и actual combat RECLAIM.
ARM проверил40 perception edges, deterministic/blocked/repeated-glyph motion,
живой consideration wake, valid displayed image до первого callback и actual
combat WAKE/RECLAIM. Renderer/язык/reset/guards/stack, interactions и rational
clock regressions прошли.1455-camera sweep не повторялся.

ROM/ELF/map/PNG — `build/g14/`; ROM32861724 байта, SHA-256
`9b4a9a28634fa5dbcb6ce15e74acf681c8bae7c91349c7da86f705d3b7e62926`.
EWRAM247800/free14344; IWRAM23980, до8KiB stack reserve596 байт.
Main stack2360/margin5576. Новые функции AI — ROM. Static renderer regression
4668231…4690291 cycles;30FPS не достигнуты, оптимизация G20.

Команда: `powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build-gba-ai.ps1 -CrossChecks -OriginalEvidence`.
Части выполнены отдельно; `analysis/g14-review.json` связывает source/ROM,
Ghidra evidence и проверки. Физическая GBA/оригинальный BIOS, полный DOS,
Linux/CMake/MSVC execution не проверялись. G15 не начиналась.

## Текущая граница G13

Обновлено 26 октября 2026 года: **G00–G13 готовы, следующий ID — G14**.
G14 не начиналась. Оружие и снаряды подключены к общему GBA-ROM:
`src/combat.c`, `include/dok/combat.h`, `gba/combat_probe.c`; A стреляет,
новое одновременное L+R запрашивает следующее имеющееся оружие. Первое
появление и pending hide/show идут по simulation ticks. Ненулевого заряда
достаточно даже ниже стоимости, расход 14/4 происходит только после spawn.
Полёт, same-class immunity, стена/дверь/цель, урон, decay, POPAL/MIMO и
удаление событий реализованы. Полёт начинает с остатка, затем проходит
четверти клетки; контакт ещё держит flight image, эффект начинает с frame 1.

Спецификация/адреса/ABI/политики — [combat.md](combat.md). Native slots
просматриваются 0…255, первый NULL или flags 0x400 побеждает. LEVEL1 имеет
141 свободный номер. Кольцо с одним событием на projectile сохраняет native
cursor/removal skip и отложенную публикацию owner, не позволяет повторно
занять текущий удаляемый слот внутри callback. Slot record 40 байт.
Dynamic occupancy — отдельные 512 байт, без записи dynamic slot glyph в
static MMap dictionary. Borrowed collision overlay учитывает четыре угла;
он блокирует actor movement и снимается при cleanup. Перед уничтожением
его context нужен `dok_collision_overlay(0,0,0)`, production context статический.
Reclaim уже снимает старый visual owner; будущий AI consumer должен снять
свои события. Два экземпляра одного шаблона используют разные texture slots
256…511, а не общий template frame. Native Object 36/player 60/ARM runtime
33864 не выросли; прежние compact резервные поля не переиспользовались.

Для G14/G15 доступны `dok_combat_spawn(owner, template_glyph)` и notifications
WAKE/DAMAGE/DEATH/RECLAIM. Sleeping monster state 0 будится без вычитания
health; consumer обязан выполнить AI transition. Active damage применяет
SUB16/clamp, но combat не меняет hurt/attack/death state и не подменяет AI
visual completion. Hero death — G18, звук/HUD consumers — G16/G17.

Свежие Ghidra C/ASM/actor.bin/world.bin и manifests:
`research/evidence/combat/{ru,en}/`. EXE SHA до/после совпали. Прошли 20 исходных x86
readiness-векторов, 72 slots cases, 2 position-copy slices, 2012 damage-decay
векторов, 336 monster arithmetic cases; полный native scheduler closure ABI
в spawn не эмулирован и не объявляется проверенным целиком. Windows suites
и Linux/Thumb compilation прошли. ARM проверил оба оружия RU/EN, empty/low
charge, full/refusal/reuse, closed/open door/wall/monster, wake/damage/death,
effect frame 1/end, отдельные кадры двух эффектов по source RES pixel oracle,
production occupancy/clear, первое pickup→show, hide/show, реальные A/L+R и
20 естественно завершающихся выстрелов без объектов/событий/occupancy leaks.
Renderer/reset/язык/guards/stack, interactions и rational clock прошли.
1455-camera sweep не повторялся; проверены targeted static и instance paths.

ROM/ELF/map/PNG — `build/g13/`; ROM 32858612 байт, SHA-256
`5039eb8c4189474259127390b26742b305f9e9ce7f354fa584ea6fe668a466be`.
EWRAM 239416/free 22728; IWRAM 23980, до 8 KiB stack reserve 596 байт.
Новые холодные функции оставлять в ROM. Main stack 2376/margin 5560.
Startup/static regression 4668074…4689700 cycles, synthetic combat capture
до 11891103 cycles. Цель 30 FPS остаётся недостигнутой; оптимизация G20.

Команда:
`powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build-gba-combat.ps1 -CrossChecks -OriginalEvidence`.
Части выполнены отдельно, aggregate `analysis/g13-review.json` прошёл и
связывает финальные source/ROM/evidence hashes. Физическая GBA/оригинальный
BIOS, полная DOS-сессия, Linux/CMake/MSVC execution не проверялись.
Следующие входы — паспорт G14, combat notifications, collision, scheduler,
enemy parameters и свежие exports. Ниже — история предыдущих границ.

## Предыдущая граница G12

Обновлено26 октября2026 года: **G00–G12 готовы, следующий ID — G13**.
G13 не начиналась. Предметы/stats/inventory подключены к общему GBA-ROM:
`src/items.c`, `include/dok/items.h`, production callback перед zone/movement
в `gba/movement_probe.c`. Все626 оригинальных размещений на язык (TMap),
11 определений на уровень,29 реально размещённых видов/уровней. Положительное
пополнение при полном ресурсе оставляет предмет; отрицательные эффекты, ADD16
overflow перед clamp, max-charge increase+refill, ключи, карта и оружие работают.
Consume меняет только одну клетку, shared object/остальные glyph сохраняются.
Notifications готовы для G16/G17; gameplay не выводит текст и не играет звук.

Спецификация/адреса/ABI/policies — [items-stats.md](items-stats.md).
Реальные числовые эффекты/позиции RU/EN совпали, переводы и масштабы сохранены
раздельно. Balance manifest не менялся. Map flag занимает прежний player
reserved word+54; native это world+0C. Object36/player60/ARM world33864 не выросли.
Первое оружие выбирается сразу; duplicate refill/pending сохраняет текущий
выбор, а transition hide/show должен реализовать G13 вместе с оружием/снарядами.
Исходный код этой цепочки — свежий `research/evidence/items/{ru,en}/` C/ASM/actor.bin.

Ghidra read-only exports и EXE SHA-256 до/после совпали.5274 actual x86 stat
vectors и16 native item callbacks (перехват только HUD/sound) прошли.
Windows suites и Linux static/ARM Thumb compilation прошли. mGBA109 stat
boundaries,58 real item cases RU/EN всех3уровней,12 duplicate weapon cases,
shared glyph/repeat/notifications/full refusal прошли. Два visible production
pickup по416-tick motion hook удаляют один billboard и меняют978 pixels каждый.
Renderer/язык/reset/guards/stack, doors/sensors/conveyor/FINISH и rational clock
regression прошли. FCMap class12 используется только для100/102/1000:
неиспользуемый DOK_INTERACTION_STAT остаётся диагностическим seam.

ROM/ELF/map/PNG — `build/g12/`; ROM32851924 байта, SHA-256
`1a49c7694cc3ca1fbf7fa1439c7597f173c4c74224f4611c71489fd1a4884850`.
EWRAM218416/free43728; IWRAM23684, до8KiB stack reserve892 байта.
Новые cold functions в ROM. Main stack2376/margin5560 в renderer/reset;
startup4653188…4673215 cycles. Цель30FPS не достигнута, оптимизация G20.
Renderer не менялся;1455-camera sweep G11 не повторялся, targeted regression
достаточна для pickup integration. Visual clock/crop/scheduler G11 сохранены.

Команда: `powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build-gba-items.ps1 -CrossChecks -OriginalEvidence`.
Части реально выполнены отдельно, финальный `scripts/report_items.py` прошёл.
`analysis/g12-review.json` связывает source/ROM/evidence hashes.
Физическая GBA/original BIOS, полная DOS-сессия, Linux execution и CMake/MSVC
execution не проверялись. Следующие входы — паспортG13, inventory API,
weapon-parameters, positioned instances и visual playback G11.
Ниже — история предыдущих границ; она не отменяет текущий результат G12.

Уточнение плана 26 октября 2026 года: по указанию пользователя удалена G19
и её паспорт — сохранение/загрузка игры не входят в объём порта. Пользователь
указал, что оригинал их не поддерживал; прежнее требование не было обосновано
исследованием. Убраны связанные требования G17, G18, G20 и G21; G20 теперь
зависит от G18. Остальные ID сохранены. В этом уточнении менялась только
документация; актуальная граница реализации приведена ниже.

Обновлено 26 октября 2026 года: G00–G11 готовы по уточнённым критериям.

## Текущая граница G11

**Следующая задача — G12.** Спрайты предметов/врагов, software billboard,
перекрытия и визуальный scheduler включены в общий GBA-ROM. Есть16 positioned
instances для изображений снарядов/эффектов; их создание/движение/урон — G13.
Логика здоровья, атаки, смерти и удаления не заменяется completion анимации.
Движение без поступательной инерции, двери/сенсоры/conveyor/FINISH сохранены.

Спецификация/адреса/ABI/границы: [sprite-rendering.md](sprite-rendering.md).
Новые `src/renderer_sprites.inc`, `src/visual.c`, `include/dok/visual.h`;
renderer/runtime header, ROM source loader, GBA motion/render adapters,
tools/build/CMake и static-regression freeze обновлены. `visual_resource`
занимает прежний резерв **compact** word+14, Object36/ARM runtime33864
не изменились. Native+14 имеет другое значение — адресацию не смешивать.
Текущий crop class12/13 хранится отдельно под known bit8 в compact animation_a;
native counter+50 увеличивается только после конфигурации старого crop.
Known animation A bit1 этим не устанавливается; crop не выдаётся за native+20.

Visual production clock333/333/334 догоняет simulation ticks через416-tick
motion catch-up, с сохранением долга. Видимость для lease берётся из последней
рассмотренной сцены и сбрасывается при переходе уровня. Interval0 — один шаг
за visual quantum; положительные интервалы округляются до следующего quantum.
Raw scheduler сохраняет signed interval/unsigned deadline semantics, включая
−31072. `dok_visual_play` выбирает binding slot, публикует frame0, затем
loop или one-shot completion; native gameplay state не меняется.

Ghidra EXE hashes до/после совпали; проекты/логи/экспорты:
`research/evidence/sprites/{ru,en}/`, скрипты `InspectSprites.java`/`analyze-sprites.ps1`.
900 оригинальных x86 billboard-векторов прошли в Unicorn. Host1750
последовательностей/9714 deadline переходов/322 прохода отрицательных
интервалов,9 coarse/fine timelines и точные water crop/end/rearm прошли.
20 item/enemy faces и8 projectile projections сверены с независимым oracle
по исходным RES; ближние masked pixels и полная дальняя occlusion проверены.
mGBA:8 playback-последовательностей RU/EN,24 projectile views, видимые pixels,
automatic multi-step per draw, exact water crop-before-INC/end, guards/stack,
embedded package, язык/poison reset прошли. Все1455 камер без отказов;
door/sensor/conveyor/FINISH и rational time regression прошли. Static suites
замораживают visual timeline явно; её ход проверяет отдельный ARM-тест.

ROM/ELF/map/PNG — `build/g11/`. ROM32 850 956 байт, SHA-256
`231069467bf90b2021956ce0d5a00552424019b1dd3c2721dbce785236d3e493`.
EWRAM218 348, свободно43 796; IWRAM23 684, до8KiB stack reserve892 байта.
Новые функции размещать и измерять осознанно. Main stack2376, нетронутый
запас5560 в измеренных ARM-сценариях. ROM slack703 476 байт не резерв всей игры.
Startup4 651 246…4 668 836, worst camera6 251 069 cycles: цель30FPS не достигнута.

Команда: `powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build-gba-sprites.ps1 -CrossChecks -OriginalEvidence`.
Части выполнены отдельно; финальный sweep —1455 камер. Acceptance
`analysis/g11-review.json` связывает текущие source/ROM/evidence hashes,
остальные отчёты `analysis/g11-*.json`. Windows suites прошли; CrossChecks
скомпилированы. Физическая GBA/оригинальный BIOS, полная DOS-сессия,
Linux execution и CMake/MSVC execution не проверялись. Ниже — история G10
и предыдущих границ, её ограничения не заменяют текущий результат G11.

## Предыдущая граница G10

**Следующая задача — G11.** G10 подключил текстурные пол/потолок, перепады
потолка, LIGHT16 и три GAMEP к общей сцене G09. Движение без поступательной
инерции, коллизии, двери, сенсоры, conveyor и FINISH сохранены. Визуальный
sampler поверхности не запускает игровое событие.

Спецификация/ABI/адреса/границы: [surfaces-lighting.md](surfaces-lighting.md).
Новые hand-written `src/renderer_{surfaces,surface_raster,surface_sides}.inc`
включены в `src/renderer.c`; обновлены renderer header, ROM linker,
GBA startup light-cases, `tools/render_tool.c`, пакер/проверка row0-bank.
FCMap+54 — отступ потолка, floorY=−H/ceilingY=I−961. Отрицательный I
даёт один текстурированный пол: имя solid из G04 было предварительным.
Near=1920 подтверждён в G00 RAM обоих языков; RUS DS:0DFA/ENG DS:0EA2,
не универсальный +B0. Незакрытый фон теперь0, цветная диагностика G05 удалена.

Оригинальные инструкции: `InspectSurfaces.java`, `analyze-surfaces.ps1`,
`research/evidence/surfaces/{ru,en}/`, прежние Ghidra-проекты сохранены; SHA EXE до/после
совпали. На язык2065 quads/8260 вершин,6 quads с negative depth,42 synthetic
single-plane,10610 light-векторов C/native,144 crop-вектора прошли.
Constructor array-helper перехвачен; проекция и crop — нет. Не полный DOS-session.
Инвентарь всех шести FCMap и bindings — `analysis/g10-original-verification.json`.

Все1455 камер LEVEL1–3, HUD/палитра,443520 strips/linked payloads, time,
door/sensor/conveyor/finish регрессия прошли.32 ARM-вида воды в RU/EN,
четыре направления и фазы0/1/9/17: по32736 scene pixels совпали с host;
512 байт palette RAM сверены с каждым из шести исходных GAMEP.
13 Windows suites, Linux static/Thumb compile и общие ARM cases прошли.
Guards/poisoned reset и сохранность mutable world при языке проверены.

**Автоматический scheduler анимаций ещё G11.** В G10 sampler принимает
`object.frame`0…17, при обычном запуске вода остаётся в начальной фазе;
тест подаёт фазы вручную. Сдвиги делят размер на18 до умножения. Не привязывать
скорость анимаций к рисованию. Dynamic light near/боевые палитровые эффекты
не выдаются за готовую игровую механику. Полная попиксельная эквивалентность
DOS raster не заявлена: GBA clip/общие рёбра/front-to-back — политика порта.

ROM/ELF/map/PNG — `build/g10/`; ROM32 846 628 байт, SHA
`e9598753692371226d68213e135a31f7fb74f42a21ebd7a52d3587797fb23992`.
EWRAM202 556 (+58 884); IWRAM code22 888/data1232 — **456 байт до stack
reserve**, новые холодные функции размещать осознанно. Main stack2352,
нетронутый запас5584. ROM slack707 804 байта не резерв всей игры.
Startup4 486 080, worst6 104 796 cycles (LEVEL1 near-x, heading240).
Стоимость существенно выше G09: ранний бюджет400000 остаётся отложенным,
30FPS полной игры не подтверждены; новые визуальные упрощения не вводились.
Физическая GBA/оригинальный BIOS, новый полный DOS-session, Linux execution
и CMake/MSVC не проверялись.

Команда: `powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build-gba-surfaces.ps1 -CrossChecks`.
`-OriginalEvidence` добавляет Ghidra/Unicorn; общая библиотека Unicorn read-only
из `C:/Source/wasdify/build/research-deps`. Части команды выполнены отдельно,
итоговый полный sweep —1455 камер. `report_surfaces.py` связал ROM/исходники/
evidence в `analysis/g10-review.json`; остальные отчёты — `analysis/g10-*.json`.
G11 в этом проходе не начинался. Далее ниже — исторические границы G09 и ранее.

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

Ghidra-проекты/журналы/ASM/bytes: `research/evidence/interactions/{ru,en}/`,
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

Ghidra-проекты/журналы/ASM/байты: `research/evidence/collision/{ru,en}/`, скрипты
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
`scripts/InspectMovement.java`, `analyze-movement.ps1`, `research/evidence/movement/{ru,en}/`.
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
[дополнительный проход](gba-renderer-review.md). `gba-performance-g06a.md` помечен
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
  Ghidra exports/logs/manifests — `research/evidence/time/{ru,en}/`, проекты сохранены.
  SHA обоих оригинальных EXE до/после совпали. Новые DOS-запуски
  `research/evidence/time/dos-{ru,en}/` дали по 21 HTTP time samples, PIT period65536,
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
  в `research/evidence/reference/{ru-01,ru-02,ru-03,en-01,en-02}/`.
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
проекты `research/ghidra/projects/{ru,en}/Kremlin.gpr`, экспорт `decompiled/`.
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
