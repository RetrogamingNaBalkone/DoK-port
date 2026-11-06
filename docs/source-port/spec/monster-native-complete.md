# SP12: полная native-сверка AI — 6 ноября 2026

Закрыта оставшаяся программная сверка LOS, выбора курса, обхода препятствий и связанного AI callback. Создан отдельный воспроизводимый стенд, который исполняет инструкции трёх оригиналов в Unicorn и сравнивает результат с настоящими `Monsters`, `World`, `Events` и движением SDL2. Полные материалы: `research/evidence/source-port/sp12-native-complete/`. Ручная игровая и аппаратная приёмка остаётся в SP18.

## Образцы и адресная система

SHA-256 EXE перед/после read-only Ghidra-экспорта и всех oracle-прогонов совпадают с SP01:

| Образец | SHA-256 |
|---|---|
| PKR.EXE | `12f78afc0eba05f76b5dc0bb99da38d7daa20585024c0ba958fe828aa3864d28` |
| PKE.EXE | `cce7bc5993d0734ad87e93db37a70b8b52faedfff14ac48d6e317e5ac996a37b` |
| OG.EXE | `7965e1d67128f196fbcf2bc449d97197f355c6b81cf043608fc9e01103ea043f` |

Ниже адреса Ghidra исходных программ, **не** файловые смещения и не runtime DPMI-селекторы. Ghidra ranges экспортированы `export_monster_ai_audit.py` через `ExportSourcePortMonsters.java`, `-readOnly -noanalysis`; SHA бинарных ranges проверяется по manifest. В Unicorn actor/world/runtime/math/animation перенесены в сегменты 5000/6000/7000/8000/9000 с сохранением исходных offset. Far-calls перенаправляются к экспортированным инструкциям; таблица объектов содержит нормализованные far pointers. Копирование структур C++ не используется.

| Исполняемый вход | PKR | PKE | Old Gold |
|---|---|---|---|
| Полный AI callback | 1068:3845 | 1020:3fae | 1ae1:3323 |
| LOS | 1068:1276 | 1020:1780 | 1ae1:0fe1 |
| Активация | 1068:3eca | 1020:4633 | 1ae1:3989 |
| Направление atan/table | 10c8:01c5 | 1058:01c5 | 26f6:0185 |
| Motion | 1068:0a85 | 1020:0f8f | 1ae1:0908 |
| Три substeps | 1068:0eb1 | 1020:13bb | 1ae1:0dc0 |
| World resolver | 1098:0901 | 1030:09e5 | 21b8:068f |
| Animation next | 1050:0054 | 1018:0f62 | 1aa1:0055 |
| Animation clamped | 1050:00df | 1018:0fed | 1aa1:00cc |
| Player virtual hit | 1068:1b10 | 1020:20c4 | 1ae1:1694 |

Animation binary берётся из SHA-проверенного `sp11-combat/<profile>/manifest.json`. Настоящие количества кадров, интервалы и параметры читаются из исходного RES; изображения заменяются только логическими идентификаторами для сравнения выбранного кадра. Таблица 101 угла дополнительно найдена в read-only RAM запущенного DOSBox всех трёх оригиналов: `angle-ram/summary.json`. PKR/PKE проверяют также предшествующие 722 trig-значения; для OG положение таблицы связано с data-anchor и offset bf02. Это отдельное наблюдение RAM, не ошибочное использование NE-селектора как linear address.

## Матрица и результат

Охвачены все **530 определений монстров**: PKR и PKE по LEVEL1/2/3 (64/64/105), OG LEVEL4 (64). Определения включают неразмещённые glyph; их arena-позы являются fixtures, а не заявлением о размещении в оригинальной карте. Семь исходных карт отдельно участвуют в map-сценариях.

| Прежний пробел / ветви | Сценарии и сравнение | Результат |
|---|---|---|
| Полный AI, выбор и смена курса | 20 случаев × 530: course до/на пороге, signed wrap, старый heading; burst до/после задержки и лимита; state1/2/3, melee первый/последний кадр, motion 832/10000/20000 | 10 600/10 600 strict |
| LOS и преграды | 48 случаев × 530: 8 направлений на открытой карте, стена, передняя стена, другой class9, class8, боковая полоса препятствий | 25 440/25 440 strict |
| Path и course trials | В полном callback выполняются исходные temporary pose / restore, world resolver, footprint и три substeps; arena и исходные карты; сравнение конечной позы, heading, course, MMap | Включены в 36 040 strict; 34 012 resolver и 81 725 footprint входов |
| Активация по дистанции | 11 границ Chebyshev distance × 530, включая равенство, оба знака и диагональ | 5 830/5 830 strict |
| Математика направления | 225 signed dx/dz-пар × 7 LEVEL, оси, ноль, ±1/2/99/100/101/1000/30000 | 1 575/1 575 strict |
| Ограничитель LOS | Пустая карта без рамки, герой вне направления: реальный цикл до лимита 5000, все 3 профиля | 3/3 strict; budget 10 000 000, проверен возврат |
| Смерть/анимация/отмена | 4 состояния × 530: первый death callback, state4 первый/последний/завершённый кадр | 1 798 strict + 322 явно принятых unsigned interval отличия = 2 120; неожиданных 0 |
| Повторные callbacks из проблемной исходной позы | 25 свидетелей × 2 режима × 12 callbacks, исходные карты и исходный spawn героя; в одном режиме на callback3 terrain становится passable | 50/50 trajectories, 600/600 callbacks strict после исправления |

В основном прогоне **36 040 strict**, адаптаций и неожиданных расхождений **0**. Реальные входы: AI10 600, LOS26 516 (включая вызовы из AI), atan10 869, next11 130, clamped932, motion8480, substeps8480. Полные построчные vectors и native/port значения находятся в `*-report.json`; итог `report.json`. Проверки требуют непустую матрицу, точное число строк C++ относительно входов, успешный код процесса, native return IP, баланс SP и отсутствие исчерпания instruction budget. Короткий stdout не может дать PASS через `zip`.

Поля строгого сравнения: x/y/z, heading, state, next frame, выбранный clip/frame, course counter, число выстрелов, motion acceleration, health героя после настоящего native hit, FNV карты MMap, количество запросов снарядов, интервал AI event и burst elapsed. Активация отдельно сравнивает state/frame/course/shots/event count/clock. LOS и atan сравнивают возвращаемый результат.

## Обнаруженное и исправленное расхождение движения

Старый host guard принимал только candidate с полностью свободной footprint, даже если исходная footprint уже блокировалась исходной картой. Native resolver делает alignment и из такой позы. Пример естественно размещённого PKR LEVEL1 glyph158: (3120,8400), terrain glyph73, class6 flags8, radius238/side480. Прямой native/C++ resolver одинаково выдаёт (3121,8401); обе footprint не свободны. Старый wrapper оставлял (3120,8400). За 12 callbacks возникало уже расхождение курса (native0, port315), поэтому это не принято как допуск 1px.

До исправления: 36 015/36 040 strict, 25 расхождений; `baseline-live-regressions.json`. Для trajectories 0/50 strict, в 6 случаях отличались также непозиционные поля. Из 50 trajectories 10 используют естественно размещённый glyph, остальные — явно сконструированную позицию неразмещённого определения на настоящей карте; provenance отмечен `source_placed`.

`movement.cpp` теперь вычисляет `origin_clear` после временного снятия собственного MMap. Для свободного origin сохраняется прежняя дополнительная footprint-защита; из уже блокированного origin разрешён исходный resolver candidate. Bounds и защита чужого MMap остаются. Новых полей состояния и изменений формата SP17 нет. Независимая SP10-регрессия на исправленном core: native35 399/35 399, ещё51 boundary/valid-origin/foreign-occupancy case, current-bank990 и pickup30/60/144 FPS — PASS (`research/evidence/source-port/sp10-post-ai-regression/report.json`, [отчёт движения](sp17-followup-movement-combat.md)). После: 36 040/36 040 strict и 600/600 callbacks. До/после сохранены отдельно, отрицательный witness не перезаписан.

## Контролируемые границы и принятые адаптации

Часы и внешний планировщик управляются fixture, возвраты audio/device calls и HUD redraw контролируются. Это не проверка DOS mixer; её выполняет независимый SP13 stateful native sound bridge. AI LOS, курс, world lookup, footprint, движение, atan, animation и virtual player hit **не заменены** формулами/stub. На границе создания снаряда записывается запрос; сам выбор атаки, курс, timer и счётчик исполняются native. Allocation/contact проверяет SP11. Это исполнение оригинальных функций, а не полный DOS playthrough и не доказательство идентичности рендера.

322 death-отличия — только интервал: исходный signed WORD расширяется до ffff8000..ffffffff, порт сохраняет 32768..65535. Это ранее принятое исправление длинного unsigned frame duration, не новая находка. Конечный кадр, состояние, позиция и отмена совпадают. Уже документированные раннее освобождение прозрачного хвоста трупа, защита чужой occupancy, немедленное сообщение об убийстве и независимая от renderer активация остаются сознательными решениями SP12; их специализированные регрессии описаны в `monster-audit.md` и паспорте, не выдаются за DOS equality.

## Воспроизведение

Prerequisites: оригиналы SP01, Ghidra read-only проекты, `sp10-movement` и `sp11-combat` exports/manifest, Python Unicorn, DOSBox-Automation для отдельного RAM опыта, Release `dok_inspect` и `dok_monster_ai_probe`.

```powershell
py -3.14 -B ports/sdl2/tools/build_commands.py --build-dir build/sdl2/windows --ninja <путь-к-ninja.exe> dok_inspect dok_monster_ai_probe
py -3.14 -B ports/sdl2/tools/export_monster_ai_audit.py
py -3.14 -B ports/sdl2/tools/check_monster_ai_oracle.py
py -3.14 -B ports/sdl2/tools/check_monster_ai_oracle.py --activation
py -3.14 -B ports/sdl2/tools/check_monster_ai_oracle.py --angles
py -3.14 -B ports/sdl2/tools/check_monster_ai_oracle.py --death
py -3.14 -B ports/sdl2/tools/check_monster_ai_oracle.py --exhaust --quick
py -3.14 -B ports/sdl2/tools/check_monster_ai_trajectories.py
py -3.14 -B ports/sdl2/tools/check_monster_ai_angles.py
py -3.14 -B ports/sdl2/tools/summarize_monster_ai_native.py
```

Компактный машинный итог: `analysis/summaries/source-port-sp12-native-complete-summary.json`. Код всех probes/tools хранится в `ports/sdl2/`; imports идут только из соседних tracked Python-модулей. `research/` содержит входные экспорты и результаты, не исполняемый test-код.

`--before` у trajectories предназначен для исторического pre-fix бинарника: текущая исправленная сборка не воспроизводит прежний дефект. При обычном запуске before-evidence не изменяется. Обновления итоговых планов/handoff и общей Release/CTest приёмки выполняются интегратором после сборки всех изменений SP11–SP13.
