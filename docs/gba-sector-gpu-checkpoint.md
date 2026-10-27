# G20A: секторное отсечение и аппаратный вывод объектов, контрольная точка

27 октября 2026 года. Реализован отдельный Mode2 backend: программный
foreground на affine BG2, аппаратные базовые плоскости на BG3 и игровые
объекты на OBJ после секторного отсечения. HBlank-перестановки OBJ и прежней
связи через экранную coverage нет. Текущий диагональный вариант **не выбран**;
он ещё не завершает оценку широких affine-фрагментов с shear.

Проверенный ROM — `hw_v128_sector_gpu_thumb`,
SHA `b2b16b9e3831b8f0d8adcafdaebce8b25ada88e96c7e5bf51a72c4656eca3d6e`,
18 754 460 байт. Транспортный контроль с CPU-объектами —
`hw_v128_sector_gpu_off`,
SHA `fe51e20f76d9d8e05042988f6fa2ff458c89432d7d1a6ac8178b8ff61c52adcc`.
Сводка: [g20a-sector-gpu-checkpoint.json](../analysis/g20a-sector-gpu-checkpoint.json).
Goal/G20/G20A остаются открытыми. Performance ROM в текущий goal не входит.

## Что именно проверено

Дерево, стены, уступы, низкий потолок и базовый секторный clip взяты из
[sector trial](gba-sector-clip-trial.md). Статические стены/уступы/низкий
потолок пока рисует CPU. Этот опыт **не является полностью аппаратным
рендерером статических стен**.

У каждой динамической грани один раз вычисляются clip/deny intervals
по120 колонкам. Полностью скрытые грани пропускаются до подготовки OBJ.
В окончательной текстуре остаются только консервативно разрешённые исходные
mip-тексели; PPU выполняет affine sampling, alpha и порядок объектов.
Это source-space copy со светом, без экранного per-pixel depth/owner или
дополнительного alpha/coverage pass. Целые плоские billboards используют
крупный OBJ; перспективные панели пока делятся на узкие диагональные полосы.
Матрицы имеют PA/PD, PC/PB пока0.

Мипы исходных image/animation ресурсов:8×16,16×32,32×64 и64×128. Они повторно
упакованы в tile order, без процедурной замены рисунка. Уникальный незатенённый
банк занимает1 912 576 байт. Исходные16 LIGHT rows применяются ARM-копировщиком
при подготовке отсечённого source patch; это не бесплатное GPU-освещение.
Хранение всех заранее затенённых mip для всех кадров дало135 571 968 байт
только банка и было отвергнуто до native ROM. Нынешний банк сохраняет все16
градаций через runtime LIGHT и помещается вместе с движком.

Слои: UI OBJ priority0, игровые OBJ priority1, CPU BG2 priority2, базовые
плоскости BG3 priority3. CPU BG2 —120×128 в128×128 tile bank, PA128/PD228.
UI и оружие сохраняют native240×160. Оружие готовится перед native scene:
его staging не пересекается с очищаемым16KiB foreground, а GPU admission
получает актуальные границы оружия для каждой строки. В Mode4 остаётся прежний
порядок overlay.18 weapon fixtures проверены против2775.

Освободили строчный OBJ-бюджет, ранее занятый четырьмя CPU-полосами. У нового
вывода нет HBlank OAM access, DISPCNT`1c42`, bit5 выключен. Admission использует
1210-cycle строку,256 cycles на поиск всех128 OAM entries и реальные верхние
границы UI по строкам. Матриц32, игровых OAM slots64, динамический atlas16KiB.
При невозможности разместить все объекты весь динамический проход возвращается
на CPU; смешанного CPU/GPU порядка объектов без доказанного перекрытия нет.

Размещение VRAM: near/mid plane tiles в bank0, far tiles в bank1 с пропуском
tile indices96..99 под CPU affine map`0x06005800`. Шесть base-plane maps занимают
`0x06006000..0x0600bfff`, CPU tiles —`0x0600c000..0x0600ffff`, OBJ atlas —
`0x06010000..0x06013fff`, UI/weapon — верхние16KiB OBJ VRAM. ARM map writer
переставляет far tile indices; source/pixel oracle подтвердил исходные
32×32 материалы и все три фоновые LIGHT bands после переноса на BG3.

## Замеры и решение по этой версии

Native-clock mGBA SHA5cac11…667e4, WAITCNT4317, PCM22050Hz, одинаковые карты,
128 CPU-строк, без overclock/capture hold в timed интервалах. Цифры ниже —
предъявленный FPS. **Во всех пяти тяжёлых видах этот GPU-вариант отклоняет
admission и рисует динамику на CPU после потраченной подготовки OBJ.**

| Вид | Контроль2775 | Mode2, CPU dynamics | Mode2, попытка GPU / CPU ratio к2775 |
|---|---:|---:|---:|
|L1,520,225°|9,95|6,64|4,98 /2,29×|
|L1,3640,225°|11,95|8,53|5,43 /2,06×|
|L2,1592,135°|9,95|5,97|4,27 /2,52×|
|L2,2871,225°|9,95|5,43|3,98 /2,60×|
|L3,1544,315°|14,93|9,95|5,43 /3,12×|

Причины отказа: scanline capacity у четырёх видов, atlas у L2/2871. Это не
сравнение пяти успешно отрисованных GPU тяжёлых сцен. Обычные стартовые виды
работают на GPU:46 из58 QA поз используют реальные OBJ. Переход от ARM к Thumb
для ROM orchestration, общие clip на грань, ранний пропуск невидимого,
минимальные canvas и целые flat billboards уже включены в этот checkpoint.

Short live24 RU/EN, камера следует игроку, поворот/огонь:

| Вариант | L1 | L2 | L3 |
|---|---:|---:|---:|
|2775, свежая пара|19,37|19,11|27,57|
|Mode2, CPU dynamics|11,03|10,24|14,63|
|Mode2, попытка GPU|10,70|8,33|12,91|

PCM underruns0, time-conservation error0. Motion debt остаётся; соблюдение
бюджета симуляции не заявляется. Отдельный инструментированный held profile,
без forced PCM: до возврата на CPU подготовка GPU стоит0,792–1,274M cycles
на пяти видах. Во многих видах уже эта работа сопоставима с полным CPU-кадром
контроля. Это повод сменить представление GPU-команд, не полировать этот цикл.

## Проверки и ограничения качества

`scripts/verify_gba_sector_gpu.py`:58 поз на GPU-кандидате и58 на CPU Mode2,
RU/EN,6 Pause/Mode4/Resume cycles с активными GPU-объектами и18 weapon poses.
Original projected dynamic geometry, image/surface leases и native HUD/weapon
overlay exact против2775. Guards/стек/VRAM/OAM/matrices/scan reservation прошли.
Для каждого Mode2-варианта проверены2 227 200 пикселей фактического PPU
и столько же исходных фоновых материалов после перестановки maps/tiles.

У GPU проверены56 444 ненулевых исходных mip/LIGHT текселя и70 446 candidate
GPU pixels по статическому sector clip contract. Это не отдельное независимое
доказательство clip histories или полноты динамической видимости.

Специальный отключённый по умолчанию trace собирает точную alpha mask CPU
динамики без изменения production timing. В сравнении с ней у GPU по58 позам
8908 missing и2858 extra physical pixels, максимум278/96 на позу. После
двухпиксельного допуска к границам остаются866 missing interior pixels
(максимум62) и12 extra pixels дальше двух пикселей от CPU mask (максимум2).
Камеры с разными weapon fixtures повторяются; суммы не равны числу уникальных
ракурсов. **Эта потеря силуэта не объявлена визуально незначительной или
принятой.** Вариант не выбран и по скорости, и по незавершённой оценке картинки.

Нового map-edit/full440/full gameplay/physical GBA acceptance нет. Ранние
версии с ошибкой physical/logical X и промежуточным admission не использовать
для решения; авторитетный checkpoint — V8. Static walls остаются CPU.

## Воспроизводимость и следующий ограниченный опыт

`build/g20a-sector-gpu-v8/`: private source, ELF/map/ROM, paired targets,
profile, visibility, live24. `build/g20a-sector-gpu-repeat/`: все три ROM
byte exact. Контроль2775 и artifacts96/72 сохранены с прежними SHA.
EWRAM261324/free820, IWRAM24196/free380 до8192 bytes stack reserve.
Main stack2544/margin5392, IRQ80/margin80. State19640 bytes заимствует
covered[] вместе с sector state13496 bytes; состояние пересоздаётся каждый
native draw, постоянного magic поверх Mode4 scratch нет.

```powershell
& C:/Source/wasdify/.venv/Scripts/python.exe scripts/prototype_gba_bsp.py --out build/g20a-sector-gpu-v8 --variants hw_v128_sector_gpu_thumb hw_v128_sector_gpu_off hw_v128_entity_rect
& C:/Source/wasdify/.venv/Scripts/python.exe scripts/report_gba_sector_gpu.py
```

Вход — GBA package SHA3efa5d…4351. Геометрия в мировых единицах порта,
package offsets относительно его начала; VRAM addresses выше — GBA runtime,
не адреса оригинальных EXE/Ghidra/NE/DPMI. Оригиналы и SDL2 не менялись.

**Не доводить локально диагональные узкие полосы.** Следующая оставшаяся
крупная проверка — широкие фрагменты перспективных панелей с affine PC shear
и измеримым ограничением ошибки. Это меняет число canvas/команд и качество
самой аппроксимации, поэтому текущий отказ по scan/atlas ещё не заменяет такой
опыт. Он должен дать disposition секторному GPU-выводу и остаточному бюджету
для статических стен; потом выбрать основную архитектуру, выполнить только
обоснованную ограниченную доводку и закончить goal итоговым remaining-cost
handoff. **Отдельную Performance-сборку не реализовывать.**
