# Прямые world OBJ: первый аппаратный этап

27 октября2026, G20A. **Это частичный аппаратный прототип, не новый быстрый
рендер и не решение архитектурного сравнения.** Реальный PPU выводит исходные
стены/прозрачные изображения через affine OBJ с разбиением текстуры.54 из56
fixtures совпали с независимым расчётом; два случая со старым CPU-слоем теряют
по100 пикселей. В парном HBlank-варианте этих пропусков нет. FPS не измерялся.

Обновление: probe повторён с DISPCNT bit5 для законного HBlank OAM access;
ранняя V4 проба без этого бита заменена. Теперь регистр проверяется фактически,
включён дополнительный multiplex-small fixture. Native игровой гибрид уже
реализован и измерен отдельно: [результат и отказ](gba-world-obj-native-trial.md).
Здесь остаётся только source-art/upload proof, без claims о скорости.

## Реализовано и проверено

`ports/gba/scripts/gba_world_obj.py` разбивает исходные изображения на допустимые формы
OBJ до64×64, упаковывает8bpp tiles/OAM и диагональные affine matrices. Учитываются
прозрачный индекс0, отражение, UV crop, обрезка экраном, матрицы8.8, центр
преобразования и double-size canvas. Увеличение больше2× явно не поддержано;
такой кандидат получает причину fallback вместо учёта как готового ускорения.
Ни mip, ни процедурная замена, ни снижение детализации источников не введены.

`ports/gba/scripts/probe_gba_world_obj.py` читает исходные pixels/geometry/lighting из
контрольного native ROM2775, включая instance descriptors, и строит команды.
Вывод выполняет отдельная сборка `hw_v128_world_obj_probe`: ARM код в
`ports/gba/platform/hardware_world_obj_probe.inc` загружает tiles, palette, OAM и MMIO из
EWRAM-пакета, после чего PPU продолжает рисовать. Прямые изменения MMIO из
frontend не использованы как доказательство: они не уведомляют renderer core
об изменениях. Ранняя такая попытка оказалась некорректным тестом и заменена.

56 fixtures RU/EN проверяют:

- Стену96×192, дробный масштаб и UV crop; прозрачный источник120×58,
  отражение и выход за границу экрана.
- Обе очередности аппаратного перекрытия стены и masked объекта.
- Все16 исходных LIGHT rows для стены и masked объекта, включая сохранение
  post-LIGHT прозрачности и непрозрачного чёрного через выбранный black index.
- Смешивание с прежним программным слоем120×128, выведенным на240×144:
  тонкий ближний occluder, несколько интервалов поY, CPU-only контроль.
- Парный вывод полной смешанной композиции с HBlank OAM multiplex.

Ожидаемое изображение вычисляется по отдельным row-major patch samples;
реальный PPU читает независимо упакованные tiles/OAM. Сравниваются все38400
native GBA5 цветов, а не только PNG hashes. Это проверяет аппаратное
представление, **не эквивалентность старому Q7/affine raster целой сцены**.
Квантование центров отдельных patches может менять texel phase/швы;
приёмка картинки в движении и физическая GBA ещё впереди.

## Что показал конфликт двух OBJ-слоёв

Прежние64 affine scene OBJ занимают slots0..63 и matrix0; world OBJ в пробе
получают slots64..127 и matrices1..31. CPU-пиксели имеют ту же priority и
меньший номер OAM, поэтому закрывают world OBJ. Пустые CPU-полосы отключены.
Тонкий occluder и CPU-only проходят, полная композиция теряет100 native pixels
в каждой локализации. Эти два случая **не считаются пройденными**.

Результат согласуется с исчерпанием scanline OBJ work при перекрытии двух
affine CPU bands и новых крупных OBJ. mGBA отдельно учитывает поиск OAM и
работу активных sprites: [renderer preprocessing](https://github.com/mgba-emu/mgba/blob/master/src/gba/renderers/video-software.c),
`GBAVideoSoftwareRendererPreprocessSpriteLayer`. Это первичный код core,
проверенный27 октября; ссылка master не закрепляет исходники используемого DLL.
Фактический DLL SHA фиксируется в отчёте. Формат32 matrices и ограничение
canvas: [Tonc affine OBJ](https://gbadev.net/tonc/affobj.html).

В новом парном опыте четыре CPU OBJ переиспользуются по строкам через DMA3
HBlank, с прежней ROM таблицей row OAM. Они занимают slots4..7; matrix0 остаётся
для CPU, matrix1 резервируется под перезаписываемые DMA padding words. World
OBJ начинаются со slot8 и matrix2. В этой **той же полной композиции** пропусков
нет в обеих локализациях. Это доказательство работоспособного смешивания в
fixture, не полной видимости за всеми дверями/потолками карты.

Во время аппаратных fixtures CPU находится в диагностическом hold, IRQ/PCM
отключены; OAM DMA перезапускается опросом VCOUNT в VBlank. Стоимость этого
стенда нельзя выдавать за стоимость игрового backend. Полное сохранение cadence,
PCM и перестроение списков во время движения требуют отдельной интеграции.

## Планирование ресурсов на исходных видах

34 frozen камеры RU/EN: по четыре стартовых направления каждого мира и пять
тяжёлых видов. После freeze исходный мир создаётся заново. У914 вертикальных
граней bbox пересекает экран;386 имеют постоянную глубину и прямоугольную
проекцию,528 требуют перспективных полос и пока не реализованы. Горизонтальные
поверхности в это число не входят. **Счётчики граней не являются CPU cost.**

Для386 прямоугольников проверены реальные padded tile bytes и reuse по
одинаковым данным, OAM/matrix capacity; порядок допуска ближний первым.

|Размещение|Допущено, суммарно по34 видам|Отказы по памяти текстур|Медиана OBJ / matrices / texture bytes|
|---|---:|---:|---:|
|Все ресурсы OBJ:32KiB/128/32|254|132|19 /5 /30720|
|Гибрид со старым CPU:16KiB/64/31|156|230|6 /3 /14336|
|Гибрид с multiplex:16KiB/120/30|156|230|6 /3 /14336|

Это консервативный допуск целых прямоугольников по bbox, без финального
occlusion и без частичного допуска patches. Отказы не доказывают невозможность
оптимального atlas/cache. Например, полная стена96×192 требует18432B и6 OBJ:
она помещается в32KiB, но не помещается целиком в оставшиеся16KiB гибрида.
Видимая часть, более мелкие patches и другая раскладка могут изменить результат.
Оценка FPS из этих чисел не делалась.

## Воспроизведение и следующий шаг

```powershell
& C:/Source/wasdify/.venv/Scripts/python.exe ports/gba/scripts/prototype_gba_bsp.py --out research/experiments/g20a-world-obj-native-final --variants hw_v128_world_obj_probe hw_v128_entity_rect hw_v128_world_obj
& C:/Source/wasdify/.venv/Scripts/python.exe ports/gba/scripts/probe_gba_world_obj.py --ppu-build research/experiments/g20a-world-obj-native-final/hw_v128_world_obj_probe --out research/experiments/g20a-world-obj-bringup-final
& C:/Source/wasdify/.venv/Scripts/python.exe ports/gba/scripts/report_gba_world_obj.py
```

Probe ROM `c1903e5e242e3287afa19175ebc64c5189da21927b8eedc1c9158f111ede8a09`;
чистый repeat `research/experiments/g20a-world-obj-native-repeat/` exact, контроль
2775 exact. EWRAM261308/free836, IWRAM24408/free168 до8192 stack reserve.
Это размещение probe, не прогноз памяти готового backend. Прежние2775/96/72
artifacts сохранены; новый игровой ROM не опубликован.

Вход — GBA package SHA3efa5d…4351, координаты runtime GBA-порта и его MMIO,
а не адреса исходных EXE. EXE не анализировались и не изменялись. Источники
изображений, LIGHT rows, native PNG и SHA сохраняются в
`research/experiments/g20a-world-obj-bringup-final/`; сводка `analysis/gba/g20a-world-obj-bringup.json`.
Коллектор явно отделяет54 exact fixtures от двух отрицательных случаев.

Native admission/coverage, residency/cache, перспективные полосы и PCM/live
замеры выполнены в отдельном hybrid trial и дали отрицательный результат.
Этот probe сам по себе не выбирает архитектуру. Genuine sector clip/span
остаётся главным непроверенным направлением; local kernels не доводим.
Текущий goal заканчивается итоговым архитектурным handoff, **не реализацией
Performance-сборки**.
