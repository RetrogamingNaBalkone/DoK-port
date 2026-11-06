# SP10/SP11 — автономная проверка после SP17

5 ноября 2026 года. Проверен текущий SDL2-код после добавления сохранений.
Производственный код, оригиналы, GBA/NDS и их статусы этим проходом не менялись.
Полные материалы: `research/evidence/source-port/sp17-followup-combat/`.

## Повторная проверка принятого поведения

`run_regressions.py` запускает сохранённые инструменты и текущие Release probes.
Все девять команд прошли; `regression-report.json` содержит команды, журналы,
время и SHA-256 трёх оригинальных EXE/RES до/после. Хеши совпадают.

| Проверка | Результат | Граница |
|---|---|---|
| Movement x86/C++ | 35 399 сравнений, 0 расхождений | Исходные footprint/resolver/step/motion; clock controlled |
| OG stat/weapon x86/C++ | 1 440 сравнений, 0 расхождений | Статистика и переходы оружия; не полный DOS-сеанс |
| Interaction x86/C++ | 720 callbacks, 0 расхождений | Двери и base-use; scheduler/audio/HUD external |
| OG current-bank | 990 native поз, поздний witness, 3 FPS PASS | Локальный исходный карман; не полное прохождение |
| Projectile probe | 54 сценария PASS | RU/EN, lifecycle/damage/immunity/render/reuse/3 FPS, арена |
| Weapon probe | 18 сценариев PASS | RU/EN, pickup/fire/cooldown/switch/overlay/3 FPS |
| Placed items | 295 OG + 626 PKR + 626 PKE PASS | Изолированные исходные клетки, не маршруты |
| IDKFA | 21 сценарий PASS | OG, реальный projectile path, 3 FPS |
| SDL switching | 6 процессов PASS | Все три датасета × GL/software, цифры и колесо |

Точные команды воспроизводятся локальным `run_regressions.py`; исходные
инструменты находятся в `ports/sdl2/tools/` и паспортных командах SP10/SP11.
Повторный прогон обновил полные отчёты movement/combat/interaction/current-bank;
их компактные сводки обновляет общий `tools/reports/summarize_reports.py`.

## Новая независимая проверка полёта

До этого прохода projectile probe сравнивал три FPS одной C++-реализации.
Теперь исходные инструкции полёта независимо исполнены в Unicorn и сравнены
с текущим `Projectiles::event`: **4 290 векторов, 0 расхождений**, все семь LEVEL.
OG: 330; каждый из шести PKR/PKE LEVEL: 660.

Листинги и байты взяты из сохранённых read-only Ghidra экспортов SP10/SP11.
Перед использованием проверены SHA-256 `objects.bin` и `animation.bin` по
manifest. Оригинальные EXE и RES проверены до/после и не изменились. Новый
анализ или декомпиляция EXE вне Ghidra не выполнялись.

| Профиль | Callback, адрес Ghidra | Существенные поля |
|---|---|---|
| PKR | NE `1068:26e8` | damage `+5c`, floor `+5e`, source far pointer `+69` |
| PKE | NE `1020:2d8b` | Те же поля; самостоятельный экспорт ENG |
| OG | MZ `1ae1:22fa` | damage `+5a`, floor `+5c`, source far pointer `+67` |

Это Ghidra MZ/NE-адреса, не файловые смещения и не DPMI-селекторы.
Проверены heading 0/1/44/89/90/135/180/225/270/315/359; отрицательная скорость
−1/−119/−120/−121/−480/−481/−1000; signed damage, включая WORD extrema;
elapsed 0/1/416/832/1664/3328. Матрица берёт каждый седьмой элемент декартова
произведения; все перечисленные значения присутствуют. Для Кремля проверены
frame0 и последний flight frame; у OG flight имеет только native frame0.

Независимые результаты: X/Y/Z после шага, signed ADD16/SAR damage decay,
frame/wrap и отсутствие ложного impact. Исполняются исходные flight,
world/Q15-step и, у Кремля, animation-next. Stopwatch elapsed контролируется;
неизменяемые clip headers имеют исходное число кадров и фиктивный image pointer.
Virtual base-use оставляет event mask без контакта. Геометрия — свободная
контролируемая область; actor coordinates не подходят к границе карты.

Это **не** native-проверка контакта, class-specific hit handlers, высоты
попадания, effect lifecycle, звука или reclaim статических glyph-слотов.
Их существующие C++-проверки выше не повышаются до DOS-эквивалентности.
OG frame>0 в native callback уже означает effect; такой fixture исключён
из flight-матрицы, а не принят за несовпадение свободного полёта.

Воспроизведение:

```powershell
# Probe source и команда shared Zig сохранены в evidence; результат компиляции в build.
py -3.14 -X utf8 -B research/evidence/source-port/sp17-followup-combat/check_projectile_flight.py
```

`projectile_flight_probe.cpp` использует текущий core, не Python-формулу.
`flight-summary.json` и отдельные `flight-<profile>-<LEVEL>.json` содержат входы
и обе независимые стороны каждого сравнения. Source и probe локальные;
они не добавляются в пользовательский пакет.

## Измерение FPS оригинального DOS

Автономный стенд использует DOSBox Automation 0.85.1, `core=normal`,
`cputype=pentium`, 24 000/60 000 cycles, VGA 320×200, без звука.
Запускаются отдельные копии неизменных PKR/PKE; ввод идёт через i8042 API.
Game memory, pose, clock и callback не записываются.

Делитель class12 находится в **DS:00c2 у обеих языковых версий**: PKR
`1068:2236`, PKE `1020:27e6` выполняют IDIV dword `[0xc2]`. Сдвиг +B0 к этой
глобальной переменной не применяется. У PKR main `1008:0fcd–0ff3` сохраняет
frame ticks и сглаживает divisor как `(10000 / frame_ticks + previous) / 2`,
с нижней границей 4. Эти адреса подтверждены сохранёнными Ghidra листингами.

Стенд сначала проверяет естественный spawn по исходным данным, проходит
шесть Enter и короткий Right, затем собирает четыре секунды наблюдений.
Условия принятия ряда: ненулевые frame ticks, растущие scheduler_elapsed и
IRQ_count, неизменность оригиналов и исполняемых копий. Первые разведочные
ряды, остановленные на экране начала попытки, **отброшены**: один лишь
ненулевой divisor не доказывает работающую симуляцию.

Полная матрица принята: **12/12 оригинальных процессов**, **1 147** samples,
в каждом ряду растут IRQ и scheduler_elapsed, оригиналы и исполняемые копии
побайтно неизменны. Медианы сохранённого native FPS divisor:

| Датасет/уровень | 24 000 cycles | 60 000 cycles |
|---|---:|---:|
| PKR LEVEL1 | 15 | 39 |
| PKR LEVEL2 | 16 | 41 |
| PKR LEVEL3 | 20 | 52 |
| PKE LEVEL1 | 15 | 39 |
| PKE LEVEL2 | 14 | 39 |
| PKE LEVEL3 | 19 | 51 |

Проверенный контроль PKR LEVEL1 при 24 000 cycles: divisor15, frame ticks
623–628; scheduler107127→147128, IRQ195→268, heading90→89 после Right.
Полная матрица и итоговые ряды: `dos-fps-verified/summary.json`; каждый запуск
содержит config, журнал, raw frame, исходные samples и SHA-256.

```powershell
py -3.14 -X utf8 -B research/evidence/source-port/sp17-followup-combat/check_dos_fps.py --full
```

Измерение показывает зависимость оригинального делителя от производительности
и сцены. Оно не меняет принятую SDL2-адаптацию `/30` и не объявляет её точной
эмуляцией DOS. Для value100 порт даёт 3 за callback; DOS при divisor15 даёт 6.
Это сравнение арифметики одного применения, не измеренная итоговая скорость
перемещения по воде с учётом коллизий и иной частоты callback.

## Остаток и SP18

- SP10: измеренный FPS не заменяет ручную оценку движения/инерции в конкретной
  пользовательской DOS-конфигурации. Такая сравнительная приёмка принадлежит SP18.
- SP11: программные contact/class-hit/height/effect/reclaim закрыты дополнительным
  [native audit от6 ноября](projectile-native-audit.md), 27 917/27 917 PASS. Два OG-дефекта
  исправлены с независимой регрессией до/после. Ручная оценка боевых ощущений и
  физических таймингов ввода относится к SP18.
- Окончательные игровые прохождения с осмотром боя, дверей, движения и
  отсутствия визуальных зависаний на доставленном пакете относятся к SP18.

## Регрессия после изменения движения в SP12 — 6 ноября 2026

На заново перелинкованных стендах: native movement35 399/35 399 PASS, current-bank990
native поз и coin43,57 при30/60/144 FPS PASS. Добавлены51 постоянный тест host guards
в `movement_tests.cpp`: границы открытой карты при валидной/уже перекрытой начальной
позиции, сохранение свободного footprint для валидного origin, защита чужой занятости.
Это регрессия сохранённых адаптаций границ порта; точные native траектории исходно
перекрытых монстров проверены отдельным SP12 стендом. EXE/RES SHA до/после совпали.

Команды и hashes стендов: `research/evidence/source-port/sp10-post-ai-regression/report.json`.
Production archive этим проходом не менялся; core взят после исправления SP12.
