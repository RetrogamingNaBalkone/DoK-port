# N21P C8: точное ускорение visual culling

Изменён только NDS `render/scene.c`: private helper `visible` и вычисление
двух camera dot products один раз в `dok_render_scene_prepare`.
Принятый C7 source сохранён в `research/evidence/nds/n21p/pre-scene/` с pins.
`render/geometry.c` сохраняет проверенное C7 удаление повторного clear;
compact disclosure producer, original-space constructors и карта не меняются.

## Предикаты и целочисленные диапазоны

Исходная функция пересекает пять outside-plane bits четырёх вершин.
Пересечение может только терять bits. Если предыдущие вершины уже сбросили
bit плоскости, никакая следующая вершина не вернёт его. Поэтому её предикат
больше не влияет на итог. Depth нужен всегда; lateral вычисляется только
при `common_outside&6`, vertical displacement — только при `&24`.
Каждый оставшийся предикат использует исходные коэффициенты и signed64.
Отсутствие общей отвергающей плоскости по-прежнему сохраняет quad для GPU.

Вместо повторного произведения33-bit разности координат на sin/cos helper
вычисляет32-bit vertex dot и вычитает camera dot, общий всем quads кадра:

```text
depth = vz*cos − vx*sin − (cameraZ*cos − cameraX*sin)
lateral = vx*cos + vz*sin − (cameraX*cos + cameraZ*sin)
```

Это точная дистрибутивность signed64, без wrap, shift, division или narrowing.
При full signed32 координатах разность координат имеет модуль не больше
2³²−1. При |sin|,|cos|≤32768 depth и lateral имеют модуль строго меньше2⁴⁸.
Каждый отдельный vertex/camera dot помещается в2⁴⁷ по модулю.
Eye добавляет только200×32768. Наибольшие weighted plane sums с исходными
400/324/326/164/166 имеют модуль меньше2⁵⁸, существенно внутри signed64.
Y displacement и все пять строгих сравнений `<0` сохраняют исходную точность.

## Проверки

```powershell
py -3.14 -B -X utf8 ports/nds/tools/map/check_scene_visibility.py
```

Host исполняет actual новые private helpers и отдельно скомпилированный
exact старый `scene.c` с host wrapper для private `visible`.
604425 quad-векторов совпали:500000 full signed32 random,102400 signed32
extremes и2025 границ пяти плоскостей с координатами−1/0/+1 около точного
контакта. Камеры и sin/cos покрывают крайние значения; коэффициенты также
могут не образовывать единичный поворот, что расширяет проверяемый диапазон.

Полные scene prepare совпали на13816 кадрах:936 исходных N07 fixtures
и12880 фактически наблюдённых runtime camera frames на трёх уровнях.
Сравниваются все counts, quads/vertices, cell origins, merge provenance,
generation/lease и весь scene после нормализации адресов. Каждый cell pointer
дополнительно сверяется по точному offset внутри quad array.
Сравнено3619570944 байта, error returns0.

Для fullscene gate используются исходные начальные LEVEL1–3 world/bindings
и реальные runtime позы. Это не воспроизведение изменяемого live world state;
численная проверка private culling отдельно покрывает произвольные quads.
Ни один quad, вершина или эффект ради ускорения не удалён.

## Pins и границы приёмки

`scene-visibility-equivalence.json` содержит inputs, команды, exact source,
host binaries и итоги. `scene-source-supersession.json` связывает только
старый `scene.c` pin в discovery reports с архивом и новой реализацией.
Первые5494 raw bytes, включая trig/gather bounds и merge, побайтно неизменны;
release также неизменён. В prepare изменены только dot locals и аргумент
private helper. Таким образом потребляемые disclosure trig/bounds сохраняют
exact source; отдельный native constructor replay не повторяется.

Это доказательство эквивалентности, не прохождения совместного FPS-бюджета.
ARM9/melonDS before/after, оба экрана, звук и actual gameplay проверяет основной
этап N21P. Физическая DS/DS Lite остаётся N24.
