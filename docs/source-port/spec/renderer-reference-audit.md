# SP07: сверка полного VGA-кадра, 4 ноября 2026

Промежуточное проверенное доказательство. Воспроизводимый скрипт:
`py -3.14 -B ports/sdl2/tools/check_scene_reference.py`.
Полные отчёты и кадры: `research/evidence/source-port/sp07-render/`.
Эталоны `research/evidence/reference/{ru-03,en-02}/level1-start.idx` не менялись.

| Профиль | Обычный deterministic raster | Replay исходной истории LIGHT |
|---|---:|---:|
| PKR LEVEL1 | 49999/51200 | 51200/51200 |
| PKE LEVEL1 | 50146/51200 | 51196/51200 |

PKR Ghidra NE `1120:0bb7` и `1120:0bef..0c13`: перед обходом вершин
инициализируется только минимум. DS:c146, индекс максимальной вершины,
переписывается `1120:0c03` только при строго большем major coordinate.
Если максимум уже vertex0, индекс остаётся от предыдущего полигона.
Vertical raster использует ту же переменную. При floor/ceiling это меняет
интерполяцию LIGHT, хотя геометрия совпадает. Сохранённые Ghidra листинги:
`research/evidence/rendering/{ru,en}/evidence.asm` и `sprites/{ru,en}/evidence.asm`.
В production сохранена инициализация high=0: намеренное исправление зависимости
освещения поверхности от предыдущего draw. Diagnostic RasterReferenceState
принадлежит экземпляру сцены, не global; включается только render_probe флагом
`--native-light-history`. Не используется в обычной игре.

Остаток PKE: (103,58), (108,60), (108,100), (105,101).
Точки находятся на стыках соседних floor/ceiling quads. Причина ещё проверяется;
не следует приписывать их анимации без доказательства.

Каждый полный массив (x,y,depth) первичных/вторичных полигонов сериализован как
little-endian signed int32 и найден целиком в 16MiB исходной гостевой RAM:
PKR213/214, PKE214/215. Отчёт содержит каждый linear_ram_offset. Это линейные
адреса RAM, не Ghidra адреса и не DPMI selector*16. Базы data image:
PKR0x1d38e0/PKE0x1d7090; см. originals/rendering-reference.md.
Ненайденный массив соответствует class9: исходный снимок уже содержит движение
монстра. В PKE найдена реальная pose по RAM0x200fcc=(12555,0,24720,270),
native type9 по object+0x12, object base0x200fa6. Его quad RAM0x302781:
(244,54,2565),(281,54,2565),(281,100,2565),(244,100,2565).
Точная идентификация host glyph и влияние на сортировку пока не приняты.

OG/VESA и контрольные позы всех уровней пока не покрыты этой сверкой.
Этот результат не закрывает весь SP07 и не является gameplay parity.

## Новые DOS-опыты OG/VESA

`capture_og_reference.py`: изолированная копия OG.EXE -n, input через
исходную таблицу DS:c563/count:c560/scan:c562 (2800:006a/025d/02f2).
Корректный data anchor `Not same device` по Ghidra3255:189d; runtime DS23f8,
линейная база0x23f80. GAMEPAL как anchor неверен: строка найдена в каталоге RES,
а не DGROUP; первый og-dos-01 не используется как игровой эталон.
`og-dos-02/enter-6.idx` — стабильный LEVEL4, цветной `level4-color.png` использует
GAMEPAL из оригинального RES. PNG от generic capture — только grayscale indices.
Оригинальные файлы SHA до/после совпали.

OG203d:00cb и RAM data+1fba: D200,W320,H171,cx159,cy84;
clip data+1fc6=(0,0,319,169),scale data+1fd6=8.
Camera pose data+1fea=(8904,-200,15960),sin/cos32523/3993 (heading83).
SoftwareScene исправлен на170 строк OG, panel29 вместо39. GL сохраняет современный
160-row/Hor+ viewport как явную адаптацию; это не native OG projection.
Старое сравнение после одной коррекции камеры:25785/54400 индексов.

Обнаружено принципиальное отличие OG от Kremlin: OG рисует горизонтальными
Bresenham scanlines с полушагом UV и исключительным правым концом,
image.width/height вместо width-1/height-1. Ghidra2b57:0b36/0d35/0f01/1301;
far/LES, signed WORD edge deltas, wrap32 UV ADD/IDIV проверяются по ASM.
Каждый polygon выбирает одну LIGHT row через2ce3:0160. Constructors
24cf:000b/07d8/1363/1a81 используют abs16(lateral)+abs16(depth),
WORD sum/sub1008, clamp0, /154; billboard24cf:0f01 использует исходный world dx
и rotated depth, sub2184. Clamp row15 в palette setter. Billboard ASM0fb4/0fbe
подтверждает world dx, а не rotated lateral. Реализация в работе, финальная
сверка пока не заявляется. Свежие disassembly missing draw entries06a5/1271
получены через ExportSourcePortRender.java в readOnly Ghidra session.

VESA capture через`capture_vesa_reference.py`: PKR -r/-r2/-r3/-r4, S3 DOSBox.
В полной32MiB RAM найден LDT: runtime DS04df descriptor0x1ff0208 имеет
base1d38e0, limitf0ee; LDTbase1fefd30. Surface selector065f:0000,
descriptor1ff0388=ffff40721ef20000 -> linear1e7240. Surface большой длины
читается из contiguous guest RAM, selector не умножается на16.
Исходный HTTP API LFB0xe0000000 возвращает500; он не использован.

| Запуск | Размер | Height argument | Clip bottom | K |
|---|---|---:|---:|---:|
| -r / -r2 |640x480|386|385|8192|
| -r3 |800x600|483|481|10240|
| -r4 |1024x768|618|617|13107|

В этом DOSBox -r реально выбирает640x480:640x400 пока не получен.
Снимки/полные RAM: ru-vesa-{2,3,4}-02. Первичный ru-vesa-auto содержит
transient message и не является стабильным эталоном.800x600 native-history
probe385141/385600;459 отличий ещё не локализованы. Остальные сравнения pending.

## Getter audit и текущая регрессия

Class6 **не имеет отдельного image getter**. Свежие Ghidra data bytes:
PKR1190:063d vtable slot+8 =1068:04ae;
PKE10d0:0656 slot+8 =1020:098e;
OG3255:0612 slot+8 =1ae1:0389. Это base getter SP06, уже подключённый
ImageGetter/Animations. Наличие SwitchState native54/56/58 не является
доказательством отдельной анимации class6. Его use/callback остаётся SP10.
Дверьclass7 имеет отдельный crop getter PKR1068:320b, vtable1190:0625+8;
Scene применяет door_image перед геометрией. Class12 сохраняет документированную
адаптацию fixed-tick18 phases вместо redraw-side-effect native interval0.

OG raster после уточнения default UV wrapper2b57:0a0d даёт **54393/54400**
на LEVEL4. Семь открытых pixels: (105,42),(106,43),(108,46),(110,49),
(74,80),(10,83),(313,122). Это не объявлено полной pixel equivalence.
Geometry166/168 массивов полностью найдены в исходной RAM; два ненайденных
массива material79 относятся к центральным вертикальным плоскостям.
Default floor UV=width-1/height-1; explicit wall/billboard UV=width/height.
Раннее утверждение «все UV=dimension» выше уточняется этим wrapper.

GL получает OG постоянный palette row отдельным shade_depth атрибутом вершины;
геометрическая глубина, perspective, depth test и shader filters не изменены.
Modern GL170/native viewport не заявлен: сохранены160/Hor+ и современный HUD.
Software classic panel использует29 строк OG; weapon bottom берётся из170-row
scene, соответствуя исходному clip.bottom169 (OG1ae1:2959).
PKR/PKE camera/HUD остались прежними. Legacy fixed0 lighting и native-history
обе ветви покрыты тестом, включая перенос high index между разными полигонами.
OG raster тестирует half-step/exclusive bounds и raw transparency перед LIGHT.

`dok_animation_scene_tests`:35 software scenes,20 real clips с software/real GL
readback, reversal/fresh-cache/repeated draw, без изменения simulation ticks.
`dok_old_gold_probe`:600 rendered simulation frames,105 money,2 wands,exit PASS.
`check_render.py`: исходные дверные texels PKR1622/PKE1388 PASS.
