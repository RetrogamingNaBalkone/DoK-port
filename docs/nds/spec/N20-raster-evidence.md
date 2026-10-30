# N20: исходный raster карты PKR/PKE

Основа независимого NDS raster — автоматический экспорт Ghidra и ASM
`research/evidence/automap/{ru,en}/`. GBA renderer не читался и не импортируется.
Исторический `ports/gba/scripts/verify_automap_original.py` использован как
указатель на метод исполнения оригинальных инструкций и перехват VGA virtuals.
Новый instrumentation находится в `ports/nds/tools/map/original_map.py`.

## Образцы и адреса

- PKR `originals/pkr/RUS/PKR.EXE`, SHA-256
  `12f78afc0eba05f76b5dc0bb99da38d7daa20585024c0ba958fe828aa3864d28`.
- PKE `originals/pkr/ENG/PKE.EXE`, SHA-256
  `cce7bc5993d0734ad87e93db37a70b8b52faedfff14ac48d6e317e5ac996a37b`.

Адреса здесь — NE-loader Ghidra: PKR `1098:1290…30C0`, PKE `1030:1374`.
Это не файловые смещения и не DPMI-селекторы. Сохранённые exports проверены
по manifest SHA до/после; helper проверяет SHA EXE перед исполнением и после
каждого кадра. Unicorn исполняет relocation исходного code segment по
CS5000, data DS/SS3000; far pointers указывают на контролируемые world/object
буферы. Адреса этой модели исполнения не объявляются адресами DOS-сессии.

ASM подтверждает `__cdecl16far`: far this на BP+6, LES, RETF, очистка аргументов
у вызывающей стороны. Классы/размеры/угол — WORD с signed comparisons;
marker — BYTE `object+1B`; monster state — WORD `object+A2`. NDS metadata
хранит только используемый предикат состояния4; остальные значения не
объявляются полным переносом AI object. Графические координаты аргументов —
DWORD; прямоугольники, горизонтали и вертикали имеют включённые концы.

## Проверенные правила

В native первый проход FCMap выполняется до каких-либо записей visited.
Пустой фон имеет marker определения glyph0x20; FCMap marker этого glyph и
marker glyph0xDB не рисуются. Исследованный пол заливается включённым
прямоугольником `[right-scale,right] × [top,top+scale]`.

Во втором проходе class1 в MMap сначала выставляет visited своей клетки.
При class0 MMap выбор определения продолжается в TMap. NDS raster сохраняет
этот порядок локальным `seen(index,current)`, не изменяя coherent snapshot.
Для wall class2 каждый из четырёх контуров проверяется отдельно: сосед должен
быть visited, его **TMap** class не должен быть2. Соседство оригинала линейное
по индексам карты, что сохраняется и на границах строк; безопасные внешние
индексы NDS считаются неизвестными/class0.

Предмет карты (`world+0C != 0`) разрешает внутренние квадраты классов3/10/6/13
и живого class9 (`+A2 != 4`). Квадраты class8/12 рисуются и без предмета карты.
Ни одна из ветвей не обходит visited. Открытие двери меняет переданный marker
по native инверсии, а не правила raster: формирование snapshot описано в
основной N20 спецификации.

Для class4/7 вертикальная центральная линия разрешена, если оба X-соседа
в TMap имеют класс вне2/4/7. Независимо проверяется горизонталь по двум
Z-соседям. Параллельные проверки нельзя заменять одним выбором ориентации.
Включённые native endpoints: вертикаль `(right-scale/2,top+1..top+scale)`,
горизонталь `(right-scale..right-1,top+scale/2)` с целочисленным делением.

Указатель героя использует native фиксированные пиксельные offsets1/2/3;
он не масштабируется вместе с клеткой. Цветной tip находится в одном из
восьми направлений, центр и противоположный pixel очищаются цветом0.
Угловые диапазоны:0…19/341…360,20…64,65…109,110…154,155…199,
200…244,245…289,290…340. Эти границы следуют signed native веткам,
а не округлению `heading/45`.

## Явные адаптации NDS и граница oracle

Оригинал использует scale3 и origin x64/y7. NDS сохраняет этот draw algorithm,
подставляя согласованные масштабы2/3/4/6/8 после двух x87 scale conversions.
Device clip переводится в выбранное окно256×160. В координатах плана правая
граница клетки x равна `(width-x)*scale`, верх клетки z — `z*scale`.
`original_map(language,snapshot,scale,left,top)` возвращает40960 INDEX8;
C API принимает противоположные смещения `offset_x=-left`, `offset_z=-top`.
Полоса кнопок в output raster не входит; её sentinel проверяется отдельно.

DOS рисует указатель после каждой строки, включая строки до обнаружения героя,
используя неинициализированные stack locals. Instrumentation устанавливает
будущие координаты героя заранее. NDS рисует три итоговых pixels один раз
после плана. Случайные DOS stack traces намеренно не воспроизводятся.
Полная оригинальная VGA/DPMI-сессия этим oracle не доказывается.

## Воспроизведение

```powershell
py -3.14 -B -X utf8 ports/nds/tools/check_map_raster.py
```

Исполнение: Python3.14.6, Unicorn2.1.4, Zig0.13.0 host C DLL.
Постоянный отчёт — `research/evidence/nds/n20/raster-check.json`.
Проверяются обе локализации и все три оригинальных LEVEL, все5 scales,
наличие/отсутствие предмета; дополнительные synthetic worlds проверяют
классы0…13, fallback MMap→TMap, обе оси doors, corpse state4, floor exclusions,
unknown visited, clipping, прямоугольные планы и каждый угол0…360.
Числа итогового прогона и его PASS фиксируются в отчёте; это отдельно от
source disclosure и ARM9/эмуляторного touch-пути.

Итог29 октября2026: **954 сравнения, 39 075 840 pixels,0 различий**.
В том числе60 LEVEL cases,160 synthetic class/visited/clip cases,
722 angle cases,10 явных all-class cases и2 проверки packed snapshot ABI.
`passed=true`; pins C/header/helper/checker в отчёте совпадают с текущими
файлами. Это host/source результат; ARM9 и реальная LCD-проверка относятся
к отдельному joint runtime прогону N20.
