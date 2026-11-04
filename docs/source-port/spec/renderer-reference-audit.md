# SP07 — приёмка renderer, 4 ноября 2026

Актуальная сводка заменяет промежуточные выводы предыдущего исследования.
Полные RAM, indexed кадры, diffs и read-only Ghidra exports находятся в
`research/evidence/source-port/sp07-render/`; исходные DOS-эталоны не менялись.
Компактная машинная матрица: `analysis/source-port/sp07-render-summary.json`.

## Матрица критериев

| Критерий | Статус | Результат |
|---|---|---|
| Старт и контрольные позы всех семи уровней | PASS | PKR/PKE LEVEL1–3: heading90/0/45/135, каждый51200/51200; OG LEVEL4:83/0/45/135, каждый54400/54400 |
| Геометрия контрольных поз | PASS | Все массивы найдены в coherent RAM; OG center сравнивается по XY, поскольку depth slots исходник не инициализирует и не читает |
| PKR VESA640×480/800×600/1024×768 | PASS | Каждый при0/45/135:247040/247040,385600/385600,632832/632832 |
| PKE VESA | PASS с указанной историей framebuffer | При0/135 exact; при45 неперерисованные щели1/1/2 pixels удерживают предыдущий кадр; см. проверку sentinel ниже |
| 640×400 | NOT_NATIVE | Недостижимый backend mode100h в данных PKR/PKE; размер порта — расширение |
| OG VESA | NOT_NATIVE | Исходный OG backend только VGA; высокие размеры порта — расширение |
| Getters/общие анимации | PASS | 20 clips, включая стену OG H=72, предмет $=36 и Kremlin X/Y/Z; class 6 — base getter |
| Размеры/flags/cache | PASS | 7 уровней×5 размеров; software/real GL, изменение размеров/flags immutable image, повтор/возврат/свежая сцена |
| GL Hor+/HUD/фильтры | OUT_OF_SCOPE | Существующий modern viewport намеренный; отдельная матрица SP08/SP15 не закрыта |
| Время class 12 воды | ADAPTATION | Непрерывные18 фаз/334 ticks; исходный interval0/redraw pacing не эмулируется |

PASS относится к указанным кадрам и контрактам, а не к полным прохождениям.
Обычный renderer исправляет две ошибки исходника и очищает framebuffer. Для pixel equality включён
только диагностический replay этих ошибок, недоступный в UI и обычной игре.

## Метод coherent capture

`capture_vesa_reference.py` и `capture_og_reference.py` запускают изолированную
копию игры через DOSBox-Staging HTTP; каждый процесс имеет собственный порт.
Native glyph читается из object+14h (class+12h), pose+26h, image far+44h.
`extract_render_state.py` связывает glyph/class с definition/owner, передаёт
настоящие pose/flags/door extent и pixels/crop state; положения не угадываются.
Getter crop: пары right/left/down/up, shift=(dimension/divisor)*index,
циклический вариант имеет приоритет. Scene принимает immutable Image.

`native_draw_trap.py` временно заменяет две **guest RAM** инструкции эпилога
на EB FE, проверяет реальный CS:IP и читает offscreen framebuffer + RAM после
завершения world draw, до flip. Затем finally восстанавливает исходные байты
и проверяет их чтением; subprocess завершается при успехе и ошибке.
Оригинальные EXE не патчатся. Trap-адреса:

- PKR Ghidra NE1100:05a3, runtime03e7:05a3, linear1b7f73, bytes5f5e.
- PKE Ghidra NE1060:1531; фактические runtime selector/linear в capture report.
- OG Ghidra MZ203d:04e4, runtime11e0:04e4, linear122e4, bytes5e5d.

Protected-mode selector разрешается через LDT, **не** selector×16.
Например PKR DS04df descriptor1ff0208 = eef0e0381df20000,
base1d38e0; LDT1fefd30. Surface065f:0000 descriptor1ff0388 =
ffff40721ef20000, linear1e7240. OG real-mode DS23f8 определяется строкой
`Not same device` в DS:189d; camera DS:1fba, framebuffer far DS:ce92.
Старая строка GAMEPAL в каталоге ресурсов не является DGROUP anchor.

Основные samples: `{ru,en}-level{1,2,3}-vga-08` — три контрольных heading,
`*-vga-11` — стартовый heading90, `og-dos-03` — четыре OG heading.
VESA PKR: `ru-level1-{2,3}-09`, `ru-level1-4-10`; PKE: `en-level1-{2,3,4}-12`.
Первичные polling/menu snapshots не используются как критерий PASS.

## Восстановленные различия и ошибки

**LIGHT history.** PKR 1120:0bb7/0bef–0c13 и vertical counterpart сохраняют
DS:c146, когда vertex0 уже имеет максимальную major coordinate. Следующий
polygon использует старый индекс при интерполяции depth/LIGHT. Старый RU
начальный кадр: default49999/51200, replay51200/51200; все1201 pixels объяснены.
Default high=0 — намеренное исправление. `RasterReferenceState` отдельно
воспроизводит историю; bounds проверяются после выбора high, что сохраняет
переход quad→triangle. Проверены обе ветви и invalid прежний index.

**Packed sampler.** PKR 1010:0030–00af упаковывает fracV/address в ESI,
fracU/counter в EAX/ECX. ADD/ADC перенос младшего WORD влияет на texel;
независимые Q16 U/V не эквивалентны. OG 2b57:1067–10e3 использует тот же
ADC, но в младшем U word находятся destination offset/+1. Перенос
воспроизведён явно unsigned32/64. Unit-vector отрицательного whole U step
показывает исходный texel8 вместо0. Это устранило наклонные seam differences.

**VESA bank boundary.** PKR 1010:009d/00ba при wrap destination DI пропускает
UV advancement. Именно это объясняет все23/71/78 и12/16/16 отличий PKR
640/800 до replay;1024 не пересекает банк внутри строки. В обычном renderer
шаг не пропускается: исправлена исходная горизонтальная полоса. Только
`--native-light-history` render_probe включает оба legacy diagnostics.
Synthetic257×257 проверяет normal/replay на границе65535→65536.

**История framebuffer.** PKE VESA heading45 оставляет1/1/2 неперерисованных
пикселя:640(396,309),800(737,446),1024(851,549)/(944,572).
В native они равны предыдущему heading0 (46/46/45/45), в очищаемом порте0.
Контроль `en-level1-{2,3,4}-13`: после trap framebuffer залит251, следующий
исходный draw оставил во всех четырёх точках251. Это прямое доказательство
дырок покрытия и зависимости от старого кадра, а не предположение по цвету.
`check_framebuffer_history.py` проверяет sentinel и предыдущие значения;
`framebuffer-history.json` сохраняет результат. Production clear=0 оставлен
намеренно; не заполняем дырки чужими DOS pixels ради PASS.

**Асимметричные cell bounds.** PKR 1098:3335/3343, PKE 1030:3859/386b,
OG 21b8:1a81/1a8f при вычислении максимума сравнивают уже уменьшенный минимум,
а не второй ближний угол. Порт восстановил исходный набор клеток. Для
RU LEVEL2 pose(20880,-300,3120,135), side480: X34…44/Z0…8, не X34…45.
Лишние три offscreen primitives меняли Shellsort равных ключей:115→112,
остаток151→23, затем packed ADC устранил23. Native/port sorted112 descriptors
и159 vertex arrays совпадают. Unit-tests проверяют bounds и влияние лишнего
невидимого элемента на equal-key order. Это общая граница activation,
поэтому повторены7-level cadence и27 monster groups/36 FPS replays;
статус SP12 этим не меняется.

**OG camera/raster/sort.** Реальная camera W320/H171, clip320×170,
center159,84, D200/k8/K4096. Classic panel29 строк, weapon bottom169;
Kremlin остаётся clip320×160/panel39. OG использует Bresenham horizontal
edges, half-step UV, исключительные правую/нижнюю границы, raw transparency
до LIGHT. Default floor UV=width−1/height−1, explicit wall UV=width/height.
Ghidra2b57:0a0d/0b36/0d35/0f01/1301. Один LIGHT row на polygon:
abs16(lateral)+abs16(depth), WORD sub1008/clamp0,/154, cap15;
billboard использует world dx и sub2184 (24cf:0fb4/0fbe).
Floor sort24cf:1964–19a0/1f8f–1fcb = ABS(mean depth + camera-relative Y/4).
Именно отсутствующий Y/4 оставлял последние4 pixels OG 83: floor ошибочно
перекрывал wall. После исправления все4 OG кадра exact.
Center constructor OG хранит XY, оставляя depth slots неинициализированными;
проверка их случайных байтов была ложным требованием. Raster их не читает,
LIGHT и sort вычисляются отдельно. GL получает постоянный OG row отдельным
shade_depth; geometric depth/perspective/depth test сохранены.

**Режимы.** PKR 1138:0180/PKE 1080:0180 — stub: return(mode==101h).
Auto проверяет100h, получает0 и выбирает101h; -r1/-r2 явно101h,
-r3=103h, -r4=105h. Поэтому640×400 отсутствует в достижимой матрице
этих EXE независимо от BIOS. S3 и oldvbe подтвердили640×480; ET4000 без
VBE дал неинициализированные0×0, что не принято за кадр. OG main20c4:03b0
имеет sound switches, его backend2b57:005b выбирает только VGA.

| Native mode | Camera H | Clip rows | K |
|---|---:|---:|---:|
| Kremlin320×200 |161|160|4096|
| Kremlin640×480 |386|386|8192|
| Kremlin800×600 |483|482|10240|
| Kremlin1024×768 |618|618|13107|
| OG 320×200 |171|170|4096|

Software frame rows при800×600 равны482; последний clip row481.
Modern GL сохраняет160-row logical view, Hor+ и современный HUD;
GL classic HUD/filters остаются SP08. Software high-mode HUD масштабируется
из VGA и не заявляет попиксельной DOS VESA панели.

## Getters и кадровые кеши

Animations::image(owner) связан с software и GL, включая стены. При смене
immutable pointer обновляются размеры, flags и pixels, не только texture.
Class6 vtable slot+8: PKR 1190:063d→1068:04ae,
PKE 10d0:0656→1020:098e, OG 3255:0612→1ae1:0389 — base getter SP06.
SwitchState не доказывает отдельную анимацию; его callback/use относится кSP10.
Class7 crop getter PKR 1068:320b потребляется door_image. Class12 setter
10f8:0592/getter05c9 задаёт crop divisor/index и scratch pixels;
порт использует чистое преобразование и стабильные18 фаз/334 ticks.
Monster/projectile/weapon draw state остаётся у соответствующих служб;
этот этап не меняет баланс и не закрывает SP10–SP12.

## Воспроизведение

```powershell
py -3.14 -B ports/sdl2/tools/export_render_audit.py
py -3.14 -B ports/sdl2/tools/capture_vesa_reference.py --language ru --level 2 --mode vga --label NEW --controls --trap
py -3.14 -B ports/sdl2/tools/capture_og_reference.py --label NEW
py -3.14 -B ports/sdl2/tools/check_trap_matrix.py
py -3.14 -B ports/sdl2/tools/check_vesa_trap_matrix.py
py -3.14 -B ports/sdl2/tools/check_native_order.py
py -3.14 -B ports/sdl2/tools/check_framebuffer_history.py
powershell -NoProfile -ExecutionPolicy Bypass -File ports/sdl2/tools/build.ps1 -Configuration Release
build/sdl2/windows/dok_animation_scene_tests.exe originals/pkr/RUS/PKR.RES originals/pkr/ENG/PKE.RES originals/Old_Gold_3D/OLD_GOLD.RES
```

Capture не перезаписывает существующий label. Матрицы ссылаются на перечисленные
контрольные samples; новый label требует явного добавления в список.
ExportSourcePortRender.java запускается read-only, проверяет SHA-256 EXE
до/после и сохраняет manifest. `checks/report.json` содержит команды и коды
финальных resource regressions. CTest 15/15; animation35 scenes/20 clips,
cadence7 levels, monsters27 groups/36 replays, OG 600 frames/105 рублей/2 палочки/exit,
исходные door texels PKR 1622/PKE 1388 — PASS.

## Хранение итогового исследования

Coherent `heading-*-memory.z`, indexed frames, state bindings, Ghidra exports
и все текущие входы матриц остаются распакованы. 61 холодный полный RAM
(1,526,726,656 bytes) упакован в проверенный
`research/experiments/storage-archives/sp07-cold-memory-20261104.zip`
(39,332,775 bytes), с SHA-256 каждой записи. Исторический
`check_state_matrix.py` вызывает extractor, который при отсутствии raw RAM
восстанавливает **конкретный файл** через `scripts/storage_archive.py restore`.
Например:

```powershell
py -3.14 -B scripts/storage_archive.py restore research/evidence/source-port/sp07-render/ru-level2-vga-05/memory.bin
```

`check_scene_reference.py` использует старые `reference/ru-03,en-02`,
не затронутые этой упаковкой. Дубли isolated game copies удалены после проверки
resolved paths внутри SP07 research; оригиналы и build inputs сохранены.
`archive_render_research.py` и `storage-report.json` фиксируют операцию.
Пакет проверен шестью реальными процессами (три профиля × software/GL),
все exit0, без GL fallback; `checks/package-report.json` содержит hashes/команды.
