# Время и события: G06

Проверено 25 октября 2026 года на неизменённых PKR.EXE RUS и PKE.EXE ENG.
Идентичность образцов — `analysis/inventory-{ru,en}.json`; SHA до/после,
проверяемые инструкции и DOS-наблюдения —
`analysis/g06-time-evidence.json`. Все адреса ниже — **адреса загрузчика NE
в Ghidra**, не файловые смещения и не runtime-селекторы DPMI.

## Доказательства и воспроизведение

`scripts/InspectTime.java` экспортирует адресные листинги, вызывающие функции,
статические данные и отдельно автоматический, непроверенный C-псевдокод.
Он также дизассемблирует ранее пропущенные IRQ-entry и clear-events.
Проекты сохранены в `build/ghidra/{ru,en}/Kremlin.gpr`, экспорты/журналы/
манифесты — `build/time/{ru,en}/`. Общий Ghidra используется без новой установки.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/analyze-time.ps1
C:/Source/wasdify/.venv/Scripts/python.exe scripts/verify_time_evidence.py
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build-native.ps1 -CrossChecks
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build-gba.ps1 -TimeProbe -OutDir build/gba-time -Verify
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build-gba-renderer.ps1 -CrossChecks
```

Последняя команда проверяет и G06-время в общем renderer-ROM. Отдельную
G06-проверку общей ROM можно повторить:

```powershell
C:/Source/wasdify/.venv/Scripts/python.exe scripts/verify_time_gba.py --build-dir build/gba-renderer --report analysis/g06-renderer-time.json
```

Новые DOS-запуски (каталог результата должен ещё не существовать):

```powershell
C:/Source/wasdify/.venv/Scripts/python.exe scripts/dos_reference.py ru --output build/time/dos-ru --time-probe
C:/Source/wasdify/.venv/Scripts/python.exe scripts/dos_reference.py en --output build/time/dos-en --time-probe --port 8138
```

Копируются исходные файлы в изолированный `build/`; EXE не патчится.
Вход в уровень использует уже исследованную инъекцию клавиш G00, а не IRQ1.
По 21 HTTP-снимку на язык подтверждены period=65536, работающий scheduler clock
и два активных владельца на старте LEVEL1. Получено RUS≈10117 ticks/с,
ENG≈10041 ticks/с по wall-clock и около18,3 IRQ/с. Это грубое наблюдение,
согласованное с PIT-моделью; HTTP не синхронизирован с гостевыми инструкциями.
Снимки не доказывают точность каждого deadline, rollover или изменение списка
в работающей DOS-игре. Эти крайние случаи восстановлены по ASM и проверены
в новой реализации, без заявления о динамическом DOS-эквиваленте.

## Источник ticks

| Роль | RUS | ENG |
|---|---|---|
| INT1C handler | `1130:0000` | `1078:168A` |
| установка PIT/INT1C | `1130:00AE` | `1078:1738` |
| чтение времени PIT | `1130:015D` | `1078:17F3` |
| stopwatch init/start/read/stop | `1130:01F5/0239/026F/02B6` | `1078:188B/18CF/1905/194C` |
| изменение period | `1130:02E4` | `1078:197A` |
| scheduler clock в data | `1190:20C6` | `10D0:2176` |
| IRQ count/base/period/calibration | `1190:0E70/0E74/0E78/0E7C` | `10D0:0F20/0F24/0F28/0F2C` |

Handler сохраняет 32-битные регистры, DS/ES и BP, увеличивает uint32 IRQ count
и возвращается через IRET. Установка сохраняет прежний INT1C, ставит свой
handler, пишет `34h` в43h и два нуля в40h: PIT channel0, mode2, period65536.
Чтение выполняется под CLI, latch command0 в43h, два IN из40h, затем STI.
Атомарность чтения обеспечена для этого участка; восстановление прежнего IF
через PUSHF/POPF отсутствует. Это платформенный эффект, на GBA не переносится.

ASM-модель чтения, с uint32 wrap и unsigned DIV:

```text
phase16 = uint16(calibration - latched_counter)
ticks = uint32(uint32(irq_count * period) / 119 + phase16 / 119 + base)
if irq_count > 10000: irq_count = 0; base = ticks
return ticks
```

Две операции DIV раздельны; остатки теряются. Calibration после измерения
overhead обнуляется. Period в обоих статических образцах равен65536 и в новых
DOS-пробах остаётся таким. При count>10000 (~549с) код после STI обнуляет
count и сохраняет ticks как base, не переустанавливая фазу PIT: возможны скачок
на фазу и гонка с IRQ. Они не измерены длительным DOS-запуском.
32-битный IMUL хранит младшие биты; сброс count до10001 ограничивает обычное
произведение при исходном period, но не доказывает безопасность произвольного
period. Расширения глобального времени до64 бит в оригинале нет.

Используемая инженерная частота PIT1193182Гц подтверждается
[исходником DOSBox Staging timer.h](https://github.com/dosbox-staging/dosbox-staging/blob/main/src/hardware/timer.h).
Отсюда номинальная шкала `1193182/119≈10026,7395 ticks/с`, tick≈99,733мкс.
Это вывод из инструкции DIV и частоты PIT, а не точное измерение кварца PC.
Прежняя оценка «tick=0,1мс» пригодна лишь как округление.

Stopwatch —10 байт: running uint16, start uint32, elapsed uint32.
Start при running=0 записывает текущее абсолютное время; read при running!=0
вычисляет `uint32(now-start)` и возвращает DX:AX. Stop сначала делает read,
затем running=0. Повторный start начинает новый интервал, накопления пауз нет.
Конвенция этих методов — far C-вызов, аргументы в стеке, cleanup вызывающим.
`MOV AX,SS; MOV DS,AX` важен: работа с данными идёт через native SS/DGROUP.

## Очередь оригинала

| Роль | RUS | ENG |
|---|---|---|
| owner init/destroy | `1038:0000/005F` | `1010:13AA/140F` |
| schedule | `1038:0108` | `1010:14BE` |
| cancel текущего события | `1038:0214` | `1010:15CA` |
| clear всех | `1038:0234` | `1010:15EA` |
| set interval | `1038:0265` | `1010:161B` |
| dispatch | `1038:0284` | `1010:163A` |
| число owners/events/cursor/array/keep | data `03E8/03EA/03EC/03F0/03F4` | data `03F0/03F2/03F4/03F8/03FC` |

Разметка owner/event — [native-state.md](native-state.md). Native event:
deadline+0, interval+4, callback far+8, opaque parameter+12. Owner хранит
кольцо только владельцев с событиями. При переходе count0→1 новый owner
вставляется после глобального cursor, а cursor становится новым owner.
При A,B,C первоначальный проход идёт C,A,B. Внутри owner порядок вставки;
сортировки по deadline нет. Равенство срока включает callback.

Ёмкость задаётся constructor argument, в игровых объектах4. Условие
signed16 `count < capacity-1` разрешает три события, четвёртое молча теряется.
Новый deadline — `uint32(clock_read()+interval)`. Вызовы анимаций явно используют
`MOVSX EAX, word [object+52h]`: отрицательные int16 остаются отрицательными
32-битными битовыми шаблонами, без clamping. Например, interval=-1 при now=0
даёт deadlineFFFFFFFF (ждать), при now=10 —deadline9 (сразу due).
`CMP now,deadline; JC skip` сравнивает **unsigned**, не signed-разность.
Поэтому deadline, завернувшийся вперёд в малое число, может сработать раньше
rollover; старый большой deadline после rollover может ждать почти весь цикл.
Поведение сохранено осознанно.

Подробности прохода, одинаковые в обеих версиях:

1. Для каждого элемента есть read-index и write-index. Сначала запись
   копируется из read в write, устанавливается глобальный keep=1.
2. Сравнение, callback и последующее обновление deadline используют **read**.
   Callback получает только far-pointer owner; parameter не передаётся.
3. После callback вновь читается clock; deadline исходного слота становится
   `now_after_callback + source.interval`. Просроченные периоды не догоняются.
4. Cancel устанавливает глобальный keep=0, не стирая остальные события.
   При keep!=0 продвигается write, read продвигается всегда. Append в текущий
   непустой owner увеличивает живой count и может исполниться в том же проходе.
5. Set interval пишет **array[0].interval**, не source[read].interval.
   Эти два странных свойства подтверждены ASM. После удаления первого события
   перемещённый survivor сохраняет старый deadline в compacted slot: обновление
   исходного слота уже не попадёт в итоговый count. Это не исправлено.
6. В конце owner count=write; пустой owner исключается из кольца. Внешний
   счётчик посещений сравнивается с **уменьшенным** owner_count. Поэтому после
   исключения owner часть соседей откладывается до следующего dispatch.

Направление копирования проверено по `1000:5CB3`: source `[BP+6]` загружается
в DS:SI, destination `[BP+Ah]` —в ES:DI; CLD/REP MOVSW, RETF8, длина вCX.
Это stdcall-подобный compiler helper, не обычный C memcpy с длиной на стеке.
Регистр/сегментная семантика объясняет выбор source/read после копирования;
восстановление не основано только на именах переменных декомпилятора.

`dispatch(duration)` при ненулевой очереди вычисляет end=now+duration,
выполняет хотя бы один sweep и повторяет, пока unsigned now<end.
Главный игровой цикл вызывает **dispatch(0)** до построения/вывода сцены.
Перенос реализует один sweep, без необязательного busy wait. События не равны
VBlank или одному кадру; в оригинале частота опроса всё же влияет на запаздывание.

## Перенос и явные исправления

`src/scheduler.c` хранит callback ID uint16 вместо DOS far-pointer. ID0 запрещён.
Переносимый callback дополнительно получает opaque parameter и context —это
контракт адаптера, не восстановленная native-сигнатура. uint32-арифметика
сохраняет переполнение без C signed-overflow UB. `dok_event`16 байт,
owner56 байт с тремя слотами; память предоставляет вызывающий, malloc нет.
Probe использует один owner. Для будущей игры384 owners заняли бы21504 байта,
поэтому прежний резерв16384 нельзя считать доказанно достаточным; окончательная
привязка к игровым объектам и лимит активных owners остаются интеграции механик.

Сохранены unsigned deadlines, отрицательные интервалы, native ring order,
живой append, compaction/source-deadline и first-slot interval, shrinking bound.
Оригинальные неопределённые/опасные случаи изменены явно:

- Destroy текущего owner из callback откладывает unlink до возврата и прерывает
  его оставшиеся callbacks. Память не освобождается, повторное создание этого
  ID до конца его обработки запрещено. В оригинале destroy освобождает массив
  и меняет глобальный cursor под продолжающим работать dispatch: возможен UAF.
- Активация прежде пустого owner из callback помещается в pending-list;
  публикация в кольцо —после sweep в порядке вызовов. Новый owner начинает
  исполняться со следующего dispatch. В оригинале вставка сразу меняет cursor,
  хотя array глобально остаётся от прежнего owner: возможна порча обхода.
  Pending-owner можно удалить до публикации; ID до неё не переиспользуется.
- Рекурсивный dispatch возвращает отказ; clear во время callback не действует.
  Native глобальные array/keep/cursor не обеспечивают reentrancy.
- Helpers cancel/interval вне dispatch возвращают отказ вместо записи через
  устаревший глобальный указатель. Destroy неактивного ID безопасен.

Это инженерные решения порта, не доказательство, что исходная игра намеренно
использует такие небезопасные пути. Восстановление конкретных игровых callbacks
будет проверяться в соответствующих G07–G15, вместе с этими ограничениями.

## Адаптер GBA и фиксируемое исполнение

`gba/clock.c` резервирует timers2/3: timer2 prescaler64, timer3 cascade.
Согласованный снимок hi/lo/hi повторяется при смене hi. IRQ не требуется.
Timers0/1 остаются измерителю cycles renderer. `dok_clock_advance` преобразует
счётчик с частотой262144Гц в ticks по точной выбранной рациональной шкале:

```text
numerator = previous_remainder + elapsed_counter_counts * 1193182
ticks += numerator / (262144 * 119)
remainder = numerator % (262144 * 119)
```

Chunks≤1024 исключают переполнение numerator uint32 и необходимость64-битного
деления на ARM7TDMI. Разбиение чтений не меняет ticks/остаток. Hardware counter
wrap учитывается unsigned subtraction; опрашивать нужно чаще2^32 counts
(16384с≈4,55ч). Native ticks также uint32 и заворачиваются примерно через4,96сут.
Потеря более полного периода между выборками не восстанавливается.

В отличие от DOS PIT-reader GBA держит дробный остаток, не воспроизводит
фазовый скачок при reset10001 IRQ и не имитирует длительность DOS-инструкций.
Это явная адаптация платформы. Точность до native tick и конкретные границы
DOS-callback на физическом PC не подтверждены. Номинальная шкала обоснована
кодом и грубыми DOS-наблюдениями, а не равенством C/Python.

`dok_steps` задаёт воспроизводимую сетку; в probe quantum=100 native ticks
(≈9,973мс, около100,267 шага/с). Это решение порта, не частота оригинального
main loop. Scheduler получает now из сетки и вызывается для каждого шага.
После медленного кадра выполняется несколько шагов с их собственными now;
набор callback воспроизводим при той же последовательности входов и шагов.
Будущие input-записи должны иметь номер шага; сейчас движения/input replay нет.
Один poll ограничен128 шагами, весь оставшийся debt сохраняется. Debt должен
оставаться меньше2^32 ticks; переполнение такого долга не поддерживается.
Нет заявления о сохранении реального времени при неограниченной перегрузке.

Общий LEVEL1-ROM вызывает clock/scheduler перед каждым render. Измеритель
полного CPU-кадра теперь охватывает time-poll и input/language switch вместе
с renderer/HUD/VRAM copy; ожидание VBlank остаётся отдельно в cadence.
Изображение сцены не изменяется от synthetic clock callback. Полная игровая
логика ещё не перенесена. Медленный G05 renderer не достигает30FPS;
исправление производительности и подвижная камера —следующая задача G06A.

## Проверки и границы результата

`tests/time_cases.c` исполняется на Windows и на ARM внутри обеих ROM:
capacity, равные сроки/порядок, repeat после callback, unsigned rollover,
negative interval bits, first-slot interval, source-slot compaction, cancel,
append из callback, изменение/удаление owners, deferred activation/cancel,
hardware-counter wrap, дробный остаток, фиксированные шаги с разной группировкой.
Linux static test binary только собран. `-Wall -Wextra -Werror` используется
для host/Thumb. Общие native-проверки —10 тестовых программ.

`analysis/g06-gba-verification.json` и `analysis/g06-renderer-time.json`
фиксируют выполнение ARM-векторов, точную рациональную формулу по аппаратным
снимкам, debt/step/callback counters, rollover младшего16-битного timer за600
VBlank и повторный запуск после poisoned EWRAM. ELF/ROM/map/PNG лежат в
`build/gba-time/` и `build/gba-renderer/`. Это эмуляция mGBA с replacement BIOS;
физическая GBA и оригинальный BIOS не проверены. Полный32-битный hardware
rollover проверен в чистом адаптере, не ожиданием4,55ч в mGBA.

Результаты renderer и независимого host-эталона обновлены в
`analysis/render-benchmark.json`, `analysis/render-host-verification.json`.
Текущая общая ROM2787036 байт, EWRAM89940; полный CPU-замер
10189214…17568022 cycles (max со сменой языка), steady37 VBlank≈1,614FPS.
Watermark main stack2392 байта, untouched5544; IRQ/BIOS не измерены.
Измеренная частота не является критерием завершённой игры. G06 завершает
исследование и пробу времени; следующий ID —G06A.
