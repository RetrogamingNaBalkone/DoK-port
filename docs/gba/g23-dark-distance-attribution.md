# G23: полная атрибуция тёмной дали

## Решение

1 ноября 2026: **G23 готова; H04 native-прототип не открывать по этой выборке**.
Расширенный подсчёт обнаружил пропущенную в G22 работу opaque/rectangle и
пересекающих границу faces, но не подтвердил значимый общий резерв тёмной дали.
Runtime, изображение, leases, AI, карты и время не изменены. Следующий ID — G24.

**H04 закрыта отрицательно в рамках этой выборки и не ожидает G24 или G26.**
H04 снята с оптимизации текущего релиза; возвращение к ней не планируется.
G25/H09 исследует отдельную гипотезу материального LOD потолка.

Это ограниченный отрицательный результат для LIGHT15 / depth ≥3720 текущего
конвейера, не доказательство бесполезности любого LOD или любого порога.
В одном движущемся checkpoint доля достигает 8,54%; её нельзя скрывать за
средним, но она не характеризует тяжёлые бои и не доказывает выигрыш CPU.

## Образец и методика

Контроль `g22_split_active`, frontend с UI02, пересобран из текущих исходников:
SHA-256 `53fdf1b1b868effecfd68c8feb87ffb60bcc72190a4d01f38d8684a781860247`,
26 649 252 B. Общие Python 3.14, Zig 0.13.0 и mGBA libretro с replacement BIOS.
SHA core и ELF, команды и хэши отчётов записаны в evidence. DOS EXE не
анализировались; все адреса ниже — GBA, depth — проекционная координата GBA,
не файловое смещение, NE-сегмент или runtime-селектор оригинала.

`audit_g23.py` вызывает production `dok_renderer_draw` в Unicorn из реально
инициализированной mGBA RAM. Hooks наблюдают параметры до изменения coverage:

- `visible_column`: отдельно opaque и masked;
- `dok_gba_tiled_rectangle`: отдельно opaque/masked колонки batch;
- `dok_gba_tiled_surface`: потолочные spans, LIGHT и Q16 depth по строке.

Nested ARM samplers не считаются повторно. Requested — кандидаты до coverage;
open — реальные выборки после coverage, включая прозрачные masked texels.
Writes дополнительно исключает masked zero. Это независимые колонки 120×128,
а не число RGB-пикселей 240×160. Для каждой из **90** replay-поз проверены
coverage и 44 B production stats; сумма writes всех классов равна
`pixels_written`. LIGHT потолка дополнительно совпадает с независимой
идентификацией фактического prelit bank. Warm/cold counts, coverage и UV hash
совпадают во всех 18 парах.

ARM ABI проверен по листингу закреплённого ELF: `surface_raster_hw`
(`0x03003440`) получает surface в r1 после удаления неиспользуемого runtime;
`visible_column` (`0x03003eec`) сохраняет четыре stack-аргумента
source/v/step/mode, а constant record/strip_first компилятор удаляет.
Driver проверяет ROM SHA перед чтением этого частного ABI.

HW базовые пол/потолок исключены: их texture fetch выполняет PPU, CPU не
сэмплирует каждый пиксель; CPU transport относится к G26. UI/оружейный overlay
после `dok_renderer_draw` также исключены. Поверхностные вертикальные стороны
проходят через обычные opaque/masked/rectangle пути и входят в их числа.

## Объём и результаты

Тяжёлые шесть поз G22 и три PERF-позы проверены RU/EN: 18 warm/frozen и
18 cold-geometry/frozen. Cold сбрасывает только geometry cache; это не cold boot.
Дополнительно получены 36 live-route checkpoints всех трёх уровней/RU/EN
и 18 live-firing checkpoints PERF01–03 с настоящими AI/снарядами.
Для боёв герой переставлен в оригинальную комнату, HP/armor/ammo=32767;
карты и определения врагов сохранены.

| Выборка | Requested | Open после coverage | LIGHT15 open | Доля от open | Максимум одной позы |
|---|---:|---:|---:|---:|---:|
| Warm, 18 | 333484 | 268754 | 1342 | 0,499% | 2,698% |
| Cold geometry, 18 | 333484 | 268754 | 1342 | 0,499% | 2,698% |
| Moving, 36 | 442070 | 408680 | 3004 | 0,735% | 8,541% |
| Live firing PERF01–03, 18 | 377476 | 302312 | 0 | 0% | 0% |

Frozen результаты обоих языков совпадают. Значения ниже — на один язык;
полные LIGHT0–15 гистограммы, requested/open/zero, crossing и class counts
сохранены в `research/evidence/gba-g23/*-work.json`.

| LEVEL:cell:heading | Все open | LIGHT15 open | Из них opaque/surface | Из них masked |
|---|---:|---:|---:|---:|
| 1:520:225 | 14474 | 0 | 0 | 0 |
| 1:3640:225 | 10712 | 289 | 205 | 84 |
| 1:2242:90 | 15360 | 0 | 0 | 0 |
| 2:1592:135 | 17483 | 340 | 280 | 60 |
| 2:2871:225 | 19518 | 42 | 42 | 0 |
| 3:1544:315 | 15554 | 0 | 0 | 0 |
| 2:3039:90, PERF03 | 15275 | 0 | 0 | 0 |
| 3:1623:0, PERF01 | 9867 | 0 | 0 | 0 |
| 3:1940:270, PERF02 | 16134 | 0 | 0 | 0 |

В L1/3640 прежние 32 потолочных +84 masked samples были неполной оценкой:
добавились 173 opaque/rectangle samples. В L2/1592 есть 40 дальних samples
на face, **пересекающем** depth3720, поэтому whole-face rejection их теряет.
В L1/3640 crossing faces дают 108 open, из них 9 за границей. Само число faces
не используется как цена пикселей.

Максимум moving — RU LEVEL3, шестой checkpoint: 479/5608 open (8,541%),
439 opaque/surface и 40 masked; 108 дальних samples на crossing faces.
Это локальная возможность, не тяжёлая стоячая сцена. На остальных маршрутах
даль также не объявляется отсутствующей: все checkpoints доступны в отчёте.

## Sampling, setup и цена замены

Отдельный `g23_kernel_profile` добавляет только `HW_KERNEL_PROFILE` к G22.
18 PPU-пар RU/EN сохраняют RGB, coverage и image/surface leases.
Instrumented timings приведены в cycles (timer ticks ×64); они содержат
таймерную/IRQ цену и не складываются с production CPU.

| RU поза | Production warm CPU | Production cold CPU | Instrumented ceiling sampling | Ceiling setup и прочая работа вне sampler |
|---|---:|---:|---:|---:|
| L1/520 | 1336352 | 1487232 | 307776 | 285312 |
| L1/3640 | 1283744 | 1464320 | 359104 | 373312 |
| L2/1592 | 1403840 | 1584192 | 53120 | 274752 |
| L2/2871 | 1458880 | 1735744 | 95808 | 272896 |

Например L1/3640: всего 5533 открытых потолочных samples, из них LIGHT15=32.
Пропорциональная оценка полной цены этих samples — около 2077 instrumented
cycles; masked 84/1495 дают около 3416. Сумма 5493 — **не верхняя граница и
не прогноз ускорения**: стоимость неодинакова, overhead/setup распределены
неравномерно, а маска не удаляет весь sampler. Для opaque отдельного native
таймера нет; его sampling и setup остаются в общей цене face. Полные строки
профиля находятся в `analysis/gba/g23-attribution-20261101.json`.

Оценка black-mask/clip выражена в проверяемых операциях, без выдуманных cycles:

- На 18 warm-позах максимум 1054 opaque/surface texture reads можно было бы
  заменить чёрным; **1054 coverage decisions и destination writes остаются**.
  Это 0,392% всех open. Ещё 288 masked samples требуют alpha/silhouette read;
  залить весь прямоугольник чёрным без чтения нельзя.
- В L1/3640 проверка порога на каждой существующей колонке/span добавляет
  534 проверки ради 205 opaque/surface reads; дальних колонок/spans — 33.
  В L2/1592 — 562 проверки ради 280 reads; дальних calls — 31. При переносе
  проверки на face нужны отдельная обработка crossing faces и сохранение
  исходного affine depth/light rounding. Эти числа не включают ещё branch/
  dispatch/clip overhead и не означают, что каждая проверка стоит один цикл.
- Surface depth меняется по строкам; black mask не устраняет автоматически
  edge setup, UV division, coverage либо leases. Masked цвет уже prelit,
  так что замена одного ROM fetch без сохранения прозрачности невозможна.
- Для локального moving-максимума 135 column/span checks обслуживают 439
  opaque/surface far samples. Здесь отношение лучше, но не измерены native
  black-fill выигрыш и качество. Одного checkpoint недостаточно для открытия
  общего H04 при нулевой дальней работе в PERF01–03.

Таким образом, новый black-mask/clip не реализован и экономия CPU не заявлена.
LIGHT15 не равен чёрному, поэтому даже идеальный shortcut не является exact.
Практическое решение для текущего релиза — не тратить реализацию и проверку
качества на H04; продолжить конкретные опыты G24 и G26.

## Живое время и проверки

Непрерывное timing-измерение выполнено отдельно: 600 video frames на каждый
уровень/язык по расписанию G22, без attribution hooks или остановок.
FPS RU L1/L2/L3: **16,33 /15,43 /22,10**, EN: **16,33 /15,43 /21,80**.
Median/p95/max CPU и checkpoints записаны в `live.json`; это характеристика
маршрутов текущей ROM, не сравнение двух оптимизаций.
Бои — десять level/language fixtures по 120 предъявлений с огнём, без audio
underruns. Frozen CPU измерен с одинаковым PCM, 6 прогревами и 12 samples.
Минимальный frozen FPS данной выборки — 9,95, фиксированного gate нет.

Attribution moving/battle дополнительно удерживает завершённый draw после
каждого блока 100 video frames. Его snapshots не выдаются за непрерывное
расписание timing-прогона или exact live A/B двух различных ROM.
Host UI tests, Windows build, Linux/Thumb cross-build прошли при пересборке.
Физическая GBA, original BIOS и полный playthrough не проверены. Полная
релизная регрессия не повторялась: production ROM побайтно прежняя; профиль
не предлагается в поставку. G21/BUG02 и другие направления не меняются.

## Воспроизведение и передача

```powershell
py -3.14 -X utf8 ports/gba/scripts/build_gba_frontend.py --out build/gba/g23/control --variant g22_split_active
py -3.14 -X utf8 ports/gba/scripts/prototype_gba_bsp.py --base build/gba/g23/control/base --out build/gba/g23/profile --variants g23_kernel_profile
py -3.14 -X utf8 ports/gba/scripts/check_g23.py
py -3.14 -X utf8 ports/gba/scripts/report_g23.py
py -3.14 -X utf8 tools/reports/summarize_reports.py
py -3.14 -X utf8 tools/reports/summarize_reports.py --check
git diff --check
```

Полные команды проверок — `research/evidence/gba-g23/commands.json`, журналы —
`build/gba/g23/`, raw evidence — `research/evidence/gba-g23/`, локальный
aggregate — `analysis/gba/g23-attribution-20261101.json`, Git-сводка —
`analysis/summaries/g23-attribution-20261101.json`. Production-кандидат остаётся
`artifacts/gba/DoK-port_G22.gba`; новой пользовательской сборки G23 нет.

Следующий субагент выполняет **G24**, читая актуальные план, бэклог, handoff
и паспорт. Нулевой результат H04 не отменяет самостоятельную проверку H08.
