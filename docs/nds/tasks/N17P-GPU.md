# N17P-GPU — статическая оценка переноса рендера на GPU

Статус: **статическая оценка завершена**, 29 октября 2026 года.
Дополнение к N17P по прямому запросу пользователя; не реализация N17/N17P
и не изменение очереди разработки.

## Контекст и границы

Корень `C:/Source/DoK-port`. Читать [AGENTS.md](../../../AGENTS.md),
[план](../plan.md), [handoff](../handoff.md), [N17P](N17P.md),
[принятое решение N13P](../spec/N13P-architecture.md).
Исследовать текущие `ports/nds/render/` и NDS backend, SDK BlocksDS,
принятые отчёты N13P. Разделить уже аппаратные операции, кандидатов переноса,
ограничения точности и оптимизации ARM9. Новые EXE-анализы, ROM-прогоны,
изменения renderer/gameplay и приёмка производительности не входят в запрос.
GBA/SDL2 не затрагиваются. Игровых сохранений нет.

## Проверки и результат

Подготовлены [заключение](../spec/gpu-offload-assessment.md), воспроизводимый
offline collector `ports/nds/tools/audit_gpu_potential.py` и локальный отчёт
`research/evidence/nds/gpu-static-20261029/report.json` с SHA входов.
Команда: `py -3.14 -B -X utf8 ports/nds/tools/audit_gpu_potential.py`.
Python 3.14; SDK BlocksDS 1.24.0-1, конкретные SDK bytes закреплены отчётом.
Проверены SHA двух принятых полных RU/EN отчётов N13P и fixture;
повторный разбор 1849 штатных nonloading presentations на язык совпал
с принятыми средними и худшим component sum. 23 source/SDK pins и 6 input pins.
Report SHA-256: `8e08d3e91832c1d2343ee2dae946e83eecaa9fce0b28dbf152ab653712a36a49`.

Потенциал: retained packed lists/DMA, cyclic вода через extended texture + UV
(96×96 повторяется в 256×128/128×256 без изменения texels), аппаратный overlay.
Крупнейший остаточный этап ARM9 требует также bounded кэша статической геометрии.
RU prepare/submit/upload mean 12,366/4,314/0,542 мс, worst sum 30,452 мс.
В худшем кадре даже условное удаление всего submit оставит 23,917 мс;
одни display lists не гарантируют 60 FPS. Ёмкость GE не равна raster throughput.

Новых ROM/EXE-прогонов, GPU-реализаций и тестов ускорения не было.
Оценки выигрыша/нового working set остаются гипотезами до парных опытов;
hardware остаётся N24. Engine, originals, статусы других направлений не менялись.
Текущий следующий игровой ID — **N17**; N17P остаётся не начатой.
Первый шаг N17 — native active callback attacks/hurt/death согласно handoff;
первый шаг N17P после N17 — совместная baseline с production fog, затем
проверка кэша/подачи команд/воды по заключению. Исследование не закрывает N17P.
