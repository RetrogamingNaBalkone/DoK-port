# SP10: взаимодействия и отдельные правила Old Gold

4 ноября 2026 года. Это проверка конкретных callback и коротких маршрутов,
а не семь полных прохождений. Реализация SDL2 независима от GBA/NDS.

## Оригиналы и адреса

Read-only экспорт Ghidra 12.1.2:
`research/evidence/source-port/sp10-interactions/ghidra/{ru,en,old-gold}/`.
Каждый manifest фиксирует проект, образец, SHA-256 до/после, диапазоны и
байты. Образцы совпадают с SP01; EXE и RES не изменены. Полные ASM/bin/log
локальны, воспроизводимый драйвер — `ports/sdl2/tools/export_interaction_audit.py`.
Адреса PKR/PKE — Ghidra NE, OG — Ghidra MZ; это не file offsets и не
runtime-селекторы DPMI. Автоматический C использован для навигации;
значимые store, LES, signed compare, far calls и RETF проверены в ASM.

| Область | PKR | PKE | OG |
|---|---|---|---|
| OPEN/CLOSE/AUTO | 1068:3262/334e/3427 | 1020:3917/3a03/3adc | 1ae1:2cdf/2dbb/2e8e |
| Use, ключи | 1068:347e | 1020:3b33 | 1ae1:2eea |
| Class6 vtable, DS | 1190:063d | 10d0:0656 | 3255:0612 |
| Class6 slot14: base event filter | 1068:0521 | 1020:0a01 | 1ae1:0400 |
| Герой: предметы/FCMap/движение | 1068:2127 | 1020:26d7 | 1ae1:1e16 |
| OG getters category/value | — | — | 1ae1:3fb2/3fc0 |

## Исправления OG

Ранее OG использовал дверные изменения flags Кремля. Теперь OPEN OG
снимает bit2, сохраняет flags в native+6a **на каждом шаге**, не разрешает
проход на четверти анимации и выставляет bit10 только в завершающем callback.
CLOSE OG снимает bit10 на первом шаге и восстанавливает saved_flags при
завершении. В отличие от Кремля завершающий CLOSE не присваивает phase=0;
в обычном цикле phase уже достиг нуля, пограничные состояния сохраняются точно.

Перезапись saved_flags на каждом OPEN — факт ASM 1ae1:2d00–2d19,
а не снимок начальных flags: `+15 → +6a`, затем OR8/ANDfffd. Например,
исходные flags40 после полноценного открытия/закрытия OG становятся48.
Нельзя заменять это восстановлением неизменяемого `definition.flags`.
Kremlin продолжает принимать проход после quarter threshold и восстанавливает
исходное native+6a, заданное конструктором.

Независимо обнаружено неверное обобщение class12 в `player_surface`:
OG 1ae1:1e16 передаёт исходное signed WORD+68 в apply_stat напрямую и не
устанавливает флаг замедления4000000. Kremlin делит значение на measured FPS
и устанавливает этот флаг. Исправление `items.cpp` выполнено владельцем SP11:
OG получает исходное значение и `slow=false`, Kremlin сохраняет принятую
адаптацию /30 и `slow=true`. Поэтому OG шипы со значением−1 снова наносят
урон, а течение+100 не превращается в+3. Это восстановление различий
оригиналов, не новый баланс. Проверка касается аргументов и логики поверхности;
не означает, что игрок уже прошёл все опасные клетки карты.

`DoorState::saved_flags` — новое сохраняемое поле S для SP17, native+6a.
Конструктор `World` задаёт исходные flags, OPEN OG изменяет поле. Snapshot
должен сохранять его отдельно от текущих flags и определений LEVEL.

## Class6

У всех трёх оригиналов slot14 class6 указывает на общий base event filter.
Он лишь снимает event bit40000 при ненулевом source и отсутствии flags8/10.
Слова native54/56/58 не меняются. Отдельного switch-use или переключающего
callback здесь нет; придумывать анимацию/открытие двери по этим полям нельзя.
Прямой `player_use` порта на class6 остаётся `ignored`; маска40000 относится
к общей native-доставке событий, а не к новому действию Use.

## Уведомления и передача SP14

Восстановлены native сообщения завершения OPEN/CLOSE и отказа замка.
`ports/sdl2/src/door_messages.inc` содержит исходные CP866-байты из DS.
Это не полная локализация: SP14 должен привязать их к смысловым ключам ниже.
Пробел в начале Kremlin строк и оригинальная опечатка EN `COOPER` сохранены
в исходных байтах; будущий перевод может иметь отдельное нормальное написание.

| Предлагаемый ключ SP14 | PKR DS1190 | PKE DS10d0 | OG DS3255 |
|---|---:|---:|---:|
| door.opened | 058c | 059c | 0500 |
| door.closed | 059d | 05a7 | 0510 |
| door.key.copper, mask1 | 05ae | 05b4 | 0520 |
| door.key.iron, mask2 | 05c1 | 05ca | 0532 |
| door.key.gold, mask4 | 05d6 | 05de | 0546 |
| door.secret, mask8 | 05ea | 05f4 | 0559 |
| door.key.unknown, прочие mask | 05fb | 0601 | 0569 |

Ключи не расходуются; любой общий бит достаточен. Class13 source обходит
замок, соседние FCMap клетки того же owner не вызывают повторный вход.

## Проверки и границы

`check_interaction_oracle.py` исполняет исходные инструкции Ghidra в Unicorn.
720 C++/x86 callback-векторов: три профиля, OPEN/CLOSE/AUTO, два состояния,
пять фаз, занятость0/1, ready0/1, два набора flags. Сравниваются phase,
state, ready, flags и saved_flags. **720/720 PASS**. Interval7 исключает
отдельную адаптацию interval0/334 ticks. Внешние scheduler/audio/HUD-вызовы
явно заменены stubs; исходный stack-copy helper исполняется, OG occupancy
getter исполняется. Это не полноценная DOS-сессия и не акустическая проверка.

168 дополнительных native class6 base-use сценариев подтвердили фильтр
маски и неизменность native54/56/58. Ещё12 OG native FCMap сценариев со spy
на границе apply_stat подтвердили неизменённый signed аргумент и отсутствие
slow flag. C++ movement regression покрывает36 аналогичных surface сценариев
трёх профилей. Это различающиеся границы проверки, а не720 полных миров.

`dok_interaction_tests` теперь понимает OG LEVEL4: 21 дверь/320 сцен/7 sensor
клеток; PKR и PKE — по54 двери/960 сцен/10 sensor клеток. Двери вызываются
напрямую, сенсорные клетки проверяются отдельным переносом героя; это не маршрут.
Регрессия проверяет OG полную OPEN/CLOSE, quarter threshold, сообщения и
трассы трёх профилей при30/60/144 FPS.

`dok_walk_probe` расширен на OG LEVEL4. Семь исходных карт, по6 секунд ввода
от исходного spawn при30/60/144 FPS; конечные позы совпадают. OG1404,
PKR4212 и PKE4212 кадров. Здесь нет телепортации или снятия стен, но это
короткий маршрут без полного боя/финиша. Он не закрывает A09/A13.

Команды из корня:

```powershell
py -3.14 -B ports/sdl2/tools/export_interaction_audit.py
powershell -NoProfile -ExecutionPolicy Bypass -File ports/sdl2/tools/build.ps1 -Configuration Release
py -3.14 -B ports/sdl2/tools/check_interaction_oracle.py
build/sdl2/windows/dok_interaction_tests.exe originals/Old_Gold_3D/OLD_GOLD.RES originals/pkr/RUS/PKR.RES originals/pkr/ENG/PKE.RES
build/sdl2/windows/dok_walk_probe.exe originals/Old_Gold_3D/OLD_GOLD.RES
build/sdl2/windows/dok_walk_probe.exe originals/pkr/RUS/PKR.RES
build/sdl2/windows/dok_walk_probe.exe originals/pkr/ENG/PKE.RES
```

Полные local отчёты: `research/evidence/source-port/sp10-interactions/`:
`callback-report.json`, `cpp-matrix.csv`, `walk-report.json` и `walk-*.log`.
Остаются полные маршруты семи уровней, native приёмка всего motion/resolver
и DOS frame pacing/скорости Кремлёвского течения. Независимый callback oracle
этого не доказывает. SP10 остаётся в работе в границе полной приёмки.
Следующий владелец боя/предметов — SP11; AI — SP12; маршруты до финала — SP16/SP18.
