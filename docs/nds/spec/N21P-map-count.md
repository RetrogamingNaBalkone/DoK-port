# N21P: поздний диагностический подсчёт PRE visited

C21 переносит только подсчёт ненулевых байтов PRE visited из prepare
в commit. Это поле34 диагностического descriptor, а не операция раскрытия
мира: class1, native producer, PRE/POST и все пиксели остаются прежними.

Prepare формирует private pending descriptor с нулём в поле34. Commit
считает тот же `visited_snapshot[n]!=0` до границы width×length, взятой
из полей8 и9 этого же pending descriptor, затем заполняет поле34 до
копирования в глобальный descriptor и публикации чётного sequence.
Размеры в prepare получены из тех же `control.width` и `control.length`,
которые задавали старую границу цикла. PRE snapshot между этапами неизменен.
Нулевой промежуточный счётчик не публикуется как завершённый снимок.

Ни новый state, ни RAM allocation, ни API/ABI не добавлены. Main, transfer,
header и растр C19 неизменны; p6 и p7 продолжают учитывать работу ровно
один раз как сумму собственных участков CPU. Это изменение размещения
диагностики относительно границы кадра, без заявления о сокращении её
полной вычислительной стоимости.

Предшественник C20 закреплён в `pre-map-count/pins.json`. Проверка
`check_map_count.py` и `map-count-source-supersession.json` восстанавливает
весь исходный runtime точным обратным преобразованием, проверяет неизменные
входы и порядок публикации. Она не заменяет ARM cache/timing и actual LCD
проверки. Root focused1066 прошёл функциональные/native/audio и строгие
map/GPU DMA gates; все1066 profiles присутствуют. Normal691 интервал:
437 по1 VBlank,254 по2 VBlank, ни одного3 VBlank, минимум29,913049 FPS.
Полная C21 матрица затем прошла: normal RU/EN по6637 интервалов без3VB,
minimum29,913049 FPS, full12 880 profiles/bank и дорогой audit. Root принял
103 отчёта; независимый архивный verifier повторил11 266 file-pin checks.
Это программная приёмка текущего прототипа; цель60 и physical DS не заявлены.
