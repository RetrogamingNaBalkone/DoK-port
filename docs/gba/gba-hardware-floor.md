# G20A: подготовка аппаратного вывода плоскостей

**Исторический первый шаг.** Продолжение с уже работающими BG2-плоскостями,
прямым tiled output, кешем HUD и новыми замерами —
[gba-hardware-planes.md](gba-hardware-planes.md). Ниже сохранён исходный
bring-up; его ограничения «пол ещё на CPU» не относятся к новому варианту.

27 октября 2026 года. Это продолжение [спанов/HUD/уступов](gba-span-prototypes.md),
эксперимент GBA. **15+/24,1/30 FPS не достигнуты.** Лучшие проверенные сборки
в `artifacts/gba/` остались прежними. Новый аппаратный путь пока медленнее,
а выборка пола всё ещё выполняется CPU; готового affine floor здесь ещё нет.

## Проверенный результат

Собран `research/experiments/g20a-hw-pack-ready/hw_obj_arm/dok-diagnostic.gba`, SHA-256
`cb35b35c3eadac987a8f6487e0a7a96912778f97d0272cf0eaea1f1caee2386e`.
Программная сцена120×144 передаётся как72 affine OBJ размером32×8 исходных
пикселей с горизонтальным растяжением2×; интерфейс240×160 остаётся отдельным
BG0. Пока это проверка композиции перед переносом пола в BG2.

**Все18 настоящих изображений PPU совпали по каждому RGB-каналу** с
`sides_exact`: девять камер × RU/EN, три мира. Это не чтение Mode4 framebuffer:
`benchmark_gba_hardware.py` сохраняет libretro video callback и проверяет,
что получены240×160×3 байта и активен Mode1. Обычный индексный benchmark
намеренно запрещён для этой сборки, чтобы не сравнивать содержимое тайлов
с bitmap-страницей. PNG имеют суффикс `-ppu.png` в каталоге сборки.

768 независимых ARM-проверок покрывают уплотнение240→128, перестановку8×8
тайлов и копирование UI с произвольными пикселями, нулями и pitch32/48/240.
Проверяются полные буферы, guards, callee-saved registers и SP. Прошли также
обычные sampler4096/coverage2048/cell30720/exact divide65536/panel768 и
48 исходных animation resources/308 кадров в том же ELF.

Проверен и обычный код: `main_control` перекомпилирует **текущий**
`ports/gba/platform/renderer_probe.c` без аппаратного flag. Он и обычный renderer дают базовый
ROM побайтно, SHA `7babd40f…7ea8`. Аппаратный путь не меняет production default.

Доказательства: `analysis/gba/g20a-hardware-ppu.json`,
`analysis/gba/g20a-hardware-bringup.json`, private `hw-arm.json` и
`prototype-build.json` в `research/experiments/g20a-hw-pack-ready/`. Aggregate проверяет SHA,
18 сравнений,768 ARM-векторов, контрольную сборку и22 неизменённых originals.
Внешняя документация использована только для GBA PPU; EXE не анализировались.
Все указанные0x04/0x05/0x06/0x07 адреса — GBA MMIO/palette/VRAM/OAM,
не файловые смещения, NE-сегменты, Ghidra-адреса или DPMI-селекторы.

## Ограничения видеорежима и принятый макет

Mode1 объединяет affine BG2, обычные BG0/BG1 и OBJ. Affine BG имеет до256
тайлов8×8 и карту до128×128; режимы с тайлами дают32 KiB OBJ VRAM.
Аппаратных OBJ всего128; при выключенном HBlank-free доступны1210 OBJ cycles
на строку, affine OBJ требует10+2×ширина cycles. Эти ограничения проверены по
[GBATEK, копия mGBA](https://mgba-emu.github.io/gbatek/); формат1D и double-size
поясняет [Tonc](https://gbadev.net/tonc/affobj.html).

Текущий макет, проверенный реальным PPU mGBA:

- 72 OBJ: четыре полосы по32×8 source,18 рядов. Double-size canvas64×16,
  matrix PA128/PD256, остальные коэффициенты0. Отображаются120 логических
  колонок с прежним шагом2. OAM задан на VBlank; HBlank-free не включён.
- OBJ texture —18 432 B с отступом до128 source pixels в строке,
  `0x06010000`.1D tile mapping, каждое32×8 изображение занимает256 B.
- BG0 UI —80 тайлов/5120 B с `0x06002000`, map с `0x06007800`.
  Нижние240×16 и верхние48×16/32×16 переносятся в родной ширине.
  Пустой BG0 tile127 — отдельные нулевые64 B с `0x06001fc0`.
- Исходный `frame` переиспользуется: уплотнённая сцена0…18431,
  UI tile staging20000…25119, исходный нижний HUD34560…38399.
  Дополнительные scratch1024 B и верхний UI1280 B — в EWRAM.

OBJ palette index0 прозрачен, тогда как программный непрозрачный чёрный0
должен закрывать фон. Замена0 на1 сначала потеряла12 красных пикселей HP:
индекс1 оказался красным. Исправлено без изменения цвета: выбирается ненулевой
дубликат чёрного **после** преобразования исходной палитры в GBA15-bit RGB.
В данном package это223 для LEVEL1 и255 для LEVEL2/3, у обоих языков.
Палитра не перекрашивается. Результат18 RGB-сравнений — ноль различий.
При будущем удалении программного пола понадобится различать непокрытый
фон и непрозрачный нулевой texel по coverage bits; это ещё не реализовано.

## Измеренный бюджет

Упаковка на C занимала около910 тысяч циклов. ARM уменьшил её примерно
до399–400 тысяч; это всё ещё большая дополнительная цена. Статические OAM,
UI map и palette обновляются при входе/смене view, а не каждый кадр.
Обычный upload уменьшился с77 до48 тысяч циклов и заканчивается около
VCOUNT199, внутри VBlank. Первый upload с инициализацией заканчивается
около222. Первый вариант не выбирается как готовая оптимизация.

Живые RU/EN fixtures со стрельбой и PCM22050 — примерно
**5,97 / 5,97 / 6,64 FPS** вместо7,17/7,47/8,53 у `sides_exact`.
CPU cycles включают упаковку и upload, ожидание свежего VBlank вычитается;
представленные FPS учитывают ожидание. Для single-buffer OBJ upload требуется
свежий VBlank, менять кадр в середине видимой части нельзя.

EWRAM257 044 B, свободно5100 B. IWRAM code/data23 372 B, свободно1204 B до
отдельного8192 B резерва стеков/BIOS. Heap не добавлен. Полный функциональный
suite, UI/flow/stress и физическая GBA для этого прототипа **не проверены**.
18 кадров не покрывают все water phases, кадры оружия и отдельные нечётные
пиксели оружейного overlay. Текущий240→120 compactor выбирает чётные пиксели
сцены; дополнительная потеря детализации такого overlay должна быть оценена
отдельно до выбора аппаратного пути. Полная игра не объявляется pixel-exact.

## Проверенные отрицательные пробы этого прохода

- `lookup_world`, `analysis/gba/g20a-lookup-trial.json`: ARM сам выбирает материал
  на каждом пикселе с cache текущей клетки. Девять `.idx` побайтно совпали с
  `sides_world`, но FPS хуже:7,47/7,17/8,53. Граница спана на C оказалась дешевле
  дополнительной per-pixel адресации/ветвлений. Не выбирать этот путь.
- `vertex_*`, `analysis/gba/g20a-vertex-trial.json`:5120 B exact geometry cache
  для общих вершин стен/дверей/уступов. Direct-map collisions обрабатываются
  fallback для первого endpoint. На девяти кадрах exact-картинка точная, но
  live exact LEVEL1 около7,06, coarse11,75: дополнительная адресация не окупилась.
- `ray_sides`/`ray_coarse`, `analysis/gba/g20a-ray-trial.json`: повторная проба с
  prelit уступами и общим view envelope также медленнее. Coarse9,95/9,75/11,95,
  изменены20–25 тысяч scene indices в контрольных видах. Не выбран.

Эти отрицательные пробы имеют только девять frozen камер/шесть live fixtures,
не полный regression suite. Исходники/flags/ROM SHA сохранены отдельно;
старые опубликованные кандидаты ими не заменены.

## Следующее действие

1. Писать программный foreground сразу в OBJ tile layout, чтобы убрать
   промежуточную упаковку400k. Сначала сохранить программные пол/потолок
   и повторить RGB-сравнения; затем проверить оружие, grates, clip и UI fallback.
   Координата tile address: `(y/8)*1024+(x/16)*64+(y%8)*8+(x/2)%8` при
   физических чётных `x`. Вертикальный sampler шагает8 байтов внутри тайла,
   после каждой восьмой строки добавляет ещё960. Чёрный alias определяется
   до renderer, HUD нельзя записывать поверх уже готовых foreground tiles.
2. После устранения упаковки — affine floor, HBlank DMA и material streaming.
   Собственных floor resources мало:1/5/1 для LEVEL1/2/3; исходные размеры
   96×96,48/96×48/96 и92×92. Это факты G03 package.32×32 derived textures
   и32×32 окно клеток помещаются в128×128 affine tilemap; это **проект**, не
   проверенный renderer. Цена ресемплинга и меньшего числа LIGHT rows должна
   оцениваться на native PNG и всех мирах до выбора качества.
3. Для HBlank DMA необходимы двойные таблицы и их перезапуск на каждом VBlank,
   в том числе пока CPU считает следующий кадр. Нельзя хранить активные строки
   в `frame`, который следующий draw очищает. Материалы воды требуют всех18
   crop/wrap phases; движение окна карты — обновления ушедших строк/столбцов,
   а не полной пересылки16 KiB каждый кадр. IRQ/audio stack и underruns проверить.

Не обещать, что hardware floor уже решил бюджет: текущий опыт доказал только
композицию. Если его полная цена/картинка окажется хуже спанов, не выбирать его.
Процедурные текстуры остаются отложенными по прежнему результату.

## Воспроизведение

```powershell
C:/Source/wasdify/.venv/Scripts/python.exe ports/gba/scripts/prototype_gba_bsp.py --out research/experiments/g20a-hw-pack-ready --variants baseline main_control hw_obj_arm
C:/Source/wasdify/.venv/Scripts/python.exe ports/gba/scripts/benchmark_gba_hardware.py --build-dir research/experiments/g20a-hw-pack-ready/hw_obj_arm --report research/experiments/g20a-hw-pack-ready/ppu-comparison.json --languages 0 1 --live
C:/Source/wasdify/.venv/Scripts/python.exe ports/gba/scripts/verify_performance_arm.py --build-dir research/experiments/g20a-hw-pack-ready/hw_obj_arm --report research/experiments/g20a-hw-pack-ready/hw-arm.json
C:/Source/wasdify/.venv/Scripts/python.exe ports/gba/scripts/report_gba_hardware_bringup.py
```

Приведённые команды действительно выполнены. Общие инструменты —
[building.md](../building.md); private ROM не копировался в `artifacts/gba/`.
`ports/gba/platform/renderer_probe.c` меняется только за `HW_OBJ`; ARM pack — за отдельным
`HW_PACK_ARM`. G20/G20A остаются открыты; G20B/G21 ожидают.
