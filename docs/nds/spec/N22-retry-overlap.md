# N22: исходное перекрытие героя и AI при retry

30 октября 2026 года отдельная интеграционная проверка обнаружила дефект
между точным death-helper и защитами NDS collision/AI. Native retry намеренно
перезаписывает MMap клетки старта glyph героя, даже если там стоит живой враг.
Отказывать в следующем callback этого врага нельзя: исходник допускает такое
временное перекрытие.

## Воспроизведение NDS

```powershell
py -3.14 -B -X utf8 ports/nds/tools/flow/check_retry_overlap.py
```

Fixture загружает настоящий LEVEL1 из NDS package вместе с animation bank,
полностью связывает `NdsGameplay` и реальный scheduler. В открытой тестовой
области живой враг ставится в клетку будущего retry. Штатный `dok_ai_hit`
будит врага; HP0 проходит `dok_gameflow_check` и `nds_gameplay_retry`; затем
исполняется настоящий scheduled AI callback. Это контролируемый host-тест
интеграции, не обычное прохождение и не подмена ввода в production ROM.

Baseline: retry1, callbacks2, AI callbacks1, callback status3, game fault3,
AI fault3. Враг glyph128, overwritten origin glyph77 (`M`, герой LEVEL1).
Программа отказала уже в `ai.c:walking`, до `dok_collision_begin`.

Baseline и четыре соответствующих исходника сохранены неизменно:
`research/evidence/nds/n22/retry-overlap-baseline/manifest.json` и зеркальные
пути `ports/nds/...` внутри того же каталога. Текущий отчёт после пересборки:
`research/evidence/nds/n22/host/retry-overlap.json`. Для проверки исправления:

```powershell
py -3.14 -B -X utf8 ports/nds/tools/flow/check_retry_overlap.py --expect-fixed
```

Эта команда требует минимум один реальный AI callback после retry и нулевые
callback/game/AI faults. Остальные старые guards и отрицательные случаи
проверяются дополнительно; один успешный overlap не разрешает чужие ссылки
или произвольные несогласованные world maps.

## Проверка оригинального поведения

```powershell
py -3.14 -B -X utf8 ports/nds/tools/flow/audit_retry_overlap.py
```

Исполняется полный native active callback PKR/PKE по сохранённым Ghidra
сегментам N14 и согласованному N17 transcript. В отличие от прежнего N17
recording hook, map setter выполняется машинным кодом с реальными native
координатами, signed division, clamp и MMap store. Исходная клетка содержит
glyph героя77. В обоих языках наблюдаются строго **32 → 128**: стереть
текущее содержимое, затем восстановить собственный glyph врага. Сохранённый
N17 результат остальных state/frame/course/shot counters также совпадает.

| Действие | PKR | PKE |
|---|---|---|
| Активный AI callback | `1068:3845` | `1020:3FAE` |
| Безусловно очистить origin | `1068:3A40…3A55` | `1020:41B5…41BE` |
| Восстановить собственный glyph | `1068:3D39…3D48` | `1020:44A2…44B1` |
| Native map setter | `1068:1512` | `1020:1A1C` |

Адреса — логические адреса NE-загрузчика Ghidra, не файловые смещения и не
runtime DPMI-селекторы. SHA образцов до/после совпали с реестром PKR/PKE.
Полный отчёт: `research/evidence/nds/n22/original/retry-overlap.json`.

В этом oracle контролируются front/sight lookup, анимационные accessors,
heading/collision trial, N09 movement boundary, таймеры, звук, UI и allocation.
Подлинные active control flow, map setter и far-copy helper исполняются.
Это не полная DOS-сессия и не доказательство физической DS.

## Следствие для восстановления

Исходник не читает прежний origin glyph перед очисткой и не восстанавливает
его после служебного запроса. Он восстанавливает **свой** glyph из actor+5C.
При временном overlap этим можно перезаписать героя до его следующего
callback; поза и жизнь героя при этом остаются в объекте. Поэтому сохранение
`old_cell` допустимо в обычном согласованном состоянии, но неверно в специально
допущенном native respawn-overlap.

Исправление должно разрешать только доказанное перекрытие конкретной клетки
и полных actor references, сохраняя foreign/epoch/lifetime guards для остальных
случаев. Нужны проверки следующих AI/hero callbacks, движения, sight/course,
смерти/снарядов и очистки такого разрешения при расхождении или удалении мира.
Статус самого исправления фиксируется в основном результате N22.

## Программная проверка исправления

Узкое исправление проверено отдельно от приёмки всего N22. Collision хранит
одну respawn-клетку и 256 бит индексов seed; право при каждом обращении
проверяется через полный актуальный actor reference и epoch мира. Retry
отмечает героя и class9 seeds, фактически находящиеся в клетке появления.
Course/walking при таком перекрытии восстанавливают собственный glyph;
dying сохраняет исходный выбор TMap/MMap для тела. Обычные foreign-origin
и lifetime guards остаются действующими.

Расширенный `--expect-fixed` проверяет героя и двух class9 в одной клетке,
два retry с сохраняющимся overlap, реальные walking/course callbacks и оба
варианта corpse transfer через настоящую dying-анимацию. Всего выполнено
40 реальных AI callbacks. Дополнительно проверены hero begin/query/commit,
camera и direct displacement, сохранение права после same-cell/rejected move,
снятие только у покинувшего клетку actor, отказ повторного входа через
чужую origin, отсутствие повторной выдачи права при обычном возвращении,
неотмеченный actor, другая клетка, чужой epoch и stale generation. Отрицательные
случаи проверены для begin, camera и displace.

Public `dok_ai_sight` — явно объявленный read-only source probe. Он временно
очищает origin для запроса и восстанавливает **прежний** байт, в том числе при
overlap; весь `dok_initial_world` до/после сравнивается побайтово. Это отличается
от служебного участка реального walking/course, где native callback обязан
восстановить собственный glyph. Контракт public probe не менялся.

Общий host gate `ports/nds/tools/flow/check_host.py` также переведён на
действительный `dok_collision_bind`, согласованные definition/seed references
и карты. Сохранились полный WORD-domain и побайтовые проверки motion,
inventory/projectiles/world/visited/scheduler. Новые overlap metadata
сравниваются отдельно заданным ожидаемым bitmap, а не исключаются из проверки.
Host gate прошёл 655 929 проверок. Это программные доказательства узкого
исправления, не полное прохождение уровней и не аппаратная приёмка.

## Отдельная ARM9-проверка

Тот же substantive C-тест собран отдельной fixture через
`Makefile.n22overlap`. Production main, `Makefile.n22` и движок для этого
не изменялись. ARM9 wrapper открывает FAT и вызывает переименованный test main
с `fat:/dok/assets.ndp`; результат публикует из фактических счётчиков теста.
При отказе публикуются status3 и номер строки. Публикация защищена seqlock;
Python наблюдает только согласованные записи, не пишет RAM/VRAM/PC.

```powershell
py -3.14 -B -X utf8 ports/nds/tools/flow/build_overlap_arm9.py
py -3.14 -B -X utf8 ports/nds/tools/flow/run_overlap_arm9.py
```

Build использует общий BlocksDS 1.24.0, GCC 16.2.0 и Binutils 2.47, проверяет
весь package lock `ports/nds/toolchain_versions.json`. ARM7 — закреплённый
`build/nds/n18/arm7/dok_audio_arm7.elf`; копия toolchain не добавлялась.
Служебные Win32 signal pipes MSYS2 требуют запуска build вне sandbox.

Actual ARM9 PASS: **160 checks, 40 AI callbacks, два повторных retry и два
варианта corpse transfer**, все callback/game/AI faults равны нулю. Получено
на 709-м кадре libretro в DS mode (`isDSiMode=0`). `dok_collision_state`
занимает 1340 байт на ARM9, новый bitmap — 32 байта и отдельный cell WORD32.
Наблюдение FAT после завершения не обнаружило изменений логических файлов.
Число кадров здесь — длительность выполнения теста, не игровой FPS.

| Артефакт | SHA-256 |
|---|---|
| `.nds`, 179712 байт | `78d890f9918bcb43af958d704227b909cd97c1b7cc6b5e514b273bfc4f13f798` |
| `.elf` | `614a9c195e6740ecd2cf945e4e0f0f414f95a19e10a5601daff692508628a0af` |
| `.map` | `30db8b1c49cd8c883d3c529778ffc4f931aff18cb59e658f3c0050c052c22e4c` |

Стенд: melonDS DS libretro 1.4.0, software renderer, JIT/threaded renderer
отключены, встроенные BIOS/firmware replacements; core SHA
`c7e6b58c667075a2a9868f2c3ba7a98e24200410a6da3051a7c555f1f0eef8ef`.
Полные options, адрес probe, ROM/ELF/source hashes и FAT audit находятся в
`research/evidence/nds/n22/overlap-arm9/emulator.json`. Там же сохранены
`probe.bin`, контрольные PNG, build manifest, 83 snapshots входов сборки
и 12 snapshots реально загруженных Python-модулей observer.
Компактный итог — [analysis/nds-n22-overlap-arm9.json](../../../analysis/nds-n22-overlap-arm9.json).

Это отдельное доказательство ARM9-интеграции новых overlap metadata.
Контролируемая fixture не считается обычным прохождением LEVEL1–3,
проверкой production sound/map rendering или физической DS/DS Lite.
