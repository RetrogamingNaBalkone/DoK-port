# SP05: исходные инструкции, арифметика и частота симуляции

4 ноября 2026 года. SP05 завершена в границе часов/событий/общей арифметики.
Исходники ядра менять не потребовалось: новые независимые проверки подтвердили
перенесённые контракты. Это не полное прохождение, не аппаратная эмуляция PIT
и не приёмка всех игровых ветвей SP10–SP12.

## Происхождение и адреса

Экспорт выполнен из сохранённых Ghidra-проектов read-only/noanalysis скриптом
`tools/ghidra/ExportSourcePortScheduler.java`. Драйвер
`ports/sdl2/tools/export_scheduler_oracle.py` проверяет исходный SHA-256 до/после;
сам Ghidra-скрипт проверяет SHA-256 источника открытой программы. Бинарные срезы,
ASM, manifest и Ghidra-журналы находятся в
`research/evidence/source-port/sp05-scheduler/{ru,en,old-gold}/`.
Хеши образцов совпадают с таблицей в [time-events](../time-events.md).

Все адреса ниже — адреса Ghidra, не файловые смещения и не DPMI-селекторы.

| Операция | PKR NE | PKE NE | OG MZ |
|---|---|---|---|
| schedule | 1038:0108 | 1010:14be | 2a8d:0112 |
| cancel | 1038:0214 | 1010:15ca | 2a8d:0223 |
| clear | 1038:0234 | 1010:15ea | 2a8d:0238 |
| interval слота 0 | 1038:0265 | 1010:161b | 2a8d:0266 |
| dispatch | 1038:0284 | 1010:163a | 2a8d:0278 |
| compiler copy | 1000:5cb3 | 1000:5cb3 | 1000:1126 |
| контролируемое чтение часов | 1130:026f | 1078:1905 | 2000:a7e7 = 2a5b:0237 |
| globals owners/events/cursor | data 03e8/03ea/03ec | data 03f0/03f2/03f4 | data 0d4e/0d50/0d52 |

В Unicorn NE-сегмент scheduler размещается с CS=5000. OG-срез начинается
по смещению a8d0 того же тестового сегмента: MZ 2a8d:0278 эквивалентен
2000:ab48, а near-переходы сохраняют исходные offset. Это адреса стенда,
не утверждение о загрузке DOS. Far-вызовы clock перехвачены; исходный copy
исполняется с перенаправлением на его экспортированные инструкции.

Harness `scheduler_oracle.py` адаптирован из независимого исследования
`ports/nds/tools/audit_time.py`; код NDS/GBA-движков не импортируется и не
служит эталоном. SDL2 владеет своим скриптом, экспортами и C++ probe.

## Scheduler: 45 независимых сравнений

По 15 сценариев на каждый оригинал: capacity−1, C/A/B-порядок кольца,
deadline equality, часы после callback, compaction со старым deadline,
изменение interval слота 0, live append, сокращающийся внешний предел,
ранний wrap, старый deadline после wrap, отрицательные интервалы в 0/10,
порядок вставки вместо сортировки, cancel всех, clear вне callback,
not-due → equality. Сравниваются точная последовательность
`owner/callback/clock`, число активных владельцев и все логические поля
оставшихся слотов (deadline/interval/callback/parameter).

Исполняются настоящие инструкции schedule/cancel/clear/interval/dispatch
и copy. Владельцы/четыре слота выделены фикстурой. Часы детерминированы,
callback-тела тестовые и вызывают исходный API по far ABI. Данные события —
16 байт; callback получает только far owner, не parameter; copy использует
CX, DS:SI → ES:DI и RETF8. Эти границы явно отделены от полного DOS-запуска.
Пустой native cursor и внутренние лишние clock-read не сравниваются: они не
представлены API порта. Порядок последующих проходов проверяется трассами.

Destroy при dispatch, отложенная активация пустого owner и clear из callback
остаются намеренными безопасными отличиями, описанными в time-events.md.
Их проверяет CTest events; равенство неопределённому native lifetime-поведению
не заявляется. Ненулевой duration/busy wait вне API `dispatch(0)`.

## Арифметика: 6864 вектора

По 1144 входа (144 граничные пары и 1000 детерминированных случайных) на
каждый примитив. Исполняются экспортированные OG-инструкции:

| Примитив | Срез Ghidra, конец исключён | Входы/эффект |
|---|---|---|
| NEG16 | 1000:05de…05e0 | AX; signed16 wrap, включая −32768 |
| ADD32 | 1ae1:22a4…22a7 | EAX/EDX; младшие 32 бита |
| SUB32 | 1ae1:2999…299d | EAX и SS:[BP−4]; младшие 32 бита |
| IMUL32 | 1f1d:0560…0564 | EAX/EDX; младшие 32 бита |
| SAR32 | 1ae1:29bc…29bf | EAX/CL; маска count&31 |
| IDIV32 | 180b:0074…0079 | CDQ, IDIV EBX; signed quotient |

13 случаев деления на ноль/INT_MIN÷−1 дали native fault и domain_error порта.
Флаги CPU и остаток IDIV не часть проверяемого C++ API. Это общие примитивы,
а не доказательство всех формул игровых объектов. После fault создаётся новая
Unicorn-машина, чтобы состояние доставки исключения не влияло на следующий вектор.
PIT-сверка повторно прошла 3399 векторов трёх оригиналов.

## Игровая интеграция

`dok_simulation_cadence_tests` выполняет три секунды на каждой исходной карте,
без подмены геометрии: движение/стрейф/поворот, активация AI, обработчики
монстров, оружия, снарядов, дверей и подборов. Ввод зависит от simulation tick,
не от номера presentation frame. При разбиении времени на 30/60/144 FPS
совпадают callback-трассы и 64-битные контрольные суммы карт, поз/характеристик,
части состояний AI/снарядов и очередей событий. Это проверка детерминированности
конкретного сценария, не исчерпывающая сериализация состояния (владелец SP06).

| Карта | Callback на каждом FPS |
|---|---:|
| OG LEVEL4 | 80 |
| PKR LEVEL1 / LEVEL2 / LEVEL3 | 100 / 81 / 72 |
| PKE LEVEL1 / LEVEL2 / LEVEL3 | 102 / 81 / 72 |

После каждого кадра `advance_events(0)` проверяет паузу: ни callback, ни состояние
не меняются. Реальный SDL focus и renderer-переключения принадлежат SP09/SP08;
стенд headless не объявляет их принятыми. Цикл использует адаптацию порта —
один sweep на native tick; кадровое запаздывание DOS намеренно не воспроизводится.

## Воспроизведение

```powershell
py -3.14 -X utf8 -B ports/sdl2/tools/export_scheduler_oracle.py
powershell -NoProfile -ExecutionPolicy Bypass -File ports/sdl2/tools/build.ps1 -Configuration Release
py -3.14 -X utf8 -B ports/sdl2/tools/scheduler_oracle.py
py -3.14 -X utf8 -B ports/sdl2/tools/check_scheduler_oracle.py
py -3.14 -X utf8 -B ports/sdl2/tools/check_arithmetic_oracle.py
py -3.14 -X utf8 -B ports/sdl2/tools/check_clock_oracle.py
build/sdl2/windows/dok_simulation_cadence_tests.exe originals/Old_Gold_3D/OLD_GOLD.RES originals/pkr/RUS/PKR.RES originals/pkr/ENG/PKE.RES
```

Release CTest 15/15. Полные отчёты: `build/sdl2/checks/{scheduler,arithmetic,clock}-oracle.json`;
исходные native результаты — `research/evidence/source-port/sp05-scheduler/vectors.json`.
Итоговый журнал пяти команд и SHA-256 всех EXE/RES до/после — соседний
`final-check.json`. `summarize_reports.py` выполнен; несвязанные сводки не изменились.
Следующий SP06: инвентаризация всех изменяемых состояний/владельцев/слотов,
конструкторы, destroy/reset и контракт будущих сохранений SP17.
