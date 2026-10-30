# Nintendo DS

Текущая итерация — [N22](../../docs/nds/tasks/N22.md) →
[N23](../../docs/nds/tasks/N23.md). N22 соединяет полный игровой цикл;
N22 принята (эмулятор): три обычных прохождения завершены, итоговый архив
прошёл4974 независимых SHA checks. Следующая задача — N23.
N23 передаётся в новый чат и **ещё не принята**: [актуальная передача](../../docs/nds/spec/N23-next-chat.md).
Последняя оптимизированная RU control — `build/nds/n23/candidate12-ru/dok_nds_gameplay.nds`;
её partial ordinary FPS прошёл порог, но natural61 и matching RU/EN matrix
остаются открытыми. C12 EN ещё не построена. N24 не начата, активных cores нет.
Проверяемые immutable-сборки —
`build/nds/n22/candidate-partial02-{ru,en}/dok_nds_gameplay.nds`, внешний пакет —
`build/nds/assets/dok-assets.ndspak`. Реализация, точные правила retry и команды —
[N22-flow](../../docs/nds/spec/N22-flow.md); статусы и следующий шаг —
[план](../../docs/nds/plan.md) и [handoff](../../docs/nds/handoff.md).

```powershell
py -3.14 -B -X utf8 ports/nds/tools/check_flow.py candidate-partial03-ru
py -3.14 -B -X utf8 ports/nds/tools/check_flow.py candidate-partial03-en
```

Эти команды создают следующий отдельный кандидат; существующий candidate label
driver повторно не собирает. Production/audit по умолчанию включают принятое
частичное перемещение VRAM (`N22_STREAM_PARTIAL_DEFRAG=1`); controls в старых
`frontend-{ru,en}` сохраняют прежний флаг0 и не являются итоговой сборкой N22.
Candidate01 сохраняет найденный CPU-cache NO_SPACE; candidate02 использует
проверенное [перераспределение памяти](../../docs/nds/spec/N22-cpu-cache-fault.md).

Обычный LEVEL1 RU на candidate02 полностью пройден: среднее 30,396 FPS,
минимум за ≥1 с 22,933 FPS в эмуляторе. Завершённый LEVEL2 EN — 26,767 FPS
в среднем, минимум 19,615 за ≥1 с. Завершённый LEVEL3 RU — 31,684 FPS
в среднем, минимум 19,942 за ≥1 с. Бюджет N23 ещё не пройден;
показатели ранних прототипов ниже не характеризуют полную игру.
[Покадровые замеры N22 и входы оптимизации N23](../../docs/nds/spec/N22-route-performance.md).

Пункты ниже описывают историю ранних этапов. Корневой `Makefile` этого порта
остаётся самостоятельной диагностикой N02; игровая сборка использует
`Makefile.n22` через указанный driver.

Python-инструменты запускаются общим `py -3.14`; внешняя зависимость x86-oracle
закреплена в `tools/requirements.txt`. Установка: `py -3.14 -m pip install --user
-r ports/nds/tools/requirements.txt` из корня. NDS не использует окружение,
библиотеки, игровые ресурсы или код из каталога соседнего проекта.

После N11 выполнена [N07P](../../docs/nds/tasks/N07P.md): ускорение вывода
и отдельные профили renderer/интерактивного probe/audit. Историческая ROM N07P —
`build/nds/perf/n11-optimized/dok_nds_interactions.nds` от корня репозитория;
старый `build/nds/n11/` — историческая принятая сборка.
На эмуляторе старты renderer — 59,83 FPS, диагностический маршрут N11 —
58,23 FPS в среднем, тяжёлый case — 46,93. Полное hash-аудирование каждого
кадра включается отдельно (`RENDER_AUDIT=1`) и даёт около 19,94 FPS.
Обычный и audit runtime/source verifier прошли; это не аппаратные DS FPS.
Команды и ограничения — в N07P; следующие задачи — по обновлённому NDS-плану.

Независимый порт для DS/DS Lite. [План](../../docs/nds/plan.md),
[handoff](../../docs/nds/handoff.md), [принятая N11](../../docs/nds/tasks/N11.md).

После N11: N01 завершена; N02–N11 готовы (эмулятор), полного игрового цикла ещё нет.
N02 создала диагностику платформы. Её исходники — `platform/diag/`, сборка —
`Makefile` и `tools/build_diag.ps1`, эмуляторные проверки —
`tools/run_diag_emulator.py`. Выходы — `build/nds/n02/`, доказательства —
локальный `research/evidence/nds/n02/`. SDK находится в общем пользовательском
каталоге, без отдельной копии в репозитории.

По поручению пользователя промежуточные задачи проверяются на эмуляторе,
реальная DS/DS Lite — на итоговой сборке. Это не подтверждение аппаратных FPS.
Игровых сохранений нет; на microSD записываются только настройки.

N03 также готова (эмулятор): строгие C11-контракты и bounded memory utilities
в `include/dok/` и `core/`, самостоятельные tests, `Makefile.n03` и
`tools/check_core.py`. Runtime memory probe — `tools/run_memory_emulator.py`.
Архитектура и бюджет — [N03](../../docs/nds/spec/N03-architecture.md).

N04 готова (эмулятор): независимый `tools/pack_assets.py`, потоковая читалка
`core/assets.c`, caller-owned cache/leases и SHA-256. Проверка всех RU/EN
ресурсов и сборки — `tools/check_assets.py`; FAT/DLDI runtime и отказы —
`tools/run_assets_emulator.py --core <общий libretro DLL>`. Внешний пакет —
`build/nds/assets/dok-assets.ndspak`, ROM — `build/nds/n04/`. Игровые данные
в ROM не встроены. [Контракт и результаты](../../docs/nds/spec/N04-assets.md).

N05 готова (эмулятор): `world.h/world.c` сохраняют полные LEVEL и строят
проверенные RU initial data; `balance.h/balance.c` применяют только принятые
39 изменений и отделяют localized refs/text. `tools/check_world.py` — host,
independent oracle и ARM9 build, `tools/run_world_emulator.py --core <DLL>` —
полная сверка шести экспортов из actual RAM. [Паспорт](../../docs/nds/tasks/N05.md).
Это ещё не исполняемый gameplay loop.

N06 готова (эмулятор): `projection.h/projection.c` задают камеру и точное округление
сцены 256×154; `tools/layout_ui.py` готовит HUD 256×38 и полные RU/EN страницы,
`tools/compose_visuals.py` — пары эталонных экранов. Общая host/ARM9 проверка:

```powershell
py -3.14 -X utf8 ports/nds/tools/check_visuals.py
```

`Makefile.n06` и `tools/build_visual.ps1` собирают отдельный bitmap viewer
`platform/visual/`: он читает подготовленные RGB555 через FAT/DLDI и меняет
пару экранов кнопкой A. Это просмотр эталонов, **не renderer N07** и не
работающий игровой HUD N19. Выходы — `build/nds/n06/`, пары — `frames/`,
первичные отчёты — `research/evidence/nds/n06/`.

`tools/run_visual_emulator.py --core <общий libretro DLL>` проверяет `happy`;
четыре дополнительных запуска используют `--scenario bad-index`, `bad-hash`,
`short-frame`, `bad-pixel`. Сравниваются все байты двух staging-кадров и все
отображённые RGB555 пиксели; повреждённая следующая пара сохраняет прежнее
изображение. Фактический итог запуска и ограничения — [паспорт N06](../../docs/nds/tasks/N06.md),
компоновка — [N06-ui](../../docs/nds/spec/N06-ui.md).

N07 готова (эмулятор): `render/geometry.c` строит поверхности по исходным
определениям, `render/scene.c` готовит immutable packet, `render/materials.c`
загружает полный статический набор уровня. `platform/render_probe/` подаёт
реальные 3D-полигоны с N06-проекцией; это ранний renderer, без игрового цикла.

```powershell
py -3.14 -X utf8 ports/nds/tools/check_renderer.py
$ndsCore = 'C:/Users/dspev/AppData/Local/Programs/melonDS-DS/1.4.0/melondsds_libretro-win32-x86_64-Release/cores/melondsds_libretro.dll'
py -3.14 -X utf8 ports/nds/tools/run_renderer_emulator.py --core $ndsCore
```

Первая команда проверяет исходную геометрию, host/Linux/ARM9 и собирает ROM;
вторая проходит 19 контрольных случаев на ARM9, включая движение камеры,
flat coverage, VRAM/RAM и VBlank history. Выходы — `build/nds/n07/`,
доказательства — `research/evidence/nds/n07/`. Внешние `assets.ndp` и `scene.dat`
не включены в ROM; fixture содержит камеры/HUD, runtime строит mesh сам.
[Контракт и ограничения](../../docs/nds/spec/N07-renderer.md).

N08 готова (эмулятор): `core/time.c` и `core/events.c` реализуют точные часы,
исходную очередность событий и явно описанные безопасные lifecycle-изменения.
`platform/time/` предоставляет SDK counter timer2/64; `platform/time_probe/`
проверяет общие C-тесты, одинаковые service timelines при разных render reads
и реальную паузу через Start. Новая фиксированная частота логики не вводится.

```powershell
py -3.14 -X utf8 ports/nds/tools/check_time.py
py -3.14 -X utf8 ports/nds/tools/run_time_emulator.py --core $ndsCore
```

Первый gate проверяет native x86 vectors, strict host/Linux/ARM9 и собирает
ROM; второй исполняет probe с независимым чтением clock/event traces.
ROM не требует игрового пакета/FAT. Банк событий36864B находится в N03 slab64KiB.
Same timeline не означает эквивалентность разного native polling;
полный gameplay loop ещё не реализован. Выходы — `build/nds/n08/`,
доказательства — `research/evidence/nds/n08/`.
[Паспорт](../../docs/nds/tasks/N08.md), [API/границы](../../docs/nds/spec/N08-time.md).


N09 готова (эмулятор): ordered input/controls сохраняет короткие фронты,
`motion` восстанавливает исходную инерцию, `camera` отдельно предсказывает позу.
Use X/B срабатывает при отпускании; UI поглощает held/release до нового действия.
`platform/movement_probe/` связывает настоящий SDK input с N08 events и N07 3D.
Диагностика пока free flight до N10, Use/fire/Select — команды до N11/N15.

```powershell
py -3.14 -X utf8 ports/nds/tools/check_movement.py
py -3.14 -X utf8 ports/nds/tools/run_movement_emulator.py --core $ndsCore
```

118511 общих host/ARM9 проверок, PKR/PKE oracle и 192 live PKR-conformant callbacks
прошли. Один короткий SDK R press/release полностью между callbacks сохранён.
При одной input/service timeline дополнительные30/60 camera observations не меняют
симуляцию; разные polling schedules эквивалентными не объявляются. Выходы —
`build/nds/n09/`, первичные доказательства — `research/evidence/nds/n09/`.
[Паспорт](../../docs/nds/tasks/N09.md), [архитектура](../../docs/nds/spec/N09-runtime.md).

N10 готова (эмулятор): `core/collision.c` восстанавливает square footprint,
скольжение, occupancy и door counters по PKR/PKE. Bounded actor transaction
отделена от pure native query; камера проверяется без записи в мир.
65280 исходных geometry comparisons,1152 wrapper calls и291 live callback
совпали; навигация проверена на трёх RU-уровнях. Полная door state machine — N11.

```powershell
py -3.14 -B ports/nds/tools/check_collision.py
py -3.14 ports/nds/tools/run_collision_emulator.py --core <путь-к-melondsds_libretro.dll>
```

Сборка `Makefile.n10`, SDK driver `tools/build_collision.ps1`, выходы
`build/nds/n10/`; постоянные доказательства `research/evidence/nds/n10/`.
[Паспорт N10](../../docs/nds/tasks/N10.md), [архитектура](../../docs/nds/spec/N10-collision.md).
Реальное железо проверяется на итоговой сборке N24.

N11 готова (эмулятор):
`core/interactions.c` восстанавливает Use/keys/OPEN/CLOSE/AUTO, сенсоры,
conveyor и FINISH output; `render/door_render.c` связывает phase snapshot с
crop/mirror, полностью открытая панель исчезает без полосы. Direct collision
API отдельно сохраняет исходное перемещение350 при C2=4, без quarter splitting.

```powershell
py -3.14 -B -X utf8 ports/nds/tools/check_interactions.py
py -3.14 -B -X utf8 ports/nds/tools/run_interactions_emulator.py --core $ndsCore
py -3.14 -B -X utf8 ports/nds/tools/verify_interactions_trace.py build/nds/n11/emulator/result.json build/nds/n11/emulator/snapshot-worlds.bin
```

`Makefile.n11`, `tools/build_interactions.ps1`; outputs — `build/nds/n11/`,
доказательства — `research/evidence/nds/n11/`. Старые N10 source bytes двух
расширенных файлов сохранены в N11 prior-source; будущие integrity audits
строятся от frozen N11 initial439 и новых N11 manifests, не от старого
recursive `check_time` collector. [Контракт](../../docs/nds/spec/N11-interactions.md),
[проверки](../../docs/nds/spec/N11-validation.md), [паспорт](../../docs/nds/tasks/N11.md).

84661 host checks,1601 shared ARM9 checks и992 live source operations прошли;
crop/mirror и полностью открытые двери просмотрены, сохранены60 PNG. ROM228352 B,
SHA`83ef8d4d629427febd96c0035ded9a255d4c93f908e43bbc8495ed9780e3e804`.
Компактный итог — [analysis/nds-n11-interactions.json](../../analysis/nds-n11-interactions.json).

По просьбе пользователя работа остановлена после N11; цель приостановить.
Следующий допустимый этап N12 остаётся не начатым до нового указания.
