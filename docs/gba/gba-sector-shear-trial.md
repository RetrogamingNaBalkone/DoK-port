# G20A: широкие affine-фрагменты с PC shear

Итоговое продолжение: [handoff](gba-optimization-handoff.md) завершает текущий
goal на анализе без Performance ROM. Проверка sparse columns ограничена
offline feasibility и128 cost profile; native run sampler не измерен.
Фразы ниже об открытом goal относятся к моменту завершения GPU trial.

27 октября 2026 года. Оставшаяся крупная GPU-проверка выполнена; вариант
**не выбран**. Для основной архитектуры оставлен2775: аппаратные базовые
плоскости и программный foreground с cached geometry/columns/rectangle path.
Плановый перебор крупных архитектур закрыт. Это решение по проверенным
вариантам, не доказательство предела GBA или невозможности будущего движка.
Goal ещё открыт: ограниченная проверка формата прозрачных колонок, затем
итоговый handoff оставшихся расходов. **Performance ROM не делать.**

Сводка: [g20a-sector-shear-trial.json](../../analysis/gba/g20a-sector-shear-trial.json).
ROM `hw_v128_sector_gpu_shear`, SHA
`484099f868157067871d6241b61dfe1afcfd97951f9d0944045c06a6bf9deaae`,
18 754 724 байта. Final inputs/ELF/map/ROM/QA:
`research/experiments/g20a-sector-shear-final-v4/`; измерения и pressure diagnostic —
`research/experiments/g20a-sector-shear-v4/`. GPU ROM и2775 byte exact в двух деревьях.
Диагональный GPU b2b16b…ca3d6e и CPU Mode2 fe51e2…52adcc также воспроизведены
с прежними SHA. Игровые artifacts2775/96/72 сохранены.

## Что изменено относительно диагонального GPU

[Mode2 backend](gba-sector-gpu-checkpoint.md), мипы, исходные16 LIGHT rows,
CPU стены/уступы/низкие потолки и BG3 базовые плоскости сохранены. Новый
constructor выбирает максимально возможные исходные прямоугольники,
уменьшая их при нарушении ограничений. Используются PA/PD и PC shear,
PB0. Каждая целочисленная mip-U граница проверяется по квантованным PA/центру:
ошибка не более2 физических X pixels. Разность native высоты на краях
фрагмента — не более2 CPU-строк. Квантование и source clipping дополнительно
оцениваются по фактическому PPU и сравнением alpha mask; эти два fit bound
сами по себе не означают принятую картинку.

Source clip учитывает PC отдельно для физических X, затем объединяет
разрешённые V intervals консервативно. Canvas выбирается с учётом наклона;
короткий последний fragment не получает слишком маленький bounding box.
Динамическое отсечение по-прежнему считается один раз на грань, без экранного
per-pixel alpha/coverage prepass.

Admission уточнён: поиск учитывается до последнего активного OAM, affine
cost не включает повторно те же2 search cycles. Рост search cost проверяется
для пика **всех** строк, а не только нового canvas. Основание — просмотренные
27 октября [OAM preprocessing mGBA](https://github.com/mgba-emu/mgba/blob/master/src/gba/renderers/video-software.c)
и [расчёт sprite cycles](https://github.com/mgba-emu/mgba/blob/master/src/gba/renderers/common.c).
Это текущий upstream master; конкретный исполняемый core отдельно закреплён
SHA5cac11…667e4. Его фактическое поведение проверено ниже, physical GBA нет.

## Производительность и настоящее переполнение PPU

Native16 777 216 cycles/s, WAITCNT4317, PCM22050Hz, одинаковые128 CPU-строк,
исходные карты/камера, без overclock и capture hold в timed intervals.

| Вид |2775 FPS|Shear FPS|CPU ratio к2775|
|---|---:|---:|---:|
|L1,520,225°|9,95|5,43|2,10×|
|L1,3640,225°|11,95|5,43|2,19×|
|L2,1592,135°|9,95|4,59|2,38×|
|L2,2871,225°|9,95|4,59|2,35×|
|L3,1544,315°|14,93|7,47|2,35×|

Во всех пяти production views admission превышен, затем рисуется полная
CPU-динамика. Это не FPS успешно отрисованных GPU тяжёлых сцен. Held profile
без forced PCM: перед fallback GPU-подготовка стоит0,566–0,939M cycles.
Short live24 RU/EN10,54/8,05/12,57 против2775 19,37/19,11/27,57.
Underruns0, time-conservation error0; сохранённый motion debt не budget pass.

Чтобы не принять консервативный allocator за предел PPU, добавлен отдельный
`g_sector_gpu_pressure_probe`, по умолчанию0. Он обходит scan rejection и
пропускает команды при жёстких OAM/atlas/matrix caps. Это диагностический
набор загруженных команд, **не полноценная сцена, FPS или quality profile**.

|Pressure view|Загруженные OBJ|Потерянные actual PPU pixels относительно этих команд|
|---|---:|---:|
|L1/520|41|732|
|L1/3640|23|8|
|L2/1592|32|222|
|L2/2871|0|CPU fallback: последующая грань не проходит fit bound|
|L3/1544|55|1368|

Все ненулевые расхождения относятся к хвосту OAM после исчерпания строчного
budget по модели обхода core, а не к неверным текстурам/матрицам. Таким
образом, в четырёх видах показана реальная потеря PPU в данном наборе команд,
а не только отказ нашего admission. В пятом не получено допустимое
представление последующей грани. Добавление статических GPU-стен не устраняет
переполнение уже этих динамических команд. Полный GPU static renderer не
создавался; новые способы ресэмплинга/сжатия/предварительного alpha occlusion
потребовали бы нового исследования, они не объявлены невозможными.

## Квалификация и решение

58 normal poses RU/EN,6 активных GPU Pause/Mode4/Resume cycles,18 weapon poses;
46 поз используют GPU,8 — реально ненулевые PC matrices. Original dynamic
geometry/leases и native HUD/weapon exact против2775. Для GPU и CPU Mode2
проверено по2 227 200 actual PPU/base pixels. GPU:56 810 nonzero original
mip/LIGHT texels и71 378 candidate pixels по static sector clip contract.
Padding/VRAM/OAM/matrices/scan budget/guards прошли. Полнота динамической
видимости и сами static histories этим независимо не доказываются.

По58 позам сравнение с CPU alpha mask даёт8750 missing/3464 extra physical
pixels, максимум304/123 на позу. После radius2 остаются868 missing interior
и28 extra beyond tolerance, максимум64/8. Повторяющиеся weapon cameras входят
в сумму. Эта потеря **не принята как незначительная**. Full440, новые map edits,
полная игровая регрессия и physical GBA acceptance этого проигравшего варианта
не выполнялись.

EWRAM261364/free780; IWRAM24196/free380 до8192 bytes stack reserve.
PC state дополнительно занимает64 bytes в covered[], без нового framebuffer.
Память/стеки — в JSON; IRQ и main margins проверены в native scope.
Originals и SDL2 не менялись. Координаты — world units GBA-порта; MMIO/VRAM —
его runtime addresses, не EXE/Ghidra/NE/DPMI. Asset package остаётся3efa5d…4351.

```powershell
& C:/Source/wasdify/.venv/Scripts/python.exe ports/gba/scripts/report_gba_sector_shear.py
```

Не продолжать подбор размеров/порогов этого GPU-пути. Основная ветка —2775;
96/72 остаются отдельными quality comparisons. Допустима только ограниченная
проверка оставшегося CPU alpha/column expense без изменения картинки;
дальность, AI radius и будущие графические уступки вынести в итоговый анализ,
не реализовывать как Performance-сборку в этом goal.
