# N22: ошибка полного VRAM repack в обычном LEVEL2

Обнаружена при обычном кнопочном прохождении 30 октября 2026 года.
Частичное перемещение принято после независимых host и actual SDK проверок;
этот документ не объявляет приёмку всего N22.

## Воспроизведённый факт

Промежуточный production ROM `ab7a6040e4cc1a0c5aef469b5743a0a4a7578861e4684969b9de87720b50f693`
останавливается в LEVEL2 между submit11352 и presentation11351 на video frame
24879. Камера25099/−300/21361, heading38. Root повторил исходный адаптивный
controller из четырёх сохранённых Python-модулей на восстановленном **точном**
ROM. Совпали все11 351 опубликованные rows,4 150 joypad events, девять touch
Center,90 renderer fields, последний frontend и SHA трёх изображений.

Отказ — `stream_commit_prepared`, error line967 в тогдашнем main.c;
map/AI/combat/audio faults отсутствуют. До этого реальный мир пережил retry.
Полный repack дал:

| Показатель | Значение |
|---|---:|
| Resident textures / bytes |19 /358656|
| Rebuilds / uploads |1 /19|
| Upload bytes с palette |367360|
| DMA start / end VCOUNT |216 /133|

DMA закончилась в активной развёртке48…191 и нарушила принятый deadline
119 scanlines после presentation boundary. Guard обнаружил ошибку; расширять
его, считать обычный miss загрузкой или исключать интервал из gameplay нельзя.
Это ошибка управления ресурсами нового NDS renderer; DOS использует иной
тракт вывода.

Доказательства: `research/evidence/nds/n22/root-v14-reproduction.json`,
`root-v14-layout.json`, `root-compare-v14.py`, реальные probe/RAM captures в
`build/nds/n22/control-ab7-ru/route-1-level2-root-isolated-v14/`.
Точный контрольный ROM/ELF/map,191 восстановленный source/generated/ARM7 input
и manifests постоянно сохранены в `research/evidence/nds/n22/control-ab7/`.
Сборка: `ports/nds/tools/flow/rebuild_ab7_control.py`. Оригиналы не менялись.

## Причина фрагментации

Адреса ниже — физические LCD VRAM addresses Nintendo DS, не адреса Ghidra
и не смещения ресурсов. SDK addresses получены через независимо проверенную
структуру фактических texture descriptors контрольного ELF; никаких записей
в память эмулятора не было.

Перед отказом resident16. Новые запросы32768/16384/2048/256 требуют51456 B.
Удаляется один старый16384-byte блок по0x0681F000. Самая большая свободная
дыра после удаления —20480 B, меньше нового32768-byte запроса. Сортировка
только новых misses по размеру не устраняет этот конкретный случай.

Одно перемещение resident-hit16384 B по0x06823000 объединяет его со старым
освобождённым блоком и соседней дырой20480 B до0x0682C000: получается52 KiB.
Нужен upload51456+16384=67840 B; четырнадцать hits и palette остаются на месте.
Полный reset ошибочно перезаливал все358656 B и8192+512 B palette.

## Проверяемое исправление

`vram_layout.c` — чистый планировщик без heap и SDK. Он получает реальные
allocations и все missing sizes, проверяет два допустимых пула A+B и D
(банк C занят нижним экраном), alignment8, capacity128, overflow, пересечения
и уникальность slot IDs. Для выбранного окна проверяется размещение всех
missing и перемещённых hits largest-first/first-fit. Перебираются границы
изменения набора блоков и соседние8-byte точки; минимизируются дополнительные
upload bytes, затем число блоков и адрес.

Backend меняет только SDK metadata после fence, формирует перемещённые
descriptors из **текущего** demand и сохраняет source lease до prepare/commit.
VRAM payload переносится только прежним uploader в прежнем окне. Palette
не сбрасывается, нижняя карта и эффекты не сокращаются. Повтор ограничен
четырьмя попытками. Неразрешённое размещение в normal frame возвращает явную
ошибку; all-textures reset допускается только в настоящем cold-loading.

Production использует `N22_STREAM_PARTIAL_DEFRAG=1`; отдельный экспериментальный
`N22_STREAM_LARGEST_MISSES` остаётся0. Partial включает largest-first missing
автоматически. Старые контрольные e742/6975 не пересобираются; проверенные
candidate builds находятся в отдельных immutable targets.

Root независимый host review:394 checks, точный V14,64 перестановки inputs,
96 layouts с полным перебором всех8-byte окон и рекурсивным bin-assignment
oracle, atomic refusals, bank-C gap/cross-bank/overflow/misalignment/duplicate
slots/high bit127. Итог `research/evidence/nds/n22/root-vram-review.json`.
Это проверка планировщика; реальный SDK, DMA, VRAM и изображение проверяются
отдельной ARM9 fixture и обычными проходами.

Первый partial candidate: `build/nds/n22/candidate-partial01-ru/`, ROM
`e3785a18b402571d10c273b160df639d8499d27ad214a3ce890531aebabb9844`.
Build/source/inputs заморожены в
`research/evidence/nds/n22/candidates/candidate-partial01-ru/`.
Compiler `.su` даёт консервативную известную цепочку≈10184/12288 B;
это не фактический high-water с IRQ. Heap/reserve планировщик не использует.

[Actual SDK paired fixture](N22-sdk-fragmentation.md) прошла: geometry всех19
textures поставлена в очередь до commit, сохранились358656 B texels и8192 B
palette,19 LCD точек совпали точно в RGB555. Upload уменьшился367360→67840 B,
GPU VCOUNT216→133 сменился216→250,435294→123829 ticks. Подготовка не меняет
payload, активная развёртка по-прежнему отклоняется guard. Root независимо
проверил финальные manifests, payload и pixels:
`research/evidence/nds/n22/root-sdk-fragment-review.json`.

RU/EN candidate01 прошли отдельные natural-death и menu/new-world matrix.
Открыты обычные LEVEL2/3, полный набор прохождений и итоговый профиль N23.
