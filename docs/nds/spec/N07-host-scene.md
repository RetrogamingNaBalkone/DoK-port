# N07: независимая проверка материалов и scene packets на host

29 октября 2026 года. Это проверка новых C11 material/scene модулей до GPU;
DS display, тайминги и raster coverage проверяются отдельно. Основой служат
принятые [N04 assets](N04-assets.md), [N05 world](N05-world.md) и
[N07 original-instruction geometry oracle](N07-geometry.md). GBA не импортируется.

Host CLI `ports/nds/tests/render_dump.c` читает **точный пакет N04** с SHA-256
`2f03ba02c9986b4e0bd321e94ca00df8a355f7dbb1dff5598a950d3366061511` через
неизменённый `platform/assets/file_reader.c` (режим rb). Затем вызывает
`dok_render_world_load`, `dok_render_scene_prepare/release` и экспортирует
actual C material bindings, scene counts, cells, source IDs, local XYZ/UV.
Вывод JSON идёт в stdout; `--bounds` читает X/Z/heading и выдаёт bounds TSV.
Игровые assets или полный мировой mesh в исполняемый файл не встраиваются.

`ports/nds/tools/check_render_host.py` независимо читает N04 directory/payload
hashes и original-instruction report. Проверяется **полный активный набор**
bindings каждого уровня: ни пропущенных, ни добавленных glyph/slot; точные
image names/IDs, размеры, shared/private flags, прозрачность только vertical
surfaces. SHA каждого полностью загруженного GPU payload совпадает с N04.

| RU level | Активных bindings | Materials | Полный POT texture set | Asset pool | Poses в scan |
|---|---:|---:|---:|---:|---:|
| 1 | 35 | 12 | 278528 B | 344064 B | 24424 |
| 2 | 47 | 16 | 319488 B | 385024 B | 21376 |
| 3 | 34 | 13 | 294912 B | 360448 B | 19392 |

Принятый texture VRAM A+B+D — **393216 B**, поэтому все полные texture sets
уровней помещаются. Эти размеры включают POT padding и отдельные материалы
с различным color0 mode. В asset pool также включён pinned LEVEL slot 65536 B.
Host sizeof: world266920 B, typed workspace105056 B, scene202848 B.
Это host ABI, не подмена фактического ARM9 RAM-профиля.

Всего просканированы **65192 позы**: все non-class2 TMap cell centers трёх карт,
Y=−300, heading0/45/90/135/180/225/270/315. Это полный названный дискретный набор,
а не доказательство непрерывного worst case или реальных DS FPS. Каждая поза
прошла actual scene prepare/release без лимита или произвольного удаления.
После добавления production merge ветви scan проверен повторно: 5720309
проверок кандидатов, **0 объединений** на исходных UV resets трёх уровней.
Сохранение отдельных клеточных texture repeats не заменялось растягиванием.

| RU level | Наиболее тяжёлая измеренная камера X,Y,Z,heading | Retained quads |
|---|---|---:|
| 1 | 23760,−300,6480,135 | 279 |
| 2 | 28560,−300,8880,45 | 267 |
| 3 | 7440,−300,18480,225 | 198 |

В каждом случае TMap class0/space: это открытая клетка, не камера внутри двери.
Top5 каждого уровня сохранён в `scene-host-report.json`. Начальные камеры,
top5 и четыре дополнительные камеры реальных LEVEL1/2 ceiling transitions
проверены целиком — **22 scene packets**. Вершины переводятся обратно через
cell.origin; каждый XYZ/UV/material/source ID совпадает с полным canonical
geometry dump, ранее проверенным по source constructor arguments. Отдельная
рациональная frustum модель проверяет точное множество retained quads,
source/rejected/published counts. Это обнаруживает пропавшие ceiling bands
до GPU. Никакие producer-функции не импортируются в Python verifier.

Все **1444 conservative AABB** совпали с исходными control-point fixtures.
Выбранный superset сохраняет все четыре точки, отдельно от native overwritten
slot поведения. По 16 lifecycle checks на уровень: busy lease, неверный/double
release, generation exhaustion, invalid camera, missing bindings; output pointer
сохраняется при отказе. Candidate staging может меняться согласно контракту.

Положительная ветвь объединения отдельно проверена в
`ports/nds/tests/test_render_scene.c`: **20762 checks**, 1872 внутренних
узла сетки, 11 положительных pure merges. Два и три смежных прямоугольника
разной ширины в плоскостях XY/XZ/YZ сохраняют точное объединение и affine UV.
Проверены alias output=a и сохранение source ID первого прямоугольника.
28 отрицательных случаев покрывают UV reset/разрыв наклона, материал, flags,
цвет, непараллелограмм, щель, вырожденную площадь и границы XYZ/UV; при отказе
out сохраняется. Интерполяция использует все четыре вершины и целые дроби.

Дополнительный synthetic мир 3×3 с текстурой 1×1 проходит **production scene
prepare** с ненулевым объединением: 18 source quads минус 6 frustum rejects
дают 4 опубликованных quad и 8 merges. Проверены все 8 actual provenance records,
retained/consumed IDs, сохранённый source ID и мировая ширина объединения 1440.
Это новый контролируемый fixture, не изменение исходных assets и не DOS-снимок.
Таким образом, нулевая пригодность исходных UV resets не оставляет merge
feature непроверенной. Команда повтора:

```powershell
C:/Source/wasdify/.venv/Scripts/python.exe ports/nds/tools/check_merge.py
```

`merge-tests.json` сохраняет команды, source/executable SHA, native результаты
и Linux strict C11 cross-link; Linux executable не запускался. Source pins
проверяются до и после исполнения, прошлая зелёная проверка не переносится
на изменённую production revision. Per-vertex camera transform revision
scene.c SHA `c0afc03c71b5548ef3038bb26130f88e6f737fc78269f151db4ed7a5797745c1`
также прошла полный повтор host scene gate.

Пример команд из корня, CPython3.14.6, общий Zig0.13.0:

```powershell
$zig="$env:LOCALAPPDATA/Programs/Zig/0.13.0/zig.exe"
& $zig cc -std=c11 -Wall -Wextra -Werror -pedantic -O2 -I ports/nds/include -I build/nds/n07/generated ports/nds/tests/render_dump.c ports/nds/render/materials.c ports/nds/render/scene.c ports/nds/render/geometry.c ports/nds/core/world.c ports/nds/core/balance.c ports/nds/core/assets.c ports/nds/core/sha256.c ports/nds/platform/assets/file_reader.c -o build/nds/n07/render_dump.exe
C:/Source/wasdify/.venv/Scripts/python.exe ports/nds/tools/check_render_host.py --step 1
build/nds/n07/render_dump.exe build/nds/assets/dok-assets.ndspak 1 --scene 23760 -300 6480 135
build/nds/n07/render_dump.exe build/nds/assets/dok-assets.ndspak 2 --scan 1
```

Linux strict C11 cross-link выполнен тем же списком исходников с
`-target x86_64-linux-musl -o build/nds/n07/render_dump-linux`; executable не запускался.
Перед сборкой нужен generated `render_trig.inc` из N07 preparation script.

Первичные полные отчёты: `research/evidence/nds/n07/scene-host-report.json`,
`scene-host-levelN.json`, `scene-host-levelN-poseI.json`. Главный report содержит
source/executable/input SHA, описание scope, heavy entries для подготовки
renderer scenarios и passed=true только после всей проверки. При изменении
scene/materials/packet ABI повторить сборку и этот gate; прежние source pins
не доказывают новую ревизию. Accepted geometry и N03–N06 здесь не изменялись.
