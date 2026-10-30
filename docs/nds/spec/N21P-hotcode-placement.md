# N21P: размещение горячего кода в ITCM

C16 меняет только размещение функций через штатный SDK `ITCM_CODE`
(section и long_call). `dok/hotcode.h` по умолчанию разворачивает атрибут в
пустую строку; только N21P Makefile включает `N21P_ITCM=1`. Функциональные тела,
API/prototypes, данные, scratch и IRQ/stack настройки не менялись. В ITCM
перенесены geometry/scene/door/native discovery/raster и горячие submit/helpers
backend. Inner loops включены вместе с export-функциями; stream/loading остаются
в прежнем размещении. Это эксперимент с размещением, без новой модели cache.

Root независимый ELF/layout audit обнаружил, что до переноса ITCM занимала
1136 B из32768 B, а горячие функции находились в main RAM. SDK linker задаёт
LMA ITCM у начала BSS; после стартового копирования этот staging переиспользуется
BSS. Поэтому перенос не требует постоянной второй копии в main RAM. Фактический
heap всё равно проверяется запуском, поскольку alignment/veneers меняют образ.

`py -3.14 -B -X utf8 ports/nds/tools/check_hotcode_placement.py` проверяет полное
обратное преобразование исходников к C15 удалением только атрибутов/include и
Makefile additions, затем анализирует actual ELF через SDK objdump/nm.
Frozen C16 RU: `.itcm`28872 B, VMA0x01000000, запас3896 B. Обязательные
`build_cell`, scene/merge/doors, native record/projection/update, raster, submit
и planned_cell_clip имеют адреса внутри этой секции. Унаследованные SDK ITCM
symbols имеют прежние размеры, `.dtcm` остаётся0 B, размер `vblank_irq` прежний;
межрегионные Thumb veneers присутствуют. Полный отчёт с SHA и layout:
`research/evidence/nds/n21p/hotcode-placement.json`.

Runtime cadence, SDK/reserve и все native/LCD/GPU/DMA gates пока выполняет root.
Каждый итоговый SCN/frontend/fixture ELF должен отдельно удовлетворять32 KiB,
поскольку набор связанных SDK-функций может отличаться. Physical DS и полный
stack/IRQ high-water остаются N24. Ускорение ещё не объявляется подтверждённым.
