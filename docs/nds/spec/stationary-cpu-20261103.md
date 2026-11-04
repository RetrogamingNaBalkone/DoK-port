# N23: периодические 29,9 FPS при неподвижном герое

Продолжение 3–4 ноября 2026 года по скриншотам пользователя: LEVEL1,
низкий проход/потолки разной высоты, соседняя комната, мухоморы и череп.
Контроль — `mapcache31-ru`, кандидат — `discovery32-ru`.

## Наблюдение в пользовательском melonDS

Для локального процесса melonDS (FileVersion/ProductVersion 1.1) прочитаны
опубликованные ARM9 probes через Windows `OpenProcess(0x410)` и
`ReadProcessMemory`. Записи в процесс, нажатия кнопок, инъекции, смена ROM,
пауза и сохранение игры не выполнялись. MainRAM оказалась MEM_MAPPED;
первые проходы поиска только MEM_PRIVATE не нашли probes. Ошибочные поиски
сохранены как промежуточная диагностика, а не успешные замеры.

MainRAM base для symbols `mapcache31-ru` выровнен на страницу. Renderer/map/
frontend magic и ABI совпали; выдуманные bases других ELF отсеяны по выравниванию.
Зафиксированный ракурс: world X=23133, Y=-300, Z=23900, heading233.
Это позднейшая позиция пользователя у мухоморов; она не выдаётся за точную
позицию первого скриншота с низким проходом.

В 80 прочитанных profile snapshots:

| Стадия | Среднее, мс | Минимум, мс | Максимум, мс |
| --- | ---: | ---: | ---: |
| CPU render_one | 17,646 | 16,315 | 22,950 |
| Simulation | 2,012 | 0,738 | 4,935 |
| Geometry prepare | 4,368 | 4,368 | 4,370 |
| Sprites, включая discovery | 4,630 | 4,630 | 4,633 |
| Discovery, входит в sprites | 3,327 | 3,327 | 3,329 |
| Submit CPU + короткий GE wait | 3,578 | 3,575 | 3,581 |
| Нижняя карта целиком | 1,552 | 0,558 | 4,512 |
| Raster нижней карты, входит в map | 0,016 | 0 | 0,121 |

В прочитанной истории 250 интервалов: 246 по 2VB и 4 по 1VB; 221 submitted
quad, geometry FIFO wait до0,001164мс. Это чтение running emulator, не DS.
FIFO wait не является полным измерением времени аппаратного rasterizer.
Часы игры и завершённые scene intervals сами показывают CPU-кадры за бюджетом
одной развёртки (~16,715мс), что достаточно для объяснения пропуска VBlank.
Повторяющаяся дополнительная simulation-нагрузка согласуется с эпизодами
просадки; конкретные AI callbacks отдельно этими snapshots не разделялись.

Исходный live observer проверял even profile sequence одним чтением, без
второй проверки seqlock; отдельные snapshots могли пересечь publication.
Поэтому это диагностическое наблюдение, а не строгий покадровый causal replay.
Текущий observer проверяет sequence повторным чтением renderer/profile/history.
Повтор с этой защитой не выполнен: пользовательский процесс к тому моменту
завершился (`OpenProcess`, Win32 error87). Процесс не перезапускался.

## Изменение

`dok_map_discovery_update_cached` повторно использует только проекции
статичных world geometry records при неизменных camera/world epoch/definition/
bindings/heading/runtime flags. Это не кэш итогового visited и не пропуск AI.
Каждый вызов заново вставляет актуальные sprite records в исходном порядке
TMap/MMap внутри клетки, сортирует общий список и выполняет исходный
nearest-to-farthest проход раскрытия 320 колонок.

Cache разрешён только при принятом N23_STATIC_GEOMETRY=1: неизменность базовой
геометрии при pickup/corpse writers уже доказана в N23-simplification.
При смене уровня NdsMapRuntime очищается. Смена camera/flags/epoch/bindings/
definition требует повторного расчёта. На overflow256 записей текущий вызов
выполняется полностью, а cache остаётся invalid: усечения результата нет.
При ошибке построения cache также не публикуется как valid.

ARM9 cache занимает3644байта, NdsMapRuntime —11852байта (DWARF фактического ELF).
Хранилище — существующий свободный scene tail после UI, static assert сборки
проверяет границу262144байт. Slab, reserve655360 и размер SDK heap не менялись.
Host64 cache3656байт из-за указателей; это не ARM-размер.

## Проверки

Host differential:18432 updates на всех трёх исходных уровнях, обычная и
принятая static geometry; сравниваются status, stats, порядок geometry/sprite
records, sort order и все4096visited bytes с frozen `mapcache31-ru`.
Движущиеся sprites, обе исходные planes, synthetic IDs, NULL sprites/flags,
смена flags/epoch/camera, явная invalidation и принудительный overflow8 PASS.
Дополнительно прежние raster/dirty-map/button/lifetime проверки PASS.
Это differential proof относительно принятого NDS-кода, не новый анализ EXE.

Контрольная позиция достигнута обычными кнопками после25 клеток LEVEL1,
затем подходом к X23133/Z23900. За240 video frames без движения:

| ROM | Реальная достигнутая pose X/Y/Z/heading | FPS | Discovery, мс |
| --- | --- | ---: | ---: |
| mapcache31-ru | 23173 / -300 / 23929 / 236 | 48,061 | 3,167 |
| discovery32-ru | 23158 / -300 / 23869 / 241 | 59,826 | 1,340 |

Poses и ход AI различаются: это близкие ракурсы, не pixel-identical causal pair.
Конкретный пользовательский ракурс и эпизоды29,9FPS на его новом ROM пока
не перепроверены. Первые локальные sweeps других точек первой комнаты не
воспроизвели29,9; одна попытка остановилась в host turn_precise около358°
при цели0°, а не из-за ошибки ROM. Эти результаты не скрыты.

Live/LCD LEVEL1/2/3, pause, RU/EN, touch PASS на `discovery32-ru`.
Вращение и обычный LEVEL1-маршрут100клеток записаны в компактной сводке
`analysis/nds-stationary-cpu-20261103.json` вместе с SHA поставки.
Полные материалы — `research/evidence/nds/n23/ceiling-20261103/`,
immutable ROM/ELF/map/sources — `research/evidence/nds/n23/candidates/discovery32-ru/`.

```powershell
py -3.14 -B -X utf8 ports/nds/tools/performance/check_static_discovery_cache.py
py -3.14 -B -X utf8 ports/nds/tools/performance/check_map_arrow.py
# build('discovery32-ru', features из mapcache31-ru/build-manifest.json)
py -3.14 -B -X utf8 ports/nds/tools/performance/check_stationary_room.py --target build/nds/n23/discovery32-ru --cells 25 --point 23133 23900 --frames 240 --angles 233 --run-id user-mushrooms
py -3.14 -B -X utf8 ports/nds/tools/run_joint_frontend_emulator.py --target build/nds/n23/discovery32-ru --language 1 --mode live --levels 1 2 3 --steady-presentations 90
py -3.14 -B -X utf8 ports/nds/tools/performance/check_turning_emulator.py build/nds/n23/discovery32-ru
py -3.14 -B -X utf8 ports/nds/tools/flow/inspect_ai_layout.py --elf build/nds/n23/discovery32-ru/dok_nds_gameplay.elf --output build/nds/n23/discovery32-ru/ai-layout.json
py -3.14 -B -X utf8 ports/nds/tools/performance/playtest_probe.py --target build/nds/n23/discovery32-ru --level 1 --cells 100 --video-budget 10000 --ai-layout build/nds/n23/discovery32-ru/ai-layout.json --run-id stationary-regression
py -3.14 -B -X utf8 ports/nds/tools/package_rom.py --target build/nds/n23/discovery32-ru
py -3.14 -B -X utf8 ports/nds/tools/performance/report_stationary_cpu.py
```

Emulator runner — pinned melonDS DS1.4.0 software core, DS mode. SDK Wonderful/
MSYS2 и host Zig0.13.0 — общие установки; Python3.14. Все большие stdout —
через run_logged.py в `.log.gz`. Оригиналы, GBA/SDL2, баланс и периоды AI не
менялись. Игровые saves/microSD writes не добавлены.

N23 не принята, N24 не начата. Следующий ID N23: повтор stationary-сценария
на пользовательском melonDS с новым ROM и профиль остальных CPU пиков.
Стабильные60FPS всей игры и физическая DS пока не подтверждены.
