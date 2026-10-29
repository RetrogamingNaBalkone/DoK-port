# Сборка первых модулей и VS Code

Для текущих NDS-инструментов используется общий Python 3.14 из пользовательской
установки, команда `py -3.14`. Зависимости ставятся в общий user site:

```powershell
py -3.14 -m pip install --user -r ports/nds/tools/requirements.txt
```

NDS-анализаторам нужен Unicorn 2.1.4; пути к окружениям других проектов
в исполняемых NDS-скриптах отсутствуют. Пакет ресурсов, EXE и Ghidra-доказательства
читаются из DoK-port. Прежние отчёты/журналы сохраняют реальные исторические команды.

29 октября: после N11 выполнена [N07P](nds/tasks/N07P.md).
Актуальные NDS source/ROM pins и воспроизводимые команды находятся в этом
паспорте и [handoff](nds/handoff.md). Оптимизированный интерактивный прототип
собирается/проверяется `ports/nds/tools/check_render_performance.py --core <DLL>`
в `build/nds/perf/n11-optimized/`, полный hash-audit — той же командой с `--audit`
в отдельном `n11-audit/`. Используются прежние общие SDK и 20 package pins.
Исторические команды N07/N11 ниже относятся к прежним accepted hashes;
их frozen gates не предназначены для перезаписи исходных доказательств новой сборкой.

28 октября 2026: GBA-модули, тесты и драйверы перенесены в `ports/gba/`; host-выходы — в `build/gba/host/`. Корневой CMake подключает GBA host-модули; независимый вход — `ports/gba/CMakeLists.txt`. Корни независимых NDS/SDL2-направлений — `ports/nds/` и `ports/sdl2/`. Карта переноса — [паспорт](tasks/RESTRUCTURE.md).

## Nintendo DS: общий SDK и диагностика N02

29 октября 2026: установлен общий BlocksDS `1.24.0-1` с GCC 16.2.0 в
`C:/Users/dspev/.local/msys64/opt/wonderful`. Версии 20 пакетов закреплены в
[toolchain_versions.json](../ports/nds/toolchain_versions.json). Драйвер
загружает `wf-env` в дочернем UCRT64 shell; `BLOCKSDS` указывает на
`/opt/wonderful/thirdparty/blocksds/core`.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File ports/nds/tools/build_diag.ps1 -Rebuild
```

Выходы — `build/nds/n02/`. Две чистые сборки совпали по .nds/ELF/map;
все три эмуляторных сценария прошли. N02 готова с пометкой «эмулятор». Подробности,
GNU C11 только для libnds-платформы, команда melonDS DS libretro core 1.4.0,
очистка и ограничения — [N02-building](nds/spec/N02-building.md).
Аппаратная проверка итоговой игры перенесена пользователем в N24;
эмуляторные результаты не доказывают FPS настоящей DS.

## Nintendo DS: ядро и memory probe N03

N03 готова (эмулятор). Общие Zig 0.13.0 и BlocksDS используются из указанных
пользовательских каталогов; core/headers компилируются strict C11, SDK-facing
платформа — GNU C11. Независимая команда из корня репозитория:

```powershell
py -3.14 -X utf8 ports/nds/tools/check_core.py
```

Она выполняет Windows host tests, Linux cross-build и ARM9 probe build.
Эмуляторный запуск — отдельный `tools/run_memory_emulator.py` с core DLL.
Выходы — `build/nds/n03/`; архитектура, бюджет, точные команды и границы
измерений — [N03-architecture](nds/spec/N03-architecture.md).

## Nintendo DS: пакет и runtime N04

N04 готова (эмулятор). Независимый конвертер и oracle сверяют все ресурсы
PKR.RES/PKE.RES, затем выполняются host tests, Linux cross-build и ARM9 build:

```powershell
py -3.14 -X utf8 ports/nds/tools/check_assets.py
py -3.14 -X utf8 ports/nds/tools/run_assets_emulator.py --core C:/Users/dspev/AppData/Local/Programs/melonDS-DS/1.4.0/melondsds_libretro-win32-x86_64-Release/cores/melondsds_libretro.dll
```

Пакет — `build/nds/assets/dok-assets.ndspak`, отдельный ROM и FAT-образы —
`build/nds/n04/`. Источники, формат, точные SHA и ограничения —
[паспорт N04](nds/tasks/N04.md), [runtime](nds/spec/N04-runtime.md).
Пакет и исходные игровые payload в Git не включаются.

## Nintendo DS: RU-мир и профиль N05

N05 готова (эмулятор). Отдельный target использует тот же пакет N04 и общий SDK:

```powershell
py -3.14 -X utf8 ports/nds/tools/check_world.py
py -3.14 -X utf8 ports/nds/tools/run_world_emulator.py --core C:/Users/dspev/AppData/Local/Programs/melonDS-DS/1.4.0/melondsds_libretro-win32-x86_64-Release/cores/melondsds_libretro.dll
```

Первый драйвер выполняет Windows tests, независимую сверку всех LEVEL/полей,
Linux cross-checks и чистую ARM9-сборку. Второй сверяет полные actual RAM exports
шести комбинаций LEVEL1–3/RU–EN, без записи emulated RAM. Выходы — `build/nds/n05/`;
точные SHA/лимиты и границы готовности — [паспорт N05](nds/tasks/N05.md).

## Nintendo DS: проекция и эталоны N06

N06 готова (эмулятор). Проверка использует подготовленный пакет N04 и исходные DOS
контрольные кадры N06; команды их получения и ограничения —
[паспорт N06](nds/tasks/N06.md). Host/ARM9 gate готовит полные RU/EN страницы,
HUD, пары экранов и отдельный bitmap viewer:

```powershell
py -3.14 -X utf8 ports/nds/tools/check_visuals.py
# Только повторная сборка viewer из уже подготовленных входов:
powershell -NoProfile -ExecutionPolicy Bypass -File ports/nds/tools/build_visual.ps1 -Rebuild
```

Выходы — `build/nds/n06/dok_nds_visual.nds`, `.elf`, `.map`, `build-report.json`;
пары — `build/nds/n06/frames/`, полные UI-макеты — `build/nds/n06/ui/`.
Внешний `index.dat` содержит SHA подготовленных `vNNtop.bin`/`vNNbot.bin`:
по 98304 байта RGB555 на экран. Данные читаются через FAT/DLDI и не включены
в ROM. Viewer подтверждает предъявление эталонов на двух экранах, а не
аппаратный 3D renderer N07, работающий UI N19 или скорость полной игры.

Пять независимых эмуляторных сценариев:

```powershell
$ndsCore = 'C:/Users/dspev/AppData/Local/Programs/melonDS-DS/1.4.0/melondsds_libretro-win32-x86_64-Release/cores/melondsds_libretro.dll'
foreach ($scenario in @('happy', 'bad-index', 'bad-hash', 'short-frame', 'bad-pixel')) {
    py -3.14 -X utf8 ports/nds/tools/run_visual_emulator.py --core $ndsCore --scenario $scenario
    if ($LASTEXITCODE -ne 0) { throw "N06 emulator scenario failed: $scenario" }
}
```

`happy` предъявляет все подготовленные пары по обычной кнопке A. Остальные
проверяют повреждённый индекс, digest, короткий файл и сброшенный обязательный bit15;
при отказе следующего кадра прежний display сохраняется. Каждый report
записывается в `build/nds/n06/emulator/<scenario>/result.json`; сравнение
охватывает 196608 байт RAM и 98304 RGB555 пикселя пары. Итоговый PASS до
фактического запуска не предполагается. Реальная DS/DS Lite остаётся N24.

## Nintendo DS: ранний 3D renderer N07

N07 принята на эмуляторе. Требуются принятый N04-пакет и локальные исходные
доказательства N05/N06. Полная проверка сама готовит trig/diagnostic fixture,
проверяет native geometry/scene/merge, Linux cross-link, строгие ARM9 headers
и модули, 20 закреплённых SDK packages, затем собирает аппаратный renderer:

```powershell
py -3.14 -X utf8 ports/nds/tools/check_renderer.py
$ndsCore = 'C:/Users/dspev/AppData/Local/Programs/melonDS-DS/1.4.0/melondsds_libretro-win32-x86_64-Release/cores/melondsds_libretro.dll'
py -3.14 -X utf8 ports/nds/tools/run_renderer_emulator.py --core $ndsCore
```

Только SDK rebuild после подготовки входов:
`powershell -NoProfile -ExecutionPolicy Bypass -File ports/nds/tools/build_renderer.ps1 -Rebuild`.
Выходы — `build/nds/n07/dok_nds_renderer.nds`, `.elf`, `.map`, `scene.dat`,
`scene-manifest.json`; `generated/render_trig.inc` создаётся из математического
генератора с проверкой всех 722 исходных runtime words. ROM не содержит игровой
банк или baked mesh: ARM9 читает точный `assets.ndp` и строит поверхности.

В математических модулях renderer используется ARM mode, общий SDK остаётся прежним;
воспроизводимый Thumb experiment задаётся `make -C ports/nds -f Makefile.n07 RENDER_ARCH=-mthumb`
в общей MSYS2/wf-env среде. Старые принятые NDS targets не пересобираются.
Полные reports/PNG/history — `research/evidence/nds/n07/` после приёмки;
реальные FPS DS/DS Lite проверяются в N24. [Паспорт N07](nds/tasks/N07.md).

## Nintendo DS: часы и события N08

N08 принята на host/эмуляторе. Новый strict C11 clock/scheduler собирается
независимо от renderer и игрового пакета. Original-instruction oracle требует
целевых Ghidra exports; при их отсутствии сначала выполнить
`ports/nds/tools/export_time_evidence.ps1`. Общий SDK и инструменты прежние.

```powershell
py -3.14 -X utf8 ports/nds/tools/check_time.py
$ndsCore = 'C:/Users/dspev/AppData/Local/Programs/melonDS-DS/1.4.0/melondsds_libretro-win32-x86_64-Release/cores/melondsds_libretro.dll'
py -3.14 -X utf8 ports/nds/tools/run_time_emulator.py --core $ndsCore
```

Gate не пересобирает N01–N07: проверяет их pins, native vectors,
независимую rational модель, strict host/Linux/ARM9 и Rebuild только N08.
Linux binaries только связываются. Runtime читает ARM9 traces без записи RAM,
проверяет точный clock, порядок callbacks и pause/resume через реальный Start.
Outputs — `build/nds/n08/`, доказательства — `research/evidence/nds/n08/`.
Timer2 занят SDK counter; callbacks не выполняются в IRQ. Сохранений и FAT I/O
в probe нет. Реальная аппаратура остаётся N24. [Паспорт N08](nds/tasks/N08.md).

## История сборок GBA

27 октября: хранение исследований вынесено в `research/`; после уточнения
пользователя промежуточные сборки и повторы удаляются. Паспорт и границы —
[tasks/RESEARCH-STORAGE.md](tasks/RESEARCH-STORAGE.md), итоговый учёт —
`analysis/storage-cleanup-20261027.json`. Старые отчёты могут ссылаться на
удалённые выходы; для повторных проверок нужна новая сборка. Игровые статусы
не меняются; текущая UI01 и необходимые контрольные образцы сохраняются.


27 октября [G20C sparse posts](gba/gba-sparse-posts-trial.md) завершена отрицательно:
опытный ROM `artifacts/gba/DoK-port_sparse_posts_trial.gba` сохранён для сравнения,
основным остаётся2775. Сборки/ROM/ELF/map — `research/experiments/g20c-sparse-posts/` и
`research/experiments/g20c-sparse-repeat/`; команды измерения и collector — в документе опыта.

После [очистки 27 октября](gba/tasks/CLEANUP.md) промежуточные `.o` опытов G20A
и кэш Zig удалены. Их ROM/ELF/map, снимки исходников и отчёты сохранены.
`prototype_gba_bsp.py` и `research_gba_fps.py` теперь удаляют собственные
объекты после сборки; добавьте `--keep-objects`, если результат будет новой
базой или нужен для ручной перелинковки. Объекты базовых `select-menu-fix`,
`level1-fixes`, `g20` и пакет `build/gba/assets` оставлены.

Новые отдельные quality prototypes G20A для сравнения:
`artifacts/gba/DoK-port_hardware_rows96_prototype.gba` (97f0b3…57f81) и
`artifacts/gba/DoK-port_hardware_rows72_prototype.gba` (176969…8888a).
[Результаты96/72](gba/gba-row-quality-trial.md):96 экономит10–12% CPU,
spatial440 min11,95/11,95/19,91.72 грубее; универсальные15+ не доказаны.
Сборки воспроизводятся `ports/gba/scripts/prototype_gba_bsp.py` с variants `hw_rows96`
и `hw_rows72`; сборщик проверочных результатов `ports/gba/scripts/report_gba_row_quality.py`.
Основной128-строчный artifact ниже автоматически не заменялся.

Рекомендуемый отдельный аппаратный прототип G20A:
`artifacts/gba/DoK-port_hardware_cached_prototype.gba`, SHA2775bd…edaab,
15 938 936B. [Результаты и стоимость кадра](gba/gba-render-costs.md):
runtime turn12019,85/16,86/21,79 FPS, ходьба17,96/17,92/20,54 RU
(EN L1 18,10). Spatial440 minimum9,954583/9,954583/14,931875; общая цель открыта.
CPU128 строк растянуты в144;396+440 PPU кадров совпали с предыдущим e803 ROM,
полные suites/512 rectangle ARM/48cache cases прошли. Прежние ROM сохранены.
Воспроизведение: `ports/gba/scripts/prototype_gba_bsp.py --out research/experiments/g20a-hw-entity-final
--variants hw_v128_entity_rect source_control`; collector —
`ports/gba/scripts/report_gba_hardware_cache.py --copy-rom` после описанных QA-запусков.
Прежний [быстрый128](gba/gba-hardware-fast.md) остаётся отдельным артефактом.

Следующий [Mode2 ceiling experiment](gba/gba-lowered-ceiling-trial.md) оставлен в
`research/experiments/g20a-hw-lowered-v6/hw_v128_lowered/`, SHA4f96b7…44d80; он не выбран.
Сборка: `ports/gba/scripts/prototype_gba_bsp.py --out research/experiments/g20a-hw-lowered-final
--variants hw_v128_lowered source_control`. Статус/контроль —
`ports/gba/scripts/report_gba_lowered_trial.py`. Не подменять им рекомендованный ROM.

Начальный, ещё медленный этап этой ветки описан в
[gba-hardware-floor.md](gba/gba-hardware-floor.md). Изображения проверяются по
настоящему PPU video, а не чтением Mode4 bitmap. Исторические сборки ниже
остаются доступными для сравнения.

27 октября: отдельные [G20A-прототипы спанов/HUD/уступов](gba/gba-span-prototypes.md).
Текущий exact-ROM для сравнения — `artifacts/gba/DoK-port_optimized_prototype.gba`;
SHA `b36472a0…eb42501`, 9 954 008 B, 488 точных кадров, live7,17/7,47/8,53 FPS.
Пол/потолок4×2 — `DoK-port_floor_4x2_prototype.gba`, SHA `04dfe358…1b5d11`,
9 888 624 B, live11,95/9,95/14,93. BSP первого прохода —
`artifacts/gba/DoK-port_BSP_prototype.gba`. Изолированная повторная сборка:
`py -3.14 ports/gba/scripts/prototype_gba_bsp.py --out research/experiments/g20a-sides-rebuild --variants baseline sides_exact sides_coarse`.
Она выполнена, ROM воспроизведены побайтно. Замеры:
`ports/gba/scripts/benchmark_gba_prototypes.py`, aggregate — `ports/gba/scripts/report_gba_spans.py`;
все команды/ограничения в документе. Прежние готовые ROM не заменены;
15+ FPS пока нет. Банк масштабов отключён, исходные16 LIGHT rows сохранены.

Текущая правка управления: язык переключается только в меню; Select в игре
язык не меняет. Проверенная сборка `build/gba/select-menu-fix/`, готовый ROM —
`artifacts/gba/DoK-port_pre_alpha.gba`. Проверки UI/renderer/flow и SHA:
[gba-fps-hypotheses.md](gba/gba-fps-hypotheses.md#попутное-исправление-select).

Основной сохранённый ROM для запуска — `artifacts/gba/DoK-port_pre_alpha.gba`.
Он включает обновлённый HUD из `build/gba/level1-fixes/` и последующую правку Select:
уменьшенные offline иконки, центрированные однострочные сообщения, исправленную
открытую дверь. История HUD — [level1-fixes.md](gba/level1-fixes.md).
ROM не включается в Git; ELF/map и отчёты остаются в `build/`.

Актуальная сборка G20 и полный профиль без усечений:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File ports/gba/scripts/build-gba-performance.ps1 -CrossChecks
powershell -NoProfile -ExecutionPolicy Bypass -File ports/gba/scripts/run-gba.ps1 -Rom build/gba/g20/dok-diagnostic.gba
```

Полный driver выполнен одним вызовом; лог `build/gba/g20/g20-validation-final.log`.
ROM/ELF/map — `build/gba/g20/`, бюджет — `analysis/gba/gba-final-budget.json`,
таблицы/ограничения — [gba-performance.md](gba/gba-performance.md).
Для pixel oracle сохранять frozen G18 с SHA `46e4bfa4…eeb3c0`; текущая
пересборка G18 из изменённых исходников не заменяет его. Нужны общие
Zig/Python/mGBA и существующий Unicorn в `C:/Source/wasdify/build/research-deps`.
`-RegressionOnly` повторяет проверки неизменённых готовых артефактов.
30 FPS не достигнуты; G20 открыта по бюджету, следующий шаг — G20A.

Следующие описания G18 и первых проб — исторические команды соответствующих этапов.

После очистки 26 октября 2026 года старые сборки G06A, G07–G15 и G17 удалены;
для повторных проверок их нужно собрать заново. Итоговые ROM/ELF/map G18
сохранены. Промежуточные объекты и банки G16/G18 удалены, а общий пакет
`build/gba/assets/` оставлен. Полная сборка ниже восстановит промежуточные
файлы и кэш Zig. Перечень очистки: `analysis/artifact-cleanup-20261026.json`.

Общий GBA-ROM этапа G18:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File ports/gba/scripts/build-gba-flow.ps1 -CrossChecks -OriginalEvidence
```

Артефакты — `build/gba/g18/`, методика/границы — [game-flow.md](gba/game-flow.md),
проверка и измерения — `analysis/gba/g18-review.json`. Части pipeline выполняются отдельно,
aggregate связывает их результаты; полный вызов воспроизводит тот же порядок. Старые описания первых
статичных проб далее не обозначают текущее отсутствие движения/поверхностей/спрайтов.

Driver G18 собирает host flow/UI tests, Linux static/Thumb compile checks,
общий ROM и запускает original x86, аудит исходных выходов RES, ARM flow/UI,
renderer/interactions/time/28AI/23PCM
регрессии. `-RegressionOnly -CrossChecks` использует уже готовые build artifacts.
`DOK_PROBE_FAST_VIDEO=1` откладывает Python RGB conversion до PNG; консоль
продолжает настоящий rasterization. Actual UI PNG и их визуальное заключение
привязаны к ROM/SHA. Полный driver одним вызовом не повторялся.
Титульное меню требует A/Start; Start+Select ставит паузу, Start сохраняет Use.
Новая игра выбирает LEVEL1–3 и показывает BINTRO; Continue возобновляет сессию.
Завершение показывает EINTRO и возвращает выбор уровня, как исходный startup.
Между независимо выбранными мирами инвентарь не переносится. «Выход» показывает ENDTXT.
ROM32881620bytes, EWRAM248688, IWRAM20684;30FPS остаются G20.

Сейчас собираются читалка RES, декодеры изображений/анимаций/звука,
загрузчик LEVEL, подготовка мира, компактный runtime, адаптер native-состояния и тесты.
Приоритет — GBA: [позадачный план](gba/plan.md). G01 выполнена:
диагностический GBA-ROM собирается и запускается в mGBA;
[команды и платформа](gba/gba-platform.md). Общий GBA-ROM уже содержит движение,
коллизии, предметы, оружие, AI и игровой цикл G18. Исполняемого SDL2-движка пока нет.
Команды первых проб ниже исторические.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File ports/gba/scripts/build-gba.ps1 -Verify
powershell -NoProfile -ExecutionPolicy Bypass -File ports/gba/scripts/run-gba.ps1
# G02: ARM sizeof/map и выполнение синтетического reset компактного мира:
powershell -NoProfile -ExecutionPolicy Bypass -File ports/gba/scripts/build-gba.ps1 -MemoryProbe -OutDir build/gba/gba-memory -Verify
# G03: полный offline pipeline и единый двуязычный asset ROM:
powershell -NoProfile -ExecutionPolicy Bypass -File ports/gba/scripts/build-gba-assets.ps1 -CrossChecks
```

## Один общий компилятор

По предпочтению пользователя компилятор установлен один раз:
`%LOCALAPPDATA%/Programs/Zig/0.13.0/zig.exe`.
Копии toolchain в проекте нет; временный ZIP после проверки и переноса удалён.
Общий кеш компилятора находится в стандартном пользовательском каталоге.
Проект хранит исходники, настройки и результаты сборки в `build/gba/host/`.
Системный PATH не менялся.

Пакет получен с [официального сайта Zig](https://ziglang.org/download/):
`zig-windows-x86_64-0.13.0.zip`, SHA-256
`d859994725ef9402381e557c60bb57497215682e355204d754ee3df75ee3c158`.
Хэш проверен по официальному индексу перед использованием. Zig используется
как поставщик C-компилятора и системных библиотек; исходники остаются на C11.

Установленный Visual Studio 2026 содержит бинарные инструменты MSVC,
но проверка не обнаружила необходимых C-заголовков и Windows SDK;
`VsDevCmd` не дал работоспособную команду `cl`. Компоненты Visual Studio
в рамках этой задачи не изменялись.

## Windows

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File ports/gba/scripts/build-native.ps1
# Дополнительно проверить компиляцию под Linux и ARM7TDMI:
powershell -NoProfile -ExecutionPolicy Bypass -File ports/gba/scripts/build-native.ps1 -CrossChecks
# При другом расположении общего компилятора:
powershell -NoProfile -ExecutionPolicy Bypass -File ports/gba/scripts/build-native.ps1 -ZigExe C:/Tools/Zig/zig.exe
```

Скрипт собирает C11 с `-Wall -Wextra -Werror` и запускает десять тестовых EXE.
`build/gba/host/dok-res.exe --list originals/pkr/RUS/PKR.RES` выводит каталог.
Проверка двух реальных архивов и выгрузка всех блоков:

```powershell
py -3.14 tools/originals/analyze_resources.py
# После готового первого этапа анализа Ghidra:
powershell -NoProfile -ExecutionPolicy Bypass -File tools/ghidra/annotate-stage2.ps1
# Проверить и выгрузить анимации:
py -3.14 tools/originals/analyze_animations.py
powershell -NoProfile -ExecutionPolicy Bypass -File tools/ghidra/annotate-stage3.ps1
# Проверить и выгрузить звук:
py -3.14 tools/originals/analyze_sounds.py
powershell -NoProfile -ExecutionPolicy Bypass -File tools/ghidra/annotate-stage4.ps1
# Восстановить метки LEVEL в Ghidra и сверить определения и карты:
py -3.14 ports/gba/scripts/prepare_levels.py
powershell -NoProfile -ExecutionPolicy Bypass -File tools/ghidra/annotate-stage5.ps1
py -3.14 tools/originals/analyze_levels.py
# Вывести модель уровня (строки CP866 представлены в hex):
./build/native/dok-level.exe research/evidence/resources/ru/0018.bin
# Создать начальный мир из оригинального архива:
py -3.14 ports/gba/scripts/prepare_world.py
powershell -NoProfile -ExecutionPolicy Bypass -File tools/ghidra/annotate-stage6.ps1
py -3.14 tools/originals/analyze_worlds.py
./build/native/dok-world.exe originals/pkr/RUS/PKR.RES LEVEL1
# G02: конвертация/проверка всех шести миров без потери известности:
py -3.14 tools/originals/analyze_runtime.py
./build/native/dok-runtime.exe --layout
./build/native/dok-runtime.exe originals/pkr/RUS/PKR.RES LEVEL1
```

Для `analyze_resources.py` нужен только Python 3.11+ и стандартная библиотека.
Примеры используют общий Python; окружение соседнего проекта не требуется.

## VS Code

Установлено общее расширение `ms-vscode.cpptools` (Microsoft C/C++, 1.34.4).
`.vscode/settings.json` ссылается на общий компилятор; `.vscode/tasks.json`
добавляет задачу **DoK: build and test C modules**. Запуск — `Ctrl+Shift+B`.
Сборка и исполнение проверены через тот же PowerShell-скрипт; интерактивная
отладочная сессия в редакторе пока не настраивалась и не проверялась.

Расширение само не заменяет C-компилятор; это ограничение описано в
[документации Microsoft](https://code.visualstudio.com/docs/languages/cpp).
При переносе проекта на другой компьютер путь можно переопределить.
Настройки `.vscode` рассчитаны на Windows; исходники C и CMake не зависят от них.

## Linux и GBA

Для обычного компилятора C11 добавлен `CMakeLists.txt`:

```sh
cmake -S . -B build/gba/cmake
cmake --build build/gba/cmake
ctest --test-dir build/gba/cmake --output-on-failure
```

Сам запуск CMake в текущей среде не проверялся: CMake здесь не найден.
Проверены прямые команды компиляции тех же C-модулей. Получены статический
ELF x86_64 Linux для ресурсного инструмента и тестов native-состояния,
а также ARM7TDMI Thumb objects адаптера состояния, декодеров изображений
и анимаций/звука. Добавлены Linux-инструменты `dok-anim-linux` и
`dok-sound-linux`. Эти артефакты не запускались
на Linux/GBA. Эти compile-check objects не являются GBA ROM и не включены
в платформенную диагностику автоматически. Отдельный настоящий ROM с startup,
линковкой, видеовыводом и вводом добавлен в G01: [gba-platform.md](gba/gba-platform.md).
Полноценного игрового цикла пока нет.

Первый статический вид настоящего LEVEL1 (G05), единый RU/EN ROM:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File ports/gba/scripts/build-gba-renderer.ps1 -CrossChecks
```

ROM, кадры и map — в `build/gba/gba-renderer/`. Условия проверки и измеренная
частота≈1,64 FPS при цели 30 FPS: [gba-renderer.md](gba/gba-renderer.md).

В G01 выяснено, что Zig 0.13.0 с `-target arm-freestanding-eabi -mthumb`
в этой установке выдавал ARM-код; шесть native compile checks исправлены на
`-target thumb-freestanding-eabi -mcpu=arm7tdmi` и проверены повторно.
Прежние отчёты о compile checks не являются доказательством Thumb-кодировки.

G06: проба времени и scheduler, отдельно и внутри общего LEVEL1-ROM:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File ports/gba/scripts/build-gba.ps1 -TimeProbe -OutDir build/gba/gba-time -Verify
powershell -NoProfile -ExecutionPolicy Bypass -File ports/gba/scripts/build-gba.ps1 -RendererProbe -OutDir build/gba/gba-renderer -Verify
```

ARM-векторы, аппаратные таймеры и fixed steps:
[time-events.md](gba/time-events.md). Renderer build также запускает проверку G06.

Итоговые контрольные суммы оригиналов и артефактов, число сверенных ресурсов
и аннотаций сохранены в [stage2-verification.json](../analysis/originals/stage2-verification.json).

Проверка LEVEL, полей и ссылок: [stage5-verification.json](../analysis/originals/stage5-verification.json),
[описание профиля и ограничений](originals/levels.md).

Подготовка мира: [world.md](originals/world.md), [stage6-verification.json](../analysis/originals/stage6-verification.json).
Добавлены `dok-world-linux` и `world-arm7tdmi.o`; они только скомпилированы.

## Ранний renderer G06A

`powershell -NoProfile -ExecutionPolicy Bypass -File ports/gba/scripts/build-gba-early.ps1 -CrossChecks`
собирает текущую пробу с 120 колонками сцены и 240 пикселями UI, затем запускает
проверки. Функциональный отказ завершает команду ошибкой; превышение раннего
бюджета 400 000 cycles выдаёт предупреждение по решению пользователя.
Методика и отдельные команды: [gba-renderer-review.md](gba/gba-renderer-review.md).

## Ввод и движение G07

`powershell -NoProfile -ExecutionPolicy Bypass -File ports/gba/scripts/build-gba-movement.ps1 -CrossChecks`
строит общий ROM `build/gba/g07/dok-diagnostic.gba` и проверяет движение, реальные
кнопки, графику и время. По указанию пользователя поступательная инерция удалена;
коллизии ещё не подключены. Отдельные команды Ghidra/Unicorn, управление,
измерения и ограничения — [movement.md](gba/movement.md).


## AI combat G15

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File ports/gba/scripts/build-gba-ai-combat.ps1 -CrossChecks -OriginalEvidence
```

Общий ROM/ELF/map и кадры — `build/gba/g15/`. Driver собирает Windows host,
Linux static/Thumb checks, общий renderer ROM, при `-OriginalEvidence`
повторяет read-only Ghidra export с SHA до/после и native x86 slices,
затем ARM28 enemy variants и renderer/interactions/time regressions.
Части этого полного workflow выполнены отдельно с теми же аргументами;
`ports/gba/scripts/report_ai_combat.py` связывает результаты с итоговым ROM/source.
`-RegressionOnly` пропускает сборку; нужны готовые host/cross артефакты и
native evidence. Общие Zig/Python/mGBA остаются по путям, указанным выше;
копий toolchain в проекте нет.

Acceptance: `analysis/gba/g15-review.json`; source/ABI/матрица и ограничения:
[ai-combat.md](gba/ai-combat.md). EWRAM247872/free14272, IWRAM23980,
до8KiB stack reserve596 байт.30FPS остаются G20. Физическая GBA,
оригинальный BIOS и полный DOS gameplay не проверены.


## Nintendo DS: ввод и движение N09

Диагностическая сборка связывает новый timestamped input/controls, исходную
кинематику и отдельную camera history с принятыми N07 renderer и N08 clock/events.
Это free flight до N10; команды Use/fire/оружия ещё не исполняют игровые эффекты.
Use X/B срабатывает при отпускании, как consuming reader оригинала.

```powershell
py -3.14 -X utf8 ports/nds/tools/check_movement.py
$ndsCore = 'C:/Users/dspev/AppData/Local/Programs/melonDS-DS/1.4.0/melondsds_libretro-win32-x86_64-Release/cores/melondsds_libretro.dll'
py -3.14 -X utf8 ports/nds/tools/run_movement_emulator.py --core $ndsCore
```

Первый gate проверяет сохранность N01–N08, strict C11 host/Linux links, исходные
PKR/PKE инструкции и общие ARM9 suites; Rebuild выполняется только для N09.
Он копирует точные принятые N04 package и N07 trig/scene.dat, не регенерируя
предыдущие результаты. Нужны уже принятые локальные входы N04/N07 и сохранённые
Ghidra exports. Второй gate подаёт обычные кнопки/touch, проверяет живые callbacks,
камеру и UI suppression через read-only seqlock observations. ROM/ELF/map —
`build/nds/n09/`, постоянные доказательства — `research/evidence/nds/n09/`.
Общий SDK и 20 закреплённых пакетов остаются прежними; отдельная копия не нужна.
Политика аппаратной приёмки — N24. [Паспорт N09](nds/tasks/N09.md).

## Nintendo DS: столкновения N10

Диагностика соединяет N09 input/motion, N08 events/clock и N07 renderer с новым
collision/occupancy над N05 RU-миром. Проверяется навигация по трём уровням;
автоматическая смена диагностических уровней не является игровой кампанией.

```powershell
py -3.14 -B ports/nds/tools/check_collision.py
py -3.14 ports/nds/tools/run_collision_emulator.py --core C:/Users/dspev/AppData/Local/Programs/melonDS-DS/1.4.0/melondsds_libretro-win32-x86_64-Release/cores/melondsds_libretro.dll
```

Gate сверяет347 принятых pins N01–N09, выполняет host/native checks и чистую
ARM9 Rebuild через общий SDK/20 packages. Linux static targets только собираются.
Выходы — `build/nds/n10/`; package/trig/scene копируются из принятых N04/N07
без регенерации. Исходные Ghidra-экспорты должны существовать; команда экспорта,
точные версии, итоговые hashes и ограничения — [паспорт N10](nds/tasks/N10.md).
Emulator observer только читает ABI и подаёт физические frontend buttons/touch.
Архив — `research/evidence/nds/n10/emulator/`, компактный итог —
[analysis/nds-n10-collision.json](../analysis/nds-n10-collision.json).
Физическая DS/DS Lite проверяется в N24 на итоговом хеше.

## Nintendo DS: взаимодействия и двери N11

N11 — отдельный target `Makefile.n11`, `platform/interactions_probe/` и
`tools/build_interactions.ps1`. Он связывает source Use/keys/door events,
сенсоры/conveyor/FINISH output с N08 events, N09 controls/motion и N10 collision.
N11 добавляет прямой public displacement API для source conveyor350 при C2=4;
прежние N10 функции сохранены байтами и отдельно проходят регрессию.
Финальные gate и runtime после renderer-fix прошли; итоговый ROM228352 B,
SHA`83ef8d4d629427febd96c0035ded9a255d4c93f908e43bbc8495ed9780e3e804`.

```powershell
py -3.14 -B -X utf8 ports/nds/tools/check_interactions.py --pins-only
py -3.14 -B -X utf8 ports/nds/tools/check_interactions.py
$ndsCore = 'C:/Users/dspev/AppData/Local/Programs/melonDS-DS/1.4.0/melondsds_libretro-win32-x86_64-Release/cores/melondsds_libretro.dll'
py -3.14 -B -X utf8 ports/nds/tools/run_interactions_emulator.py --core $ndsCore
py -3.14 -B -X utf8 ports/nds/tools/verify_interactions_trace.py build/nds/n11/emulator/result.json build/nds/n11/emulator/snapshot-worlds.bin
```

Gate проверяет frozen439 dependencies и source supersession proof, strict host
suites, Linux static links, исходные PKR/PKE инструкции и ARM9 Rebuild через
общий SDK/20 packages. Linux binaries в Windows не исполняются. Нужны принятые
N04 package/N07 trig/scene bytes и свежие read-only Ghidra exports; экспорт:
`powershell -NoProfile -ExecutionPolicy Bypass -File ports/nds/tools/export_interaction_evidence.ps1`.
Fixtures копируются без регенерации. Старые N10 outputs не пересобираются.

ROM/ELF/map и временный runtime — `build/nds/n11/`; постоянные отчёты,
полные snapshots и кадры — `research/evidence/nds/n11/`. Offline verifier
только читает сохранённый trace и snapshots, повторяет source x86 comparisons;
он не запускает эмулятор. Точные команды, versions, hashes, scope fixtures
и ограничения — [паспорт N11](nds/tasks/N11.md).

После приёмки N11 работа остановлена по просьбе пользователя; N12
без нового указания не начинать. Для её будущего dependency audit базой служат
frozen N11 initial439 + принятый compact + новые source/build manifests;
старый recursive `check_time.preserved_inputs()` не учитывает обоснованное
расширение collision API. Реальное железо и аппаратные FPS остаются N24.
