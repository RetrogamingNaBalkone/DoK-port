# G20A: геометрия и отдельный прототип128 строк

27 октября2026. **Goal/G20/G20A открыты:** LEVEL2 в длинном повороте14,75 FPS,
поэтому15+ во всех обычных сценариях,24,1 и30 не достигнуты.
Отдельный `artifacts/gba/DoK-port_hardware_128_prototype.gba`,10 846 124 B,
SHA-256 `16ba83e8efd86169abec4206b720dd7a59fbfc1de85dc4ac2580b098296b3961`.
ELF/map/частные исходники — `build/g20a-hw-face-box/hw_v128_box/`;
сводка и хеши свидетельств — `analysis/g20a-hardware-rows.json`.
Прежние pre-alpha/exact/coarse/BSP/hardware/culling ROM сохранены.
Выбор качества для release не сделан. SDL2 и оригиналы не менялись.

## Изображение и изменения

Это продолжение [аппаратного отсечения](gba-hardware-culling.md).
CPU рисует стены/актёров/низкие потолки в120×128 вместо120×144.
64 affine OBJ растягивают строки8→9: повторяется строка4 каждого блока.
Проекция CPU4/5 вместо9/10, матрица OBJ PD227. Пол/базовый потолок BG2
по-прежнему144 строки; BG0 интерфейс и оружие сохраняют исходное разрешение.
Сборщик меняет только частные renderer copies; без flags базовый ROM прежний.
На вертикальных текстурах видна потеря детализации; это отдельная проба качества.

Новые оптимизации, не меняющие выбранное изображение:

- `HW_COLUMN_CACHE`: одинаковый LIGHT на концах монотонного depth-интервала
  позволяет переиспользовать строку; повторяющаяся высота соседней колонки —
  прежний Q16 шаг. Пределы depth проверяются до выбора кеша.
- `HW_CELL_CLASS`:1536 ROM bytes масок классов исключают пустые вызовы
  `cell_sprites`. Текущий MMAP читается каждый раз; неподдержанный glyph
  передаётся старому helper для сохранения ошибки. Разрешённые классы TM/MM
  совпадают со старым фильтром. Актёры/анимация/лимиты не удаляются.
- `HW_FC_SEED`: первая проба базовой плоскости в1920 единицах перед камерой,
  внутри прежнего envelope. Вызывает прежний builder для inset0..10;
  базовые GPU плоскости не добавляют CPU faces. Лизы рассмотрения монотонны;
  последующая общая обработка/игровая логика сохранены.
- `HW_FACE_BOX`: перед подготовкой растеризации проверяет coverage во всём
  консервативном прямоугольнике грани. Отсечение возможно лишь при полном
  покрытии.1024 независимых ARM vectors проверяют ответ, неизменность битов,
  границы descriptor, callee-saved registers и SP.

Инструментированный geometry profile отдельно показал в L2-worst30 вызовов
боковин/75 surfaces/117 sprite cells. Последняя группа стоила1194 timer ticks
с накладными расходами профиля. Это основание для class filter, не FPS-замер.
Источник — `build/g20a-hw-geometry-profile/attribution.json`; единица timer64 cycles.

## Полная игра с runtime-камерой и PCM

mGBA libretro,16 777 216 cycles/s, WAITCNT4317, replacement BIOS, без overclock.
Хеш ядра5cac11…667e4. Capture hold выключен во всех интервалах измерения.
Python fast-video откладывает только преобразование RGB. FPS включает ожидание
предъявления; coherent logic snapshot соответствует завершённому кадру.

| Сценарий | LEVEL1 | LEVEL2 | LEVEL3 |
|---|---:|---:|---:|
| Поворот+огонь24 предъявления, RU |17,70|14,78|22,40|
| То же, EN |17,70|14,78|22,05|
| Поворот+огонь120, RU/EN |18,71|14,75|20,13|
| Ходьба+поворот+огонь120, RU/EN |16,55|16,00|19,27|

Во всех18 fixtures: audio underruns0, cold loads0, upload overflows0,
motion conservation error0; flow gameplay/UI playing, minimum gap2 VBlank.
Observed upload budget margin не меньше5496 cycles; universal bound не доказан.
EWRAM260944/free1200; IWRAM24500/free76 до stack reserve8192.
Observed main stack2528/margin5408, IRQ80/margin80. Запас кода IWRAM мал.

Отдельная144-строчная версия `hw_cell_seed` не ухудшает изображение относительно
предыдущего hardware ROM:18 проверенных кадров exact. Короткий поворот
16,48/14,05/19,64; длинный17,48/13,92/18,71 (EN L3 18,86);
ходьба15,86/15,58/18,81. Она пока частная, без наследования полной приёмки128.

## Выполненная проверка и границы

-396 actual PPU RGB кадров128 совпали с исходным128 вариантом до новых
  оптимизаций. Сравнение с прежним144 ROM меняет до11898 scene pixels;
  overlay UI/weapon различий0 во всех396 cases.
-282 camera/weapon/message transitions, все1633 PPU callbacks совпали с
  целым старым либо новым кадром; смешанных0, overflow0.
-Dedicated CPU-written OBJ row-color probes128/144 проверили все160 строк PPU.
  Прямая запись через Python pointer в MMIO/VRAM обходит renderer side effects
  mGBA, поэтому для этой проверки она не используется.
-ARM1536 tiled,512 masked,256 map,1024 face box и прежние arithmetic/animation
  vectors прошли.48 исходных animation resources/308 frames неизменны.
-UI22 actual PPU кадров, исходные font/icon masks, полный flow,8 playback
  sequences/18 sprite captures, AI perception/motion/wake,28 AI combat groups,
  interactions logic, rational clock и poisoned reset прошли.
-Новая water harness проверила144 cases:18 phases×4 moving materials×RU/EN
  в LEVEL2, все3 LIGHT VRAM bands относительно оригинального RES и362232
  unobstructed actual PPU pixels. Других moving GPU materials в текущих мирах
  нет. Это проверка32×32 crop/wrap/загрузки/композиции, не исходной affine геометрии.
-Чистая повторная сборка byte-exact; source control7bab…7ea8;
  все22 исходных SHA совпали с inventory.

UI/flow/sprites/water дополнительно прошли на прежнем опубликованном144
ROM240669…f014 (`build/g20a-hw-early-qa/{ui,flow,sprites,water}.json`);
старые документы отражают прежний срез. Адаптированная sprite harness также
прошла на базовом Mode4 ROM7bab…7ea8.
UI harness проверяет actual PPU color перед восстановлением исходного индекса
непрозрачного BG0 для font/icon oracle. Tiled VRAM не выдаётся за bitmap.
Sprite harness читает CPU geometry до ожидания PPU и синхронизирует mailbox
записи с завершённым кадром; иначе следующий draw мог уже менять faces.

Integrated capacity/all-awake stress проверил guards/stack/audio, но **не бюджет**:
pool capacity11,56..18,86 FPS, all-awake0,07..0,37 FPS с растущим motion debt.
Та же проблема есть в `analysis/g20-game-profile.json` (0,07..0,36).
Нельзя называть этот `status=passed` подтверждением производительности.
Это искусственные нагрузки с диагностической камерой, отдельно от таблицы runtime.
Исходный affine interaction pixel oracle намеренно отключён для этого quality trial.
Независимый combat-effect pixel oracle и item presentation ещё не адаптированы;
физическая GBA/оригинальный BIOS, весь уровень и minimum FPS не проверены.

## Остальные опыты

Все перечисленные ниже остаются частными и не вошли в опубликованный128 ROM.
Их source/ELF/manifest/JSON лежат в соответствующих `build/g20a-hw-*`:

| Каталог/flags | Результат |
|---|---|
|`rims`, HW_MERGE_RIMS|18 RGB exact, CPU выигрыш0..1,34%, live почти не меняется|
|`affine`, HW_AFFINE_U+HW_INTERVAL_EWRAM|18 RGB comparison; exact U edge walk не окупил перенос240B coverage в EWRAM|
|`urecip`, HW_U_RECIP|unsigned reciprocal1..65535; малый live выигрыш, отдельно не выбран|
|`anim-cache`, HW_ANIM_CACHE|1024B EWRAM cache без измеримого выигрыша; не расходовать этот запас|
|`cell-near`, HW_NEAR_FC|консервативный near-ceiling skip,18 RGB exact; неоднозначный live результат|
|`ceil-prelit`, HW_SURFACE_PRELIT|CPU low-ceiling16 LIGHT bank;18 RGB exact, выигрыш мал|
|`combo`, HW_CONTAINS_CALL/recip/prelit/near|396 RGB exact против128 box; в L2 до14,93, остальные уровни иногда хуже|
|`wide`, HW_U_WIDE|256734 positive-divide и1024 box vectors,396 RGB exact; normalization не ускорила live|
|`affine-div`, HW_AFFINE_DIV|сокращение U при равном Q;17,92/14,78/22,05, существенно не лучше выбранного|

Unsigned helper отдельно прошёл196605 vectors в combo ARM report.
Широкий divide имеет контракт n0..INT_MAX,d1..INT_MAX: нормализация к16 bits,
приближение отличается максимум на1, trial product помещается вuint32.
Код gated; это результат проверки, не причина включать его в лучший ROM.
Предыдущие процедурные/BSP/opaque/gaps варианты остаются отложенными.

## Воспроизведение

```powershell
C:/Source/wasdify/.venv/Scripts/python.exe scripts/prototype_gba_bsp.py --out build/g20a-hw-face-box --variants hw_face_box hw_v128_box
C:/Source/wasdify/.venv/Scripts/python.exe scripts/verify_gba_hardware_water.py --build-dir build/g20a-hw-face-box/hw_v128_box --report build/g20a-hw-face-box/water.json
C:/Source/wasdify/.venv/Scripts/python.exe scripts/report_gba_hardware_rows.py --copy-rom
```

Collector требует фактически полученные private QA/quality/live/repeat JSON и
проверяет хеши; он не запускает suite. Следующий шаг — L2 wall/low-ceiling raster
и geometry, завершение независимых hardware pixel regressions. Не закрывать
goal по walking-only результату или за счёт исключения искусственного stress.
