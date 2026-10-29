# N11: представление динамических дверей

Архитектура принята до реализации. Модули:
[door_render.h](../../../ports/nds/include/dok/door_render.h),
[door_render.c](../../../ports/nds/render/door_render.c).
Это независимый NDS C11-код без SDK, allocation и GPU-вызовов. GBA-исследования
использованы как указатели на адреса оригинала; GBA-код не является основой.
Принятые N07 geometry, scene, packet, material loader и backend не изменяются.

## Источники и разделение доказательств

Свежий read-only экспорт N11 — `research/evidence/nds/n11/original/{ru,en}/`.
Адреса ниже — Ghidra NE-loader, не файловые offsets, NE-сегменты или runtime
DPMI-селекторы. Образцы PKR.EXE/PKE.EXE и их SHA закреплены в N11 evidence;
проверка до/после экспорта относится к oracle, а не к renderer.

| Назначение | PKR RU | PKE EN |
|---|---|---|
| Настроить crop image header | `10F8:0592` | `1060:06A2` |
| Преобразовать пиксели crop | `10F8:05C9` | `1060:06D9` |
| Constructor центральной плоскости, mirror argument | `10B0:03EE` | `1040:03FA` |
| Четыре вызова constructor из world builder | `1098:49E7/4A77/4E5D/4EED` | `1030:5405/5508/5938/5A3B` |

`LES`, far `CALL/RETF`, caller cleanup и порядок PUSH проверяются по свежему
ASM. Mirror — отдельный аргумент BP+1C; он не выводится из image flag20.
Неизменный N07 builder уже сохраняет constructor mirror обратным U.
Новая стадия использует эти U и не вводит вторую эвристику соседней стены.

Native crop `0x48` сдвигает пиксели по +X, `0x108` — по +Y, заполняя
освободившуюся область нулями. Dimensions не меняются. Сначала выполняется
signed division `dimension / extent` с усечением к нулю, затем умножение
на phase; исходный IMUL сохраняет WORD результата. Для допустимых размеров
N04 и неhidden промежуточной фазы `0<=phase<extent-1` переполнения произведения
нет. Для промежуточных фаз
неподдерживаемые wrap/reverse crop modes отвергаются, не принимаются за обычную
дверь. Нулевая фаза сохраняет исходный quad; terminal correction ниже общая.

Независимый oracle исполняет исходные инструкции crop и constructor.
Отдельная математическая проверка сравнивает XYZ/UV нового crop с native
смещением пикселей и уже принятой исходной плоскостью N07. Это не выполнение
всего DOS rasterizer. Применение float interpolation и GPU edge coverage
не объявляется побайтовым DOS-кадром.

## Одна фаза для изображения и collision

Interaction core владеет phase/extent; mutable flags и occupancy принадлежат
N10 collision. Renderer получает скопированное значение
`dok_interaction_phase_view`: world_epoch, revision, signed phase/extent[256]
по glyph. Definitions и bindings остаются immutable. Все клетки одного glyph
читают одну фазу, но сохраняют собственный constructor mirror и source ID.

Shell получает view после завершения simulation callback. До synchronous
submit и release scene lease запрещены изменения interactions, collision и
карт. Именно caller обеспечивает этот borrow и проверяет неизменность revision;
renderer не зависит от collision state или event queue. Проверка epoch
отвергает чужой мир до приобретения lease. Presentation elapsed не продвигает
phase и не влияет на проходимость.

Проход через дверь разрешается native flags по правилам interaction core.
Панель может быть ещё видима после разрешения прохода: разрешение не выводится
из размера изображения. Проверка отсутствия панели не доказывает collision.

## Pure crop плоскости

`dok_door_geometry_build` получает исходный DOOR quad N07, image binding,
phase и extent. Нулевая фаза возвращает тот же quad byte for byte, включая
метаданные. Начальное `extent=1, phase=0` остаётся закрытой дверью.

При сдвиге X сохраняется часть affine quad, где исходный `U>=shift`;
новое `U=oldU-shift`. Для Y аналогично `V>=shift`, `V=oldV-shift`.
Остальная часть соответствовала бы прозрачным нулям преобразованного image.
Исходный texture bank не изменяется, новых uploads на активном кадре нет.
Обратные U отражённой створки автоматически меняют мировую сторону сдвига.
Нельзя передвинуть всё полотно на shift: это нарушило бы native zero-fill crop.

World XYZ целочисленны; delta до пересечения округляется к ближайшему world
unit, половина — от нуля, до прибавления исходного endpoint. Ошибка положения
не более половины world unit. UV остаются Q16; texture padding не растягивается.
Такая интерпретация clipped polygon наследует GE nearest/edge адаптацию N07.
Она не заменяет исходные dimensions или порядок целочисленного вычисления shift.

**Явная коррекция NDS:** при `extent>1 && phase>=extent-1` DOOR quad отсутствует.
Нативный crop может оставить полосу: для width96, extent30, phase29 сдвиг87,
остаток9 texels. Требование N11 устраняет эту полосу. Phase, callbacks,
occupancy и момент изменения flags сохраняются; floor/ceiling/walls не исчезают.
Terminal hidden проверяется отдельно от сравнения native crop samples.

`phase>=extent` — допустимый runtime state. Use готовой открытой двери заново
считывает C2 в extent, сохраняя прежнюю phase: например, открытая phase29/extent30
после Use с C2=8 становится phase29/extent8. Исходный image getter прижимает
только image frame к extent; door.phase не прижимается. Поэтому renderer не
отвергает такой кадр: он применяет terminal hidden, а native CLOSE позже
уменьшает сохранённую phase. Отказ остаётся для отрицательной phase и extent<1.
Начальное extent1/phase0 по-прежнему показывает закрытое полотно.

## Additive scene adapter

`dok_door_scene_prepare` вызывает неизменный N07 prepare с локальным output
pointer. До публикации наружу он postprocess-ит caller-owned staging:

1. Source surface ID раскладывается по принятому N07 контракту
   `((z*64+x)*DOK_GEOMETRY_CELL_MAX)+ordinal+1`. Неизменный geometry builder
   восстанавливает исходный cell в двух bounded stack scratch-структурах.
2. Только DOOR класса7 применяет phase. Class4 и остальные поверхности
   остаются прежними. Сужение crop консервативно относительно исходного full
   quad frustum: оно не добавляет поверхность за пределами исходной.
3. Stable compaction в тех же scene.quads/cells исключает hidden панели и
   пустые spans, сохраняя source IDs, material slots, origins и порядок.
   Второй runtime mesh не выделяется. Counts N07 source/frustum/merges
   описывают исходный prepare; published_quads и отдельные doors/cropped/hidden
   описывают новый packet.
4. При ошибке acquired base lease освобождается; output pointer и counts
   caller остаются прежними. Следующий prepare разрешён. На успехе наружу
   публикуется только полностью обработанный packet, release остаётся N07.

NULL view использует прежний путь без postprocess. Phase0 сохраняет packet
N07 побайтно по quads и точно по cells/counts; generation увеличивается обычным
N07 prepare. Ни симуляция, ни clock, ни геометрия мира не изменяются.

N07 умеет X-run merge с provenance. Для исходных class7 images width96>1
reset UV исключает объединение смежных горизонтальных дверей: equality
общего ребра требовала бы `width-1==0`. Orientation90 не образует такое
X-продолжение. Проверены все153 исходных class7 quad трёх уровней, с widths96
и минимальными heights128/114/110. Это не доказательство для произвольного mod.

Synthetic image width1 может объединяться. Adapter проверяет retained и
consumed source IDs каждого merge: если crop действительно меняет дверь,
он отвергает кадр целиком до публикации. No-op shift0 разрешён. Adapter не
обрезает несколько полотен как одно и не пропускает проблемную дверь молча.
Сам synthetic merged path и освобождение lease проверяются host suite.

## Проверки и диагностические fixtures

Общие host/ARM9 cases:
[door_render_cases.c](../../../ports/nds/tests/door_render_cases.c), workspace284
bytes. Они проверяют136 комбинаций phase×mirror×crop axis, phase0/terminal,
углы/UV, alias out, signed extreme endpoints и отказы. Host-only
[test_door_render.c](../../../ports/nds/tests/test_door_render.c) проверяет
base packet, compaction, source IDs, untouched surfaces, epoch, released
failure и synthetic merged-door guard. Его большие comparison globals
не включаются в ARM9.

Public-API regression дополнительно открывает дверь через interactions и N08
до phase29/extent30, выполняет Use с C2=8, передаёт настоящий phase view в
scene adapter и проверяет hidden compaction. После CLOSE phase28/extent8
остаётся hidden; при phase6 снова появляется cropped панель. Это проверка
связки состояния и представления, без записи внутренних полей interactions.

[door_render_dump.c](../../../ports/nds/tests/door_render_dump.c) читает точный
N04 package и actual RU world N05. `--geometry` выдаёт4590 vectors всех153
class7 panels×30 phases; source quad содержится в phase0. `--scan 1` сравнивает
phase0 с неизменным N07 для всех65192 non-wall cell-center cameras на восьми
headings:24424/21376/19392 по уровням. Comparison backup существует только
в host-драйвере. `--scene x y z heading phase extent` выдаёт dynamic packet.
Окончательные команды, хеши и runtime результаты фиксирует N11 gate.

SDK/emulator shell использует явно обозначенные fixtures: перемещение героя
к двери и выдача keys не объявляются естественным прохождением уровня.
Параметры actual LEVEL1 сохраняются: a/d/g mode1, delay40000, initial interval0;
key mask a=2, d/g=0. Проверенные по исходным картам свободные facing poses:

| Случай | Glyph, размещение RU runtime | Camera/hero XYZ, heading |
|---|---|---|
| Locked a, before/after key |97, (56,24)/(57,24) |(27360,−300,11280),0 |
| Разъезд отражённых полотен d |100, (24,46)/(24,47) |(11280,−300,22560),270 |
| Вертикальная дверь g |103, (2…5,24) |(1920,−300,11280),0 |

Здесь heading следует N06:0 смотрит +Z,270 — +X. Corner footprint проверяется
отдельно по MMap/TMap; opposite side a пересекает исходного монстра и не выбран.
Для ручного CLOSE подходят actual mode0 двери L2 h и L3 k после sensor Use;
у L1 mode1 необходимо сохранить AUTO и обе исходные задержки, а не менять
definitions ради короткого теста. Screenshots closed/intermediate/terminal
и oblique view отделяются от collision traces и выполняются на целевом
256×192 экране со сценой154/HUD38. Физическая DS/DS Lite остаётся N24.
