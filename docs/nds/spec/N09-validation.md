# N09: проверка ввода и движения на ARM9

Диагностика связывает реальный SDK keypad/touch, N09 input buffer и control
latch, N08 world clock/events, исходную кинематику и N07 3D renderer. Это
free-flight маршрут LEVEL1 до N10: стены ещё не останавливают игрока. Fire,
Use и выбор оружия публикуют команды; их игровое исполнение относится к
N11/N15. Нижний экран временно содержит диагностический текст до карты N20.

## Наблюдение

[movement_probe.h](../../../ports/nds/platform/movement_probe/movement_probe.h)
задаёт два наблюдаемых объекта: probe288 байт и ring512×288 байт. Все поля —
little-endian uint32; signed значения хранятся как sign-extended bit patterns.
Запись сначала публикует volatile odd sequence и flush cacheline, затем payload,
затем even sequence и flush. Наблюдатель читает sequence/payload/sequence.
Каждый record имеет ordinal, slot `(ordinal-1)%512`; потеря записи — ошибка.
Кольцо читается по индивидуальной публикации records, включая записи новее
последнего probe.trace_total: общий probe обновляется после GPU/VBlank и может
отставать. Это позволяет внешнему маршруту вовремя увидеть реальный input poll
и подать отпускание, не изменяя CPU или гостевые данные.

Пишутся INPUT, CALLBACK, RENDER, PAUSE, RESUME, COMPLETE. Сохранены raw и
отфильтрованные кнопки/touch, исходный timestamp/sequence, команды сервиса,
полное motion state, elapsed/stopwatch boundaries, отдельная presentation pose,
world clock fraction/counter и тайминг dispatch. INPUT наблюдается после capture,
до возможной UI-границы; нажавший Start sample затем поглощается границей.
CALLBACK содержит точный `input_flags` перед motion и итоговое состояние.

`service_tick` — clock read перед motion; `watch_start` — read после motion,
равный commit timestamp камеры; первый elapsed0, затем uint32 разность
service_tick и предыдущего watch_start. RENDER содержит точный world timestamp,
по которому вычислена presentation pose. Отдельный raw platform counter может
быть прочитан позже; он не заменяет атомарный world-clock snapshot.

Отдельный короткий Fire tap начинается после наблюдаемого CALLBACK. R
удерживается до реально записанного SDK press, затем отпускается до SDK release.
Оба timestamp переводятся в native ticks независимо по counter/fraction и должны
строго лежать между началами двух сервисов. Следующий сервис должен содержать
оба сохранённых фронта и Fire, даже если при обработке кнопка уже отпущена.

## Внешний сценарий и независимые проверки

[run_movement_emulator.py](../../../ports/nds/tools/run_movement_emulator.py)
подаёт обычные libretro joypad/pointer callbacks, читает ARM9 main RAM и video.
Он не пишет RAM, clocks, результаты тестов и CPU state, не загружает savestate.
Сохранён неизменённый N02 transport; core — melonDS DS libretro1.4.0, SHA-256
`c7e6b58c667075a2a9868f2c3ba7a98e24200410a6da3051a7c555f1f0eef8ef`.
Настройки: DS mode, builtin firmware, direct boot, software rasterizer, JIT и
threaded renderer отключены, DLDI read-only, host sync выключен.

Маршрут привязан к наблюдаемому frame_id. Он проверяет все12 кнопок, сочетания
хода/поворота/бега/Fire, оба Use, короткие нажатия между сервисами, Select и touch.
Два раза Start ставит мир на паузу; Start долго удерживается, затем отпускается
и нажимается заново. R/X/B/touch удерживаются через UI-границу и возврат, потом
отпускаются и нажимаются снова. Удержание и поглощённое отпускание не создают
нового действия; следующее нормальное отпускание X/B создаёт один Use.

Проверяющий отдельно восстанавливает ordered button fronts, blocked-until-release
и сервисный latch из входных samples. По каждому CALLBACK он исполняет подлинные
PKR x86 motion/wrapper/trig инструкции из [N09-evidence](N09-evidence.md), задавая
фактические pre-flags и независимо проверенный elapsed. Сверяются все17 полей
результата, включая число подшагов. Clock проверяется точным рациональным
отношением `38181824/1994081929`; во время пауз ticks/fraction и callback count
должны оставаться неизменными.

Камера независимо вычисляется по двум завершённым observation snapshots,
кратчайшей дуге и bounded horizon416. Проверяется каждый RENDER; это чистая
extrapolation фактического движения, которая не меняет координаты симуляции.
Движение на 30/60 render schedules отдельно проверяет shared replay; реальные
DS FPS по функциональному эмулятору не заявляются.

Снимки сохраняются на старте, при движении, паузе, возврате и завершении.
В каждом все9728 HUD RGB555 pixels сравниваются с принятым N06/N07 packet;
верхняя сцена должна быть видима и измениться при движении. RAM budget замыкается
до байта: region3670016, managed2293760, reserve655360, startup≤524288,
SDK headroom≥196608. Проверяются ошибки renderer и обычные4MiB DS, не DSi.

Прежде запуска требуется passed `research/evidence/nds/n09/host-check.json`:
пины всех исходников и ARM9 ROM/ELF проверяются до/после. FAT содержит только
принятый N04 package и точный N07 scene.dat. Core может инъецировать копию ROM
при direct boot; после load FAT должен оставаться неизменным до unload включительно.

```powershell
$ndsCore='C:/Users/dspev/AppData/Local/Programs/melonDS-DS/1.4.0/melondsds_libretro-win32-x86_64-Release/cores/melondsds_libretro.dll'
C:/Source/wasdify/.venv/Scripts/python.exe -X utf8 ports/nds/tools/run_movement_emulator.py --core $ndsCore
```

Текущие результаты — `build/nds/n09/emulator/result.json` и PNG рядом.
Окончательные первичные доказательства архивируются в `research/evidence/nds/n09/`;
повторные ROM/FAT не копируются. Численные результаты принимаются только после
завершённого прогона; этот документ описывает проверку, а не заранее заявляет PASS.
Физические DS/DS Lite и flashcart остаются на N24 по решению пользователя.

## Зафиксированный результат

Окончательный прогон: **PASS, exit 0**, 1173 frontend frames. Получены все 1085
records, 444 input samples и 444 представленных кадра, включая 400 активных.
Все 192 live callbacks совпали с исходными PKR-инструкциями; проверено 441
предсказание камеры и 1084 перехода точных рациональных часов. ARM9 выполнил
те же 118511 shared checks, что host: input 99038, motion 19001, replay 472.
Ошибок renderer/matrix и отказов тестов нет; все 12 кнопок и touch наблюдались.

Короткий R tap: service ticks 20495 и 21165, press/release source ticks 20829 и 21164.
Оба фронта произошли до следующего callback, который получил оба сохранённых
фронта и Fire. Две паузы длились по 44 VBlank; world ticks 38930 и 76974, fraction
и число callbacks сохранились при соответствующем возврате. Четыре Use
сработали по отпусканию, Select — один раз. Итоговая поза симуляции:
`(13413,-300,21726), heading 68`.

RAM: startup 327680, managed 2293760, SDK headroom 393216, reserve 655360,
сумма 3670016 байт. Typed high-water: world 268704, scratch 105056, scene 184408,
assets 344064; events bank 36864. Все пять наборов снимков сохранили 9728 точных
HUD RGB555 pixels; сцена меняется с камерой. На снимках паузы и завершения
видны PAUSED/COMPLETE. Диагностический текст обновляется в несколько операций;
отдельный снимок может захватить промежуточную перерисовку нижнего экрана.
Окончательный игровой UI относится к N19–N21.

`result.json`: 4151418 байт, SHA-256
`36087174e894855a6f1041a80b881dd71f190075372b3187c02bac4f1bcd6aa5`.
ROM 222208 байт, SHA-256
`90ab2eccf3d82bd0c1e6188e84ed120ce2d4387f826f680924abe28ab5061a75`;
ELF SHA-256
`1abb4e8342656aaf4f035a5079a1b140e190e44e3fb2070c0683dcd73c0b73d5`.
FAT SHA-256 после load и после unload одинаков:
`6769e92afd704c741e5b6a0da29d24f3dbee97222c65f4f287bb2bc2f59e688d`.

Постоянный архив `research/evidence/nds/n09/emulator/` содержит 22 файла:
result, журнал, пять наборов PNG и matching manifests/reports. Его
`archive-manifest.json` имеет SHA-256
`996e4b4803d5bb7a7b7e4f8af6223ba3745331cda13bb471d7bbafcb35536940`.
ROM/ELF/map остаются только текущими выходами сборки, FAT не дублируется.

Сохранены и непринятые попытки в `research/evidence/nds/n09/experiments/`:
первый функциональный PASS со старым LIVE-текстом при паузе; focused tap без
нужного покрытия из-за задержанного чтения общего probe.trace_total; PASS
с корректным tap, но ошибочным ANSI home консоли. Наблюдатель исправлен на
индивидуальные ring seqlocks, консоль — на SDK consoleSetCursor. Ни один из
этих опытов не подменяет окончательный ROM и финальное доказательство.
