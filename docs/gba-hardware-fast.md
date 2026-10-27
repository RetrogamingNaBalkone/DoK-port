# G20A: точные оптимизации после128 строк

Следующий проход и расширенная проверка пространственных ракурсов описаны в
[gba-render-costs.md](gba-render-costs.md). Этот документ сохраняет результаты
e803 ROM и прежней выборки; новые минимумы нельзя выводить из её стартовых камер.

27 октября2026. Новый отдельный ROM:
`artifacts/gba/DoK-port_hardware_fast_prototype.gba`,10 845 692 B,
SHA-256 `e803f847e2c21abd9c1e5dbc70f12e98ef68f8c1ffc699989c7b97204a74308c`.
ELF/map/source/QA — `research/experiments/g20a-hw-projection/hw_v128_projection/` и родительский
каталог; чистое повторение — `research/experiments/g20a-hw-fast-final/`.
Реестр — `analysis/g20a-hardware-fast.json`; collector проверяет фактические JSON.
Прежний [128-строчный вариант](gba-hardware-rows.md), остальные готовые ROM и
оригиналы сохранены. SDL2 не менялся.

**Goal/G20/G20A открыты.** Live averages уже выше15 во всех трёх мирах в
измеренных проходах, но дополнительный sweep обнаружил14,931875 FPS на части
фиксированных направлений LEVEL1/2.24,1/30 для всех миров не достигнуты.
Нельзя закрывать цель по одному среднему или округлять этот минимум вверх.

## Что изменено

Изображение осталось тем же, что в прежнем128 ROM:396 actual PPU RGB кадров
совпали полностью, включая weapon/HUD/message/pause fixtures RU/EN.
Поэтому новых потерь качества относительно него нет: CPU120×128 растягивается
в240×144, GPU floor144, исходные интерфейс и оружие. Сохраняются ограничения
того quality trial; это не исходный DOS affine renderer.

Выбранные изменения:

- `HW_COVER4`:128 строкам нужны4 слова покрытия вместо5. Удалены480 bytes
  IWRAM data, в C и обоих ARM surface samplers stride теперь16 вместо20.
  Важен также linear Mode4 fallback: builder пересобирает `gba/columns.S`,
  а не только hardware kernel. Flag допустим только с128-row variant.
- Убрано `HW_COVER_ROM`: освобождённая память использована для
  `coverage_edge` в IWRAM. Это частый helper восстановления интервалов;
  прежнее размещение в ROM увеличивало цену выборки инструкций.
- `HW_DIRECT_RUN`: opaque sampler сразу получает tiled pointer. Удалено
  повторное восстановление X/Y через деление адресного смещения на240.
  Первый Y внутри tile извлекается из готового смещения. Compile-time gate
  требует `NO_SCALES`, поскольку старые precomputed strips используют linear API.
- `HW_PROJECT_RECIP`: `dok_projection_project` использует уже проверенный
  exact reciprocal helper при denominator1..16384; остальные значения идут
  прежним signed IDIV. Low32 IMUL/ADD/SUB, SAR15, нулевой divisor и
  INT_MIN/-1 failure с неизменным output сохранены.

IWRAM24240/free336 до stack reserve8192 вместо24500/free76 в прежнем128.
EWRAM260944/free1200. Observed main stack2528/margin5408, IRQ80/margin80.
Именно уменьшение bitset открыло возможность вернуть hot helper из ROM.
Это резерв текущего linked ROM, не весь свободный аппаратный стек.

## Замеры

То же mGBA libretro5cac11…667e4,16 777 216 cycles/s, WAITCNT4317,
replacement BIOS, без overclock. Capture hold выключен в измеренных интервалах.
FPS учитывает предъявление, PCM включён, игровая симуляция/лимиты не урезаны.
Runtime-camera measurements используют coherent completed-logic snapshots.

| Сценарий | LEVEL1 | LEVEL2 | LEVEL3 |
|---|---:|---:|---:|
| Runtime поворот/огонь24, RU/EN |18,38|15,25|24,30|
| Runtime поворот/огонь120, RU/EN |19,37|15,15|20,90|
| Runtime ходьба/поворот/огонь120, RU |17,11|16,75|19,58|
| То же, EN |17,15|16,75|19,58|

В18 fixtures audio underruns0, cold loads0, upload overflows0,
motion conservation error0; flow gameplay, UI playing. Это измеренные проходы,
не обещание minimum FPS на всём уровне.

Дополнительная `benchmark_gba_heading_sweep.py`:72 фиксированных ракурса,
три исходные позиции игрока, шаг15°,6 unheld presentations после прогрева,
continuous PCM, **симуляция заморожена**, RU. Минимум LEVEL1/2 —14,931875,
LEVEL3 —19,909167 FPS. Первый минимум соответствует четырём GBA refresh на
предъявление. Он обнаружен даже без стоимости живой логики и прямо противоречит
универсальному15+. Количественно это сильнее прежней выборки9 frozen cameras,
но всё ещё не обход всей карты. Не смешивать этот sweep с runtime таблицей.

## Проверка

-396 actual PPU RGB exact относительно16ba83…3961.
-282 transitions, все1601 PPU callbacks — целое старое либо новое изображение;
  mixed0, overflow0. Эта проверка не доказывает любой continuous-gameplay кадр.
-ARM32811 scalar projection vectors: каждый divisor1..16384, границы/fallback,
  signed wrap, нулевой divisor, явный INT_MIN/-1 и неизменность error output.
  Плюс прежние65536 plane division,4096 surface,2048 coverage-edge,
  1024 face-box,1536 tiled,512 masked,256 map и animation vectors.
  Callee-saved registers и SP проверяются после каждого вызова.
-UI22 actual PPU кадра, flow, sprites8 sequences/18 captures,144 water phases,
  AI perception/motion/wake,28 AI combat groups, interactions logic,
  rational clock и poisoned reset прошли.
-G12:109 stat vectors,58 исходных item effects; исчезновение предмета теперь
  проверяется по actual PPU в его прежней экранной области, не по tiled bytes.
-G13: ammunition/flight/collision/occupancy/transitions/input и два независимых
  эффекта. В RU/EN обе перестановки исходных animation frames сверены через
  PPU с оригинальными непрозрачными texels:444 samples. Geometry снимается
  до ожидания PPU, vertical128 mapping учитывается отдельно.
-Адаптированные G12/G13 также прошли на базовом Mode4 ROM7bab…7ea8.
  G13 теперь явно замораживает live AI polling/consideration в контролируемых
  случаях, оставляя синхронные уведомления wake/hurt. Старый pre-G15 oracle
  ошибочно ожидал неизменного state после уведомления; текущие ожидаемые
  состояния1/3 соответствуют уже проверенному G15 и `dok_ai_notice`.
  Game code из-за этого не менялся; G15 отдельно проверяет callbacks/attacks.
-Чистая повторная сборка byte-exact, source control7bab…7ea8;
  все22original SHA совпали с inventory. Collector проверяет сохранность
  опубликованных hardware128/culling/planes и pre-alpha ROM.

Integrated capacity/all-awake stress прошёл проверки guards/stack/audio.
Слово `passed` не означает бюджет: artificial all-awake остаётся около
0,07..0,38 FPS с растущим motion debt. Этот известный предел симуляции
не устранён оптимизацией изображения. Stress использует диагностическую
камеру и не заменяет runtime-camera таблицу. Original affine interaction pixel
oracle сознательно omitted; physical GBA/original BIOS не проверены.

## Невыбранные варианты этого прохода

`research/experiments/g20a-hw-face-light/`: постоянный LIGHT по середине depth грани меняет
до4724 RGB pixels в18 видах, но практически не улучшает L2 FPS. Не выбран.
Это отдельная попытка упростить свет; основной ROM сохраняет прежний свет.

`research/experiments/g20a-hw-hot-cover/`: перенос также `edge_start` из ROM в IWRAM
(`hw_v128_hot_edges`) улучшил LEVEL3, но L2 вернулся к14,93; не выбран.

`research/experiments/g20a-hw-u-track-v3/`: exact quotient tracking через остаток предыдущей
видимой колонки, до двух коррекций, затем старый division fallback. Ограничение
u0/u1<=512,q0/q1<=128,span<=16384 сохраняет positive signed products.
18 RGB кадров совпали, но live17,92/14,93/22,05 хуже выбранного.
Рост кода потребовал contains-call, а combined projection trial ещё и
coverage_first в EWRAM. Ранние варианты не влезли в IWRAM; stack reserve не
уменьшался. Не выбран, отдельной arithmetic acceptance для tracker не заявлено.
Эти флаги остаются gated experiments, не defaults.

`research/experiments/g20a-hw-v128-profile/attribution.json` — диагностический frozen profile:
L2-worst mapped-column2964 ticks/226 calls, visible-column4821/228,
CPU low ceiling983 ticks/10 surfaces, vertical strips3219/8.
Единица64 cycles, вложенные timestamps включают overhead; суммы вложенных
групп не складываются и не являются FPS. Большая часть стоимости по-прежнему
в wall/side raster. Предыдущие процедурные/BSP/opaque варианты не продвигаются
без новых доказательств выигрыша.

## Воспроизведение

```powershell
C:/Source/wasdify/.venv/Scripts/python.exe scripts/prototype_gba_bsp.py --out research/experiments/g20a-hw-fast-final --variants hw_v128_projection source_control
C:/Source/wasdify/.venv/Scripts/python.exe scripts/benchmark_gba_heading_sweep.py --build-dir research/experiments/g20a-hw-projection/hw_v128_projection --report research/experiments/g20a-hw-projection/sweep.json
C:/Source/wasdify/.venv/Scripts/python.exe scripts/report_gba_hardware_fast.py --copy-rom
```

Collector требует уже выполненные private suites и timing JSON. Полная цель
остаётся открытой: нужно ускорить тяжёлые неподвижные wall/side виды и проверить
новое решение на тех же sweep/runtime fixtures, сохраняя оговорённое качество.
