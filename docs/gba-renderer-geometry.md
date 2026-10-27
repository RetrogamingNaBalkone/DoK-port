# Геометрия первого GBA-кадра

G05, 25 октября 2026 года. Реализован ограниченный статический renderer LEVEL1:
`include/dok/renderer.h`, `src/renderer.c`, host-инструмент `tools/render_tool.c`.
Он использует проверенные `dok_runtime`, ROM reader и scalar projection из G04.
Общие состояние/карты и профиль берутся из единого пакета G03; переключение
языка не выбирает другую геометрию. Исходные EXE повторно не анализировались.

## Контракт и память

`dok_renderer_draw(workspace,runtime,assets,level,language,camera,stats)` ничего
не выделяет и не меняет runtime или пакет. Caller предоставляет workspace с
indexed framebuffer240×160, массивом256 плоскостей, небольшим кешем borrowed
image views и строкой LIGHT256 байт. Исходные текстуры остаются в ROM.
Размер workspace на Windows x64 —55 312 байт; ARM sizeof и linker placement
измеряются GBA-пробой корневого агента. Это не дополнительный framebuffer:
платформа выводит непосредственно `workspace.frame`.

Caller-owned `frame_guard_before/after` окружают ровно38 400 байт framebuffer;
renderer не меняет эти поля. Draw записывает сцену в y0…143, очищает y144…159
в индекс0 для последующего сообщения/диагностики. На failure кадр может быть
частичным: его нельзя выдавать за успешно отрисованную сцену. Статусы различают
argument/profile/capacity/resource/projection ошибки. Переполнение массива
плоскостей возвращает capacity без записи за пределами. Ёмкость можно уменьшить
при контрольной сборке через `-DDOK_RENDER_FACE_CAPACITY=1`.

Profile принимает native lens D200,k8,K4096,cx159,cy79 и heading90
(sine32768,cosine0). Начальная камера получается из actor героя с обязательной
известностью XYZ. Служебный API допускает другие bounded XYZ для контролируемых
проб, но это не реализованное перемещение/поворот G07.

## Геометрия

Это генерация плоскостей и painter rendering, а не общий grid raycaster.
Исходные формулы/адреса — [rendering-geometry.md](rendering-geometry.md).
Используется viewport-cell AABB для heading90:
X от trunc((cameraX−7*CellSide)/CellSide) до trunc((cameraX+2*CellSide)/CellSide),
Z от trunc((cameraZ−6*CellSide)/CellSide) до trunc((cameraZ+6*CellSide)/CellSide),
с clamp в границы карты. В стартовой сцене это130 клеток.

- TMap class2 даёт до четырёх cell-edge вертикальных плоскостей. Сторона
  пропускается, если сосед в соответствующем направлении тоже class2;
  выходящие за карту соседи ограничиваются краевой клеткой, как в оригинале.
- TMap class4/7 даёт center plane orientation0 при открытых северном и южном
  соседях и orientation90 при открытых западном и восточном. Здесь закрытыми
  считаются class2/4/7. Обе допустимые оси проверяются отдельно.
- Размеры берутся из реального Sprite image: signed16 low-word от imageWidth
  и imageHeight, умноженных на width_parameter. Стена96×192 scale5 —480×960;
  дверь96×128 scale5 —480×640. Это не одинаковые кубы CellSide×CellHeight.
- Floor origin Y — sign-extended NEG16 height_parameter текущего FCMap.
  Для wall edge высота равна low16(scaledImageHeight−neighborFCMap[+0x54]+1),
  а начало V — low16(imageHeight*neighborFCMap[+0x54]/CellHeight).
- Начальные class7 numeric параметры сохраняются. В частности, +0x5C у c
  равно4, у d равно0; эти числа не были подменены выдуманным «прогрессом
  открытия». G05 проверяет их соответствие неизменяемому начальному descriptor.

Scalar transform/projection вызывается через `src/projection.c` с исходными
low32 IMUL, ADD/SUB wrap, SAR15 и signed IDIV. Сегмент, у которого оба depth<=0,
отбрасывается. Один отрицательный endpoint обрезается до depth0 с обновлением U.
Denominator остаётся depth+D. Проекции вне bounded setup range ±8192 возвращают
profile failure вместо неопределённого переполнения последующих C-вычислений.

Координаты GBA получаются **после** полного DOS projection: floor(x*3/4) и
floor(y*9/10), включая отрицательные значения. Тем самым сохраняются исходные
cx159/cy79 и асимметрия включительных границ. Это даёт поле240×144 с учётом
VGA pixel aspect; нижняя полоса16 строк соответствует [gba-ui.md](gba-ui.md).

Depth key — среднее endpoint depths; у center plane дополнительно вычитается
scaledWidth>>2, затем key ограничивается нулём. Stable insertion sort размещает
плоскости по убыванию key, и они рисуются с дальних к ближним. Полная исходная
таблица column occlusion G04 здесь не воспроизведена; текстурные нули сохраняют
уже нарисованный дальний кадр только в прозрачном режиме.

## Текстуры и явно упрощённая растеризация

Pixel layout — подтверждённый row-major `v*width+u`. Статические начальные
Sprite shift modes имеют crop index0 и используют исходные bytes. Shared image
ресурсы не модифицируются: исходные общие побочные эффекты OR flags/crop
пока не исполняются. Animated geometry возвращает profile failure.

Для каждого экранного X линейно интерполируются верхняя/нижняя границы,
вычисляется U, затем по Y идёт16.16 V accumulator без деления на пиксель.
Для различающихся глубин U использует bounded reciprocal weights Q7
(`max(1,(6553600/(depth+200))>>8)`). Это **выбранное упрощение G05**, а не
доказанный эквивалент всех исходных UV interpolation ветвей. При одинаковых
endpoint depths, включая проверяемые двери d, оно сводится к affine U без
потери reciprocal precision. Точное воспроизведение UV seams/сужений,
flag0x20 и полноценной billboard растеризации относится к G11.

Каждый texel проходит через LIGHT row0. Строка256 байт один раз за кадр
копируется в EWRAM workspace. Если Sprite flags содержат mask0x08, **после
LIGHT lookup** mapped index0 пропускается; raw texel0 сам по себе не определяет
прозрачность. Полная дистанционная выборка LIGHT rows остаётся G10.

Пол и потолок пока заполняются диагностическими плоскими индексами Floor Color
и Ceiling Color по сторонам горизонта y72. Это не текстурированный пол/потолок
из G04. Actor/item billboards, двери в движении, анимации и полные FCMap step
faces не рисуются; статистика отдельно считает пропущенные TMap billboards.

## Host-выход и проверяемая сцена

```powershell
build/native/dok-render.exe build/gba/assets/assets.bin 1 ru research/evidence/render/level1-ru.idx research/evidence/render/level1-ru.pal
build/native/dok-render.exe build/gba/assets/assets.bin 1 en research/evidence/render/level1-en.idx research/evidence/render/level1-en.pal
```

IDX — ровно38 400 индексов без заголовка, PAL — ровно768 исходных VGA RGB
компонентов0…63. JSON в stdout содержит camera, stats и все итоговые face
records для независимой проверки; PNG создаётся средствами verifier.
Host tool проверяет оба frame guards, побайтную неизменность runtime,
отказ для неподдержанного heading и null workspace.

Первый фактический результат LEVEL1: camera(15120,−300,22800), heading90;
130 рассмотренных клеток,23 wall cells,4 center cells,28 построенных/нарисованных
плоскостей,3 near clips,28 699 записанных пикселей,76 пропущенных прозрачных
пикселей. Две двери d совпали с G04 bounds после масштабирования:
[88…108] и [108…129] по X,53…85 по Y, depth3360, key3240.
Это числовое совпадение геометрии с исходными render records, не только
похожесть картинки. Для текущего пакета сцены RU/EN совпадают побайтно.

Фактические ARM размеры, screenshot, RAM/VRAM equality и стоимость кадра
относительно30 FPS публикует platform verifier. Пока нет его результата,
host-компиляция и узнаваемое изображение не означают достигнутую GBA частоту.

## Фактическая проверка ARM

Платформенная проверка в mGBA завершена: [render-benchmark.json](../analysis/render-benchmark.json).
ARM workspace занимает **54 284 байта**, вся статическая EWRAM этой диагностики —
89 776 байт. Scene/framebuffer и palette на ARM совпали с независимой host-проверкой;
оба frame guards сохранились, RU→EN→RU сохранило mutable world и геометрию.
Просмотрен кадр `build/gba/renderer-host-check/ru/with-ui.png`: видны реальные
текстурированные стены и ворота LEVEL1, зелёный пол остаётся явно диагностическим.

Стоимость измеренного диагностического pipeline — около **10,18 млн CPU cycles**
на кадр при бюджете **561 792 cycles** для обновления через два VBlank.
Таким образом, текущая G05-проба **не достигает30 FPS**. Это первый статический
renderer с проверенным изображением и измеренной ценой, не завершённая
оптимизация игрового движка. Физическая GBA пока не проверена.
