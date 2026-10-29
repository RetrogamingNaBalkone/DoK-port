# N06: исходная камера и численный контракт 4:3

29 октября 2026 года. Это основание проекции NDS до развития renderer.
Исходные факты ниже проверены по сохранённым Ghidra C/ASM и DOS RAM;
правила непрерывной камеры и округления — явно выбранная адаптация NDS.
Код GBA не используется. Статус всей N06 определяет [паспорт](../tasks/N06.md).

## Доказательства и адресация

Образцы PKR.EXE и PKE.EXE закреплены в [N01](N01-evidence.md).
Их SHA-256 соответственно
`12f78afc0eba05f76b5dc0bb99da38d7daa20585024c0ba958fe828aa3864d28` и
`cce7bc5993d0734ad87e93db37a70b8b52faedfff14ac48d6e317e5ac996a37b`.
Повторная read-only проверка всех четырёх EXE/RES до/после выполняется командой:

```powershell
C:/Source/wasdify/.venv/Scripts/python.exe -X utf8 ports/nds/tools/audit_camera.py
```

Результат — `research/evidence/nds/n06/camera-audit.json`: SHA входов, проверенные
инструкции, поля камеры, четыре исходные render records и рациональные целевые
координаты. Проба не импортирует GBA-модули, не запускает новый DOS-сеанс и
не заменяет исходный EXE новым C. Сохранённые `annotated.c` — автоматический
псевдокод; доказательство арифметики берётся из соответствующего `evidence.asm`
в `research/evidence/rendering/{ru,en}/`.

Все адреса функций — **Ghidra NE-loader**, не файловые смещения и не DPMI
selectors. Камера: RU `1100:00F5`, EN `1060:1025`; central vertical plane:
RU `10B0:0000`, EN `1040:0000`; billboard: RU `10B0:0CCB`, EN `1040:0CE3`.
В сохранённых RAM `research/evidence/reference/{ru-03,en-02}/level1-memory.bin`
CRT anchor определяет линейные базы данных `0x1D38E0` и `0x1D7090`.
DS offsets ниже прибавляются к этим базам. Формула `selector*16` не применяется.

## Проверенная камера DOS

| Параметр | RU | EN | Значение |
|---|---|---|---:|
| D, signed32 | DS+AA16 | DS+AAC6 | 200 |
| Width / height argument, signed16 | AA1A / AA1C | AACA / AACC | 320 / 161 |
| Principal point, signed16 | AA1E / AA20 | AACE / AAD0 | 159 / 79 |
| Clip L,T,R,B, signed32 | AA22…AA2E | AAD2…AADE | 0,0,319,159 |
| Viewport scale k, signed16 | AA32 | AAE2 | 8 |
| Sin / cos, Q15 signed32 | AA3E / AA42 | AAEE / AAF2 | 32768 / 0 |
| Focal K, signed16 Q12 | **0934** | **0A44** | 4096 |
| Camera X,Y,Z, signed32 | AA46 / AA4A / AA4E | AAF6 / AAFA / AAFE | см. ниже |

Сдвиг +B0 применим к приведённым camera globals, но **не к focal**.
Начальная RU-поза `(15120,−300,22800)`, исходная EN-поза
`(15120,−300,23280)`; heading 90. Камера копирует позицию героя, без добавления
отдельной высоты глаз. NDS использует RU-мир для обоих языков; исходный сдвиг
EN на 480 по Z не переносится в NDS-переключение языка.

Камера получает W=320,H=161,k=8. Signed SAR и раздельное вычисление половин
границ дают `L=0,R=319,T=0,B=159,cx=159,cy=79`. Поэтому H=161 не означает
161 видимую строку. Реальное игровое поле — 320×160, HUD — строки 160…199.
Все 64000 исходных VGA индексов сверены с памятью `0xA0000…0xAF9FF` обоих RAM.

Исходные функции используют far calls / RETF, DS=SS, far pointers через ES;
вызывающий удаляет аргументы. В NDS не переносятся адреса/ABI DOS. Для численной
проверки важны signed16 расширения, low32 IMUL/ADD/SUB, SAR15 и IDIV с усечением
к нулю. В частности, нельзя молча превратить исходный low32 multiply в точное
64-битное произведение.

```text
dx = wrap32(X−Px); dz = wrap32(Z−Pz)
lateral = SAR15(wrap32(wrap32(dx*cosQ15)+wrap32(dz*sinQ15)))
depth   = SAR15(wrap32(wrap32(dz*cosQ15)−wrap32(dx*sinQ15)))
N = wrap32(wrap32(sign16(k)*D)*sign16(K)) = 6553600
q = trunc0(N / wrap32(depth+D))
sx = wrap32(cx + SAR15(wrap32(lateral*q)))
sy = wrap32(cy + SAR15(wrap32(wrap32(Y−Py)*q)))
```

Проверяемые участки: RU `10B0:01BB…0265`, `0D9B…0E0C`, EN
`1040:01C1…026B`, `0DB9…0E2A`. При heading0 forward=+Z, right=+X;
при heading90 forward=−X, right=+Z. Положительный Y направлен вниз.
Положение героя Y=−300 означает 300 world units над полом Y=0.
Cell Side=480, Cell Height=960; у стандартной FCMap плоскости extent=951
и ceiling Y=−951, но FCMap height parameters меняют это значение. Нельзя
заменять все высоты одним кубом. См. [исходную геометрию](../../originals/rendering-geometry.md).

## Непрерывная интерпретация и обзор

Если убрать только целочисленное квантование, `focal=200` исходных raster pixels
и `eye_z=depth+200`. Эффективный глаз расположен на 200 units позади опорной
точки вдоль forward. Обычный `f/depth` без смещения меняет ближнюю геометрию.
Горизонт в index coordinates проходит через y=79, а не через y=80.

В NDS непрерывные исходные границы определяются внешними краями пикселей:
X от −0.5 до 319.5 и Y от −0.5 до 159.5. Это соглашение ресэмплинга/GPU N06;
в EXE нет найденной константы «FOV 77°» и не доказан отдельный subpixel origin.

| Луч от эффективного глаза | Отношение к eye_z |
|---|---:|
| Left lateral | −319/400 = −0.7975 |
| Right lateral | 321/400 = 0.8025 |
| Top −dy | 159/400 = 0.3975 |
| Bottom dy | 161/400 = 0.4025 |

Обзор между этими лучами: horizontal **77.3194034812°**, vertical
**43.6026060730°**. Симметричные оценки `2atan(160/200)` и `2atan(80/200)`
удобны для описания, но теряют проверенную асимметрию центра. Для GPU используется
асимметричный frustum; число 256/154 само по себе не задаёт его aspect/focal.

## Перевод в 256×192 и граница 154/38

DOS mode13h имеет физический pixel aspect 1:1.2. Полный 320×200 кадр показывается
в 4:3; точное преобразование полного статического экрана — X×4/5, Y×24/25.
Сцена 160 строк в этом масштабе имела бы 153.6 строки. Округление границы до 154
вводится **по отдельным областям**, а не добавлением видимой мировой полосы:

| Область | Исходные outer edges | Целевые outer edges | Масштаб |
|---|---|---|---|
| Весь X | 0…320 | 0…256 | 4/5 |
| Игровая сцена Y | 0…160 | 0…154 | 77/80 |
| HUD Y | 160…200 | 154…192 | 19/20 |
| Полный статический Y | 0…200 | 0…192 | 24/25 |

В этой таблице edges — координаты от внешнего левого/верхнего края; index
coordinate центра пикселя на 0.5 меньше. Для уже спроецированного source index:

```text
target_x_index       = (sx+1/2)*4/5 − 1/2
target_scene_y_index = (sy+1/2)*77/80 − 1/2
target_hud_y_index   = 154 + (sy−160+1/2)*19/20 − 1/2
```

Principal point NDS равен **(127.1,76.01875)** в index coordinates, либо
(127.6,76.51875) в edge coordinates. Continuous focal — **(160,192.5)**.
Ровно те же крайние исходные лучи занимают всю сцену 154 строки; вертикальный
обзор не расширяется. Отклонение вертикального масштаба от идеальных 192/200
равно **1/384 = 0.2604167%**, это неизбежное округление выбранного целочисленного
раздела 154/38, не изменение FOV. Нельзя использовать fy=192 вместе с полной
высотой 154: это добавило бы 0.4167 исходной строки обзора.

Для nearest reference sampling target pixel centres обращаются в source так:

```text
source_x        = floor((2*x+1)*5/8),             x=0…255
source_scene_y  = floor((2*y+1)*40/77),           y=0…153
source_hud_y    = 160+floor((2*(y−154)+1)*10/19), y=154…191
source_static_y = floor((2*y+1)*25/48),           y=0…191
```

Все входы source_scene_y ограничены 0…159, source_hud_y — 160…199: нет чтения
из HUD в сцену, обрезки исходного обзора или ошибочных полей для растра 8:5.
При reflow HUD исходные значимые показатели сохраняются по [визуальному контракту](N06-visual.md);
эта формула нужна для исходного композиционного эталона, а не запрета крупного текста.

## Связь с аппаратной перспективой N07

Для положительного forward eye_z=depth+200 и dy=Y−Py требуются отношения:

```text
clip_x = (5/4)*lateral − eye_z/320
clip_y = −(5/2)*dy + eye_z/160
clip_w = eye_z
NDC_x = clip_x/clip_w
NDC_y = clip_y/clip_w
```

При viewport 256×154 они дают указанные focal и principal point; Y NDC направлен
вверх. Все координаты/плоскости можно равномерно перевести в единицы клетки
480 без изменения отношений. Реальная fixed-point матрица DS, viewport origin,
точность деления и clipping проверяются в N07 по указанным rational points;
N06 не объявляет эмуляцию GPU уже выполненной. Нельзя подменять это стандартным
симметричным `perspective(fov,256/154)`.

Для vertical planes исходное отсечение происходит при native depth 0, то есть
на eye_z 200; billboard требует depth>0. Это **не** eye_z 0 и не плоскость через
эффективный глаз. GPU near/far и правила пересечения с ними должны сохранять
доказанную видимость либо фиксировать адаптацию отдельно. Дальность source
cell-builder AABB не является найденной frustum far plane. Конкретный far,
масштаб мировых координат и запас precision принимаются в N07.

У horizontal planes отрицательная depth имеет особую ветку: после исходного
SAR15 выполняется условный SAR5 (`10B0:118F…11D7` и `1760…17A8` RU).
Результат равен SAR20 исходного low32 rotation numerator. Это отличается от
деления отрицательной уже целочисленной depth на 32 с усечением к нулю:
например, SAR5(−1)=−1, но trunc0(−1/32)=0. Это не отсечение полигона, и ветку
нельзя объявлять эквивалентной GPU clipping. Точный material/light, вода и
отражения остаются N13; никаких данных этой ветки не удалять ради удобной камеры.

## Численные fixtures высот и ближней границы для N07

Тот же audit генерирует `research/evidence/nds/n06/height-near-fixtures.json`.
Для всех использованных FCMap glyph трёх RU LEVEL он читает закреплённую N05
base-модель, сверяет её source_hex с реальными байтами RU RES и сохраняет:
source index/SHA, resource/file offsets, строку определения, диапазоны токенов,
native offsets0x19/0x54, количество клеток и примеры source/runtime координат.
Исходный `.` нормализуется в space перед lookup, строки карты отражаются:
source `(x,y)` соответствует runtime `(x,63−y)`.

Проверены инструкции NEG16+CWDE для floorY (`1098:35E3…35EB`),
INC16/SUB16 для extent (`3612…3626`) и branch одной/двух плоскостей
(`36A0…36B4`). Подтверждённая формула при offset54≥0:

```text
floorY  = sign16(NEG16(height_parameter_at_19))
extent  = sign16(CellHeight+1−parameter_at_54−height_parameter_at_19)
ceilingY = wrap32(floorY−extent)
```

| Уровень / glyph | +0x54 | +0x19 | Floor / ceiling | Клеток с TMap не class2 |
|---|---:|---:|---|---:|
| LEVEL1 space | 10 | 0 | 0 / −951 | 2668 |
| LEVEL1 `+` | 400 | 0 | 0 / **−561** | 383 |
| LEVEL2 space | 10 | 0 | 0 / −951 | 1671 |
| LEVEL2 `<` / `>` / `{` / `}` | 400 | 0 | 0 / **−561** | 386 / 341 / 127 / 141 |
| LEVEL3 space | 10 | 0 | 0 / −951 | 2415 |
| Все LEVEL, border `=` | 0 | 0 | 0 / −961 | **0** |

LEVEL3 не имеет used FCMap с 400. Текстовая таблица сокращена: fixtures также
сохраняют class12/13 glyph, количество placements и до трёх примеров клеток
каждого glyph, с отдельным счётчиком cells под стеной. Нельзя
выдавать border `=` за существующий открытый коридор. Для LEVEL1 `+` первый
пример — source `(2,2)`, runtime `(2,61)`, offset 20294 в LEVEL1 и 423536 в RES;
numeric 400 находится в resource span `(7485,3)`, native object+0x54.
Это не объявление поля уроном или его неизменности после игровых событий.

Для визуального контроля предоставлены **синтетические**, явно не DOS-снимки,
вертикальные прямоугольники с camera `(0,−300,0)`, heading0, depth1440,
lateral±240, floor0 и ceiling−951/−561. Исходный q=3996. Native bounds:
`[129,−1,188,115]` и `[129,47,188,115]`; mapped NDS bounds:
`[103.1,−0.98125,150.3,110.66875]` и
`[103.1,45.21875,150.3,110.66875]`. Horizon —76.01875 в обоих случаях.
Их задача — отличить правильную низкую высоту от однотипных кубов; они не
доказывают полный source wall builder, texture V или GPU raster.

Near fixtures отдельно фиксируют vertical endpoint depths −1/0/+1 и−32/+32,
U0…95 и lateral−120…120: оба depth≤0 отбрасываются, смешанная negative сторона
интерполируется до 0, нулевая сторона допускается при другой положительной.
Billboard требует строго depth>0. Horizontal fixtures проверяют сначала
all-corners≤0 reject, затем условный SAR5, границы SAR15/SAR20 и denominator.
Adversarial adjusted depth−200 даёт divide fault, но **не объявляется достижимой
реальной клеткой**: для mixed-sign480-unit cell depth span≤ceil(480√2)=679,
negative adjusted depth≥−22 и denominator≥178. Такие ограничения нужны N07,
чтобы отличать найденное source поведение от произвольных invalid inputs.

## Контрольные точки и критерии сравнения

Четыре реальные door records подтверждают исходные bounds без подбора FOV:

| DOS версия / клетка | DOS bounds x0,y0,x1,y1 | NDS bounds в index coordinates |
|---|---|---|
| RU 24,16 | 145,59,172,95 | 115.9,56.76875,137.5,91.41875 |
| RU 24,17 | 118,59,145,95 | 94.3,56.76875,115.9,91.41875 |
| EN 24,16 | 118,59,145,95 | 94.3,56.76875,115.9,91.41875 |
| EN 24,17 | 91,59,118,95 | 72.7,56.76875,94.3,91.41875 |

Это mapped native integer vertices, с уже совершившимся DOS квантованием.
Continuous GPU projection может отличаться от них, потому что не обязана
сначала квантовать q и координаты в DOS. Ожидаемые continuous точки и native
reference points должны сравниваться раздельно; совпадение только screenshots
не доказывает правильность transform, clipping, LIGHT или порядка перекрытий.

Контроль аспекта: нарисованный исходный эллипс с радиусами 60×50 pixels при
DOS pixel aspect 6/5 визуально круглый. После NDS scene mapping радиусы 48×48.125,
разница 0.125 px объясняется округлением 153.6→154. При full-screen mapping они
ровно 48×48. Важно: равные world X/Y размеры исходная камера проецирует в равные
raster spans, которые DOS-монитор растягивает по Y; такой объект не становится
физически круглым от одного требования «сохранить DOS4:3». Нельзя исправлять
авторскую проекцию на современную изотропную без отдельного решения.

N07 проверяет rational camera и отсутствие дополнительного обзора до оценки
sampling; N13 отдельно проверяет palette/LIGHT/transparency/water; N19 —
исходные HUD-показатели, большие числа, иконки и полные RU/EN тексты. Физические
проверки и FPS отложены до итоговой N24 по пользовательскому поручению.

## Небольшой C11 API для последующих задач

`ports/nds/include/dok/projection.h` и `ports/nds/core/projection.c` содержат
scalar numerical oracle, без renderer, allocation, SDK или floating point.

| Тип | Состав | Размер в host/ARM9 gates |
|---|---|---:|
| dok_projection_camera | XYZ32, sin/cos32, D32, k/K/cx/cy16 | 32 байта |
| dok_projection_vertex | XYZ32 | 12 байт |
| dok_projection_sample | screen X/Y32, depth32, factor32 | 16 байт |
| dok_projection_q16 | target X/Y signed Q16 | 8 байт |

`dok_projection_native()` принимает camera и vertex, повторяет описанные
low32/SAR15/IDIV и выдаёт sample. Нулевой делитель и INT32_MIN/−1 возвращают
`DOK_PROJECTION_DIVIDE_FAULT` вместо host UB; исходный DOS получал divide error.
Это отличие способа сообщения об ошибке, а не скрытая замена результата нулём.
Scalar sample остаётся до 16-битного raster narrowing и не выполняет visibility,
near clipping, width/height halves или horizontal SAR20. Эти операции относятся
к конструкторам плоскостей и будущему renderer, а не к данному API.

`dok_projection_map()` принимает SCENE/HUD/STATIC и global source index
coordinates. HUD y задаётся в 160…199. Offscreen coordinates допустимы; функция
не обрезает полигоны. Точные half-pixel дроби округляются до ближайшего Q16,
при равенстве — от нуля. Ошибка ≤ 1/131072 px. При переполнении signed32 Q16
возвращается `DOK_PROJECTION_RANGE`. Все 64-битные промежуточные map значения
ограничены <2^55 даже для полного signed32 входа. При любой ошибке обеих функций
output остаётся неизменённым.

Независимый Python audit сохраняет 5002 native и 1525 map TSV-вектора в
`research/evidence/nds/n06/`, SHA и количество строк включены в camera-audit.
4000 native inputs используют допустимые map-range координаты и Q15 trig,
1000 — произвольные 32-битные параметры с проверкой wrap; два отдельных control
вектора требуют divide fault. Проверяются и положительные, и отрицательные
products/SAR/IDIV. Mapping проверяет все области, offscreen/отрицательные
координаты, полный signed32 предел и сохранение результата при отказе.
Фиксированные C checks дополнительно используют настоящие native door records,
а не только совпадение двух новых вычислений.

Прямой host gate с общим Zig0.13.0:

```powershell
& "$env:LOCALAPPDATA/Programs/Zig/0.13.0/zig.exe" cc -std=c11 -pedantic-errors -Wall -Wextra -Werror -O2 -I ports/nds/include ports/nds/core/projection.c ports/nds/tests/test_projection.c -o build/nds/n06/test_projection.exe
build/nds/n06/test_projection.exe research/evidence/nds/n06/projection-vectors.tsv research/evidence/nds/n06/projection-map-vectors.tsv
```

Фактический запуск дал **19 626 checks PASS**. Это numerical host gate;
ARM9 compile/runtime и визуальная N06-приёмка отражаются отдельно в паспорте.
