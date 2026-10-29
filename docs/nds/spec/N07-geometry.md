# N07: исходные поверхности и контракт builder

Рабочая спецификация N07, 29 октября 2026 года. До приёмки N07 это описание
исследования и проектируемого интерфейса, а не готового renderer.

## Источники и адресация

Основа — сохранённые Ghidra-экспорты
`research/evidence/surfaces/{ru,en}/{evidence.asm,automatic.c,geometry.bin}`
и `research/evidence/rendering/{ru,en}/`. Автоматический C используется как
указатель; решения проверяются по ASM. GBA-код не является основой модуля.
Адреса в таблицах — Ghidra NE-loader, не файловые смещения, NE-индексы
и не runtime DPMI-селекторы. Нового анализа EXE пока не выполнялось.

PKR.EXE SHA-256:
`12f78afc0eba05f76b5dc0bb99da38d7daa20585024c0ba958fe828aa3864d28`;
PKE.EXE:
`cce7bc5993d0734ad87e93db37a70b8b52faedfff14ac48d6e317e5ac996a37b`.
PKR.RES:
`af7135fbcfc765c1e963682a29b108ffe90d652d96fa130cf9db6b217277b816`;
PKE.RES:
`865da340621fa6f90548a84e9ae9bf0a85071befb32a40ca1d359e488f74d458`.
Все четыре исходника совпали перед исследованием; заключительный SHA-аудит
будет сохранён вместе с результатом.

## Проверенные правила

Обход статического мира — PKR `1098:30C1`. Карты уже находятся в runtime
orientation N05: source `(x,y)` становится `(x,63−y)`, `.` заменён пробелом.
NDS использует RU-мир для обеих локализаций. Модель N05 не заменяется и не
сужается до class2-кубов.

- Образ текущего TMap берётся virtual slot+8; базовые размеры — из object+44,
  масштаб — signed16 object+17. `3583…35AC` умножает размеры в WORD с сохранением
  младших 16 бит. Начальная статическая привязка не объявляет готовой runtime
  анимацию или открытие двери.
- FCMap `H=object+19` задаёт `floorY=sign16(NEG16(H))`. При `I=object+54>=0`
  extent=`sign16(CellHeight+1−I−H)`, ceilingY=`floorY−extent`.
  `35CE…3626`, `36A0…36B4`, `444B…4480`. Для `I<0` остаётся один
  текстурированный пол (`44F5…4509`), а не цветная заливка.
- Class2 TMap исключает горизонтальные плоскости своей клетки. Его грань
  создаётся только при соседнем TMap class!=2, с clamped map coordinates.
  Height=`sign16(imageHeight*scale−neighborI+1)`,
  V-start=`sign16(trunc(imageHeight*neighborI/CellHeight))`,
  V-end=`imageHeight−1`; PKR `3C00…439E`, пример `3D28…3D7E`.
- До исключения class2 оригинал обрабатывает полосы потолка. Если текущий
  I>=0 и соседний I<текущего, создаётся грань с originY=`sign16(I−CellHeight)`,
  height=I, width=CellSide. Она использует ceiling-side getter vtable+20
  (object+5E), V-start=0 и V-end=`trunc(imageHeight*I/CellHeight)`.
  Это высота текущего I, а не разность I. PKR `36B8…3BAE`.
- Class4/7 имеет cell-centred плоскость orientation0, если N/S соседи не
  class2/4/7; constructor mirror равен `(west class2)` (`465F…4B05`).
  Orientation90 создаётся независимо, если W/E не class2/4/7, mirror равен
  `(north class2)` (`4B05…4F7B`). Полная таблица решений должна пройти x86 oracle.
- Horizontal images: floor getter vtable+1C (object+5A), ceiling getter +18
  (object+56), ceiling-side +20 (object+5E). Class12 floor getter возвращает
  текущий Sprite вместо обычного floor resource; **class13 использует обычный
  field+5A**, как подтверждает vtable DS069D+1C → `1068:42C0` в Ghidra data.
  Прежнее обобщение GBA-заметок «class12/13» не принимается за факт NDS.
  Floor-side getter +24
  существует, однако в рассматриваемом статическом обходе не вызывается;
  добавлять воображаемые floor-step faces нельзя.
- Исходные UV идут по texel centres 0…width−1 / 0…height−1. Padding N04 GPU
  texture не меняет полезный прямоугольник и не должен растягиваться на всю грань.
  Вертикальный constructor mirror переставляет концы; это отдельный аргумент,
  не универсальное значение image flag20.

В предоставленных FCMap все H=0. I=400 встречается на открытых клетках LEVEL1/2,
LEVEL3 имеет обычную высоту. Наличие glyph `=` с I=0 под class2 не доказывает
открытый низкий коридор. Полные значения и source spans — N06 height fixtures.

## Принятый интерфейс builder

Builder [geometry.c](../../../ports/nds/render/geometry.c) без SDK, allocation
и floating point получает N05 InitialWorld,
таблицу material bindings и координату клетки. Он выдаёт bounded список
исходных quad: XYZ32, UV texels Q16, material ID, image/object flags, kind,
orientation и source cell. Таблица bindings хранит исходные размеры текущего
initial image и отдельные floor/ceiling/side привязки. Не хранить весь мировой
mesh в RAM; caller проходит клетки или доказанно видимые runs, затем делает
camera-relative transform, clipping и GPU emission.

Канонический oracle сохраняет несоединённые исходные поверхности. Слияние
допустимо лишь при одинаковых plane/material/flags/height, совпадающей UV-фазе
и сохранении исходного повторения на границе каждой клетки. Одинаковая картинка
при другом crop/анимационном состоянии не является достаточным условием.
Полосы с различной I/V-start, двери и прозрачные поверхности не объединяются
по одной близости. Контроль GPU budget не должен произвольно удалять грани.

## Initial image и флаги кеша

Это независимое уточнение по оригиналу; описания GBA не являются источником
готовой семантики. Обычный constructor `1068:02F5` помещает descriptor Sprite
в native object+44. Animation first frame выбирается только при object flags
`0x0800`; этого бита нет у исходных static classes0/2/4/7/12/13. Scale берётся
из `width_parameter`, native signed WORD+17. `height_parameter` — другое
поле, WORD+19. Getter class2/4 vtable+8 — `1068:04AE`, возвращает +44.

Class7 constructor `1068:30FF` копирует 18-байтовый image header в private
object+6C и перенаправляет +44 туда (`313E…3165`). Общий constructor задаёт
frame+50=0, class7 задаёт divisor+60=1 (`3185`). Getter `1068:320B` вызывает
`image_flags_crop_configure(image,0,1,0)`. Поэтому начальный crop нулевой,
размеры named Sprite сохраняются. Например, LEVEL3 `ДВЕР1` имеет **96×111**,
`ДВЕРЬ_Л` — **96×110** непосредственно в RES; LEVEL2 `ДВЕРЬ_Л` — **96×114**.
Это не результат выбранной NDS модели или runtime изменения высоты.

LEVEL parser `1048:0789` получает cached image, а `07AA…07B2` **присваивает**
image.flags=Sprite directive flags. Здесь нет OR со stored flags. Каждое
следующее определение с тем же именем заменяет flags общего image. Прежнее
описание OR относится к позднему `10F8:0592`, а не к этому присваиванию.
Cached getter `1060:0000` сначала ищет имя и только при отсутствии читает image.
Все исходные stored flags=0; untouched FC image сохраняет их, а shared Sprite
image получает последний assignment в порядке определений. Private class7
image сохраняет assignment на момент своего создания. Getter FC +56/+5A/+5E
сам flags не меняет. Class12 использует текущий Sprite для пола, class13 — +5A.
В отчёте oracle сохранены все initial bindings и flags по glyph/slot.

Image.flags&8 выбирает прозрачность только вертикального raster. Horizontal
raster остаётся opaque, включая индекс0. Само поле в quad не разрешает backend
включать color0 transparency у пола/потолка. LIGHT и phase/crop runtime — N13.

## AABB исходного обхода

Исходный блок PKR `1098:30D1…33AB` не является far clipping plane.
Пусть s=CellSide, S/C=sin/cos[heading], R/T=cos/sin[360−heading].
Таблицы берутся точно, без допущения float-симметрии. Все произведения,
суммы и разности выполняются low32 до SAR15; итоговая координата делится
signed IDIV s с усечением к нулю и сужается до signed WORD.

```text
Bx=Px+SAR15(2*s*S); Bz=Pz−SAR15(2*s*C)
p0=(Bx+SAR15( s*R),       Bz+SAR15(−s*T))
p1=(Bx+SAR15(−s*R),       Bz+SAR15( s*T))
p2=(Bx+SAR15(6*s*R+9*s*T),Bz+SAR15(9*s*R−6*s*T))
p3=(Bx+SAR15(−6*s*R+9*s*T),Bz+SAR15(9*s*R+6*s*T))
```

После IDIV native min берётся по четырём точкам. При вычислении max исходный
slot p1 уже перезаписан min: фактически max берётся по p0/p2/p3. Это обнаружено
исполнением исходных инструкций, а не предположением по форме AABB. При
camera `(15120,−300,22800)`, heading112 точки X равны 32/33/22/27, native X
bounds22…32, обычный conservative AABB22…33. Min clamp≥0, max clamp≤63
выполняются после min/max. N07 backend может принять строгий conservative
superset min/max всех четырёх точек, явно отделив его от native bounds;
произвольное уменьшение дальности этим фактом не разрешается.

`native-geometry-aabb.json` сохраняет 1444 x86 fixtures: все 361 headings для
четырёх поз, включая границы карты, с native и conservative bounds отдельно.

## Независимая проверка

Выполнено isolated x86 исполнение сохранённого исходного RU builder на каждой
клетке трёх RU-карт. Far getters и allocation/constructors заменяются bounded
стендами, которые сохраняют входные параметры. Решения о наличии, размерах,
orientation и mirror принимает оригинальный ASM, а не Python-копия нового C.
Нативные far pointers заменяются контролируемыми real-mode адресами стенда;
это не DOS-сессия и не DPMI selector mapping.

Использован Ghidra segment export `research/evidence/flow/ru/1098.bin`, SHA
`44937d68d6e03f426afd77c6e0427af1f7cfcc42e6946d23d0e00618701f91f9`.
Его bytes совпадают с отдельными сохранёнными combat/automap/AI экспортами.
Новые EXE export/disassembly не выполнялись. Canonical N05 models закреплены
по accepted SHA; исходные RES читаются только для resource names/dimensions.

Исходные инструкции выбирают наличие и параметры каждого constructor.
Разворачивание этих параметров в world XYZ/UV выполнено отдельной декларативной
моделью по проверенным ASM endpoint/UV правилам; это не исполнение DOS raster.

| RU level | Wall | Initial door | Floor | Ceiling | Ceiling band | Всего quad | Max/cell |
|---|---:|---:|---:|---:|---:|---:|---:|
| 1 | 1282 | 104 | 3053 | 3053 | 716 | 8208 | 6 |
| 2 | 2026 | 291 | 2672 | 2672 | 1216 | 8877 | 5 |
| 3 | 1116 | 74 | 2424 | 2424 | 248 | 6286 | 3 |

Все 12288 клеток, 23371 quad и 677759 scalar values C dump совпали с oracle.
Дополнительно исходный x86 builder прошёл 512 synthetic combinations соседей
class4/7 и negative-I single-floor/NEG16 case. C test выполнил 2831 checks:
все 512 комбинаций, mirror UV, image/object flags, wall V-start, WORD overflow,
single floor, missing binding и неизменность out при отказе. Linux cross-link
прошёл, Linux executable не запускался. ARM9/DS проверки выполняет общий N07 gate.

Bounded cell ABI: image8B, binding32B (8192B/256glyph), vertex20B, quad92B,
cell1108B. Верхняя доказанная ветвями ёмкость8quad, caller capacity12; ни одна
ветвь не удаляется при лимите. Candidate может меняться, published out — только
при полном успехе. Unknown FCMap class/field отвергается, не превращается в ноль.

Команды из корня, CPython3.14.6 и общий Zig0.13.0:

```powershell
$zig="$env:LOCALAPPDATA/Programs/Zig/0.13.0/zig.exe"
& $zig cc -std=c11 -Wall -Wextra -Werror -pedantic -O2 -I ports/nds/include ports/nds/tests/geometry_dump.c ports/nds/render/geometry.c ports/nds/core/world.c ports/nds/core/sha256.c -o build/nds/n07/geometry_dump.exe
& $zig cc -std=c11 -Wall -Wextra -Werror -pedantic -O2 -I ports/nds/include ports/nds/tests/test_geometry.c ports/nds/render/geometry.c ports/nds/core/world.c ports/nds/core/sha256.c -o build/nds/n07/test_geometry.exe
build/nds/n07/test_geometry.exe
C:/Source/wasdify/.venv/Scripts/python.exe ports/nds/tools/audit_geometry.py --dump build/nds/n07/geometry_dump.exe
& $zig cc -target x86_64-linux-musl -std=c11 -Wall -Wextra -Werror -pedantic -O2 -I ports/nds/include ports/nds/tests/test_geometry.c ports/nds/render/geometry.c ports/nds/core/world.c ports/nds/core/sha256.c -o build/nds/n07/test_geometry-linux
```

Shared Unicorn читается из `C:/Source/wasdify/build/research-deps`, отдельной
копии в проекте нет. Отчёт `geometry-native-report.json` содержит все SHA,
counts, bindings, source pins и original before/after hashes; все четыре
исходника неизменны. Script выводит только краткую сводку, полные payloads
остаются в ignored research/build. Guards отвергают выход в originals,
parent traversal, hardlink, symlink и symlink ancestor; sentinel не изменён
(`geometry-output-guards.json`). Перед запуском проверяются все выходы,
до работы публикуется passed=false, чтобы старый PASS не скрывал сбой.

Host oracle не подтверждает rasterization, LIGHT, water phases, игровое
открытие дверей, DS presentation или FPS. Старые N03–N06 sources/artifacts
этим модулем не менялись. Приёмка всей N07 остаётся за общей проверкой renderer.
