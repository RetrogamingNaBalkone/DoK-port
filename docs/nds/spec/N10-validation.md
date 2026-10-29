# N10: проверка collision и occupancy

Архитектура и границы: [N10-collision](N10-collision.md). Реализация независимого
NDS-порта, без заимствования GBA-модулей. Статус задачи и итоговые хеши
определяет [паспорт N10](../tasks/N10.md). Этот документ описывает область
проверок; наличие инструмента само по себе не означает пройденную приёмку.

## Сохранение принятых входов

`research/evidence/nds/n10/initial-dependency-integrity.json` фиксирует 347
принятых входов N01–N09; SHA-256
`0f3fcc9276004bffcf4fbe6adc1110be0e577a6f97b9c948ff270c83447bb9b8`.
[check_collision.py](../../../ports/nds/tools/check_collision.py) сверяет
каждый размер и SHA до и после нового прогона. Предыдущие задачи не
пересобираются и их выходы не регенерируются.

[prepare_collision_fixture.py](../../../ports/nds/tools/prepare_collision_fixture.py)
копирует точные принятые N07 trig/scene.dat/scene-manifest и N04 asset package
в собственный `build/nds/n10/`. Trig и сцена не генерируются заново.
`scene.dat` предоставляет палитру, HUD и диагностические записи; collision
читает исходные карты и определения N05, не GPU mesh.

## Независимые источники и новые реализации

Оригинальные инструкции PKR/PKE из свежих Ghidra-экспортов исполняются отдельным
oracle. Он сравнивает source footprint/resolver и occupancy wrapper с
[collision_dump.c](../../../ports/nds/tests/collision_dump.c), который лишь
декодирует явный бинарный протокол и вызывает новое C-ядро. Порядок аргументов,
подшагов, карты и счётчики проверяются отдельно от renderer и ввода. Хеши EXE
до/после, адресная система и пределы oracle фиксируются в N10-evidence.

Охранные отказы NDS около реального края и при повреждённом состоянии
проверяются отдельно от native clamp. Их нельзя включать в число совпадений
с оригиналом. Полная DOS/DPMI-сессия этим oracle не воспроизводится.

[collision_cases.c](../../../ports/nds/tests/collision_cases.c) — общий host/ARM9
набор синтетических случаев: узкий проход, порядок осей, края, препятствия,
самоисключение, controlled door flags/counters, reference/revision и чистая
проверка камеры. Его caller-owned workspace занимает world slab до загрузки
уровня. [collision_routes.c](../../../ports/nds/tests/collision_routes.c)
проверяет соединение input/controls, N08 clocks/events, N09 motion/camera и
N10 collision на каждом загруженном исходном RU-уровне.

## Явные timeline и языки

Каждый маршрут исполняется трижды с одинаковыми аппаратными образцами и
service samples; отличаются только наблюдения камеры: 0/30/60 Гц. Первые 64
service samples имеют шаг 80 native ticks, следующие — 700; это данные
диагностического опыта, не частота игрового цикла. Событие героя сохраняет
исходный интервал 416 ticks, первый elapsed равен нулю, stopwatch запускается
после движения. Вторая часть маршрута держит Up+Right+L и встречает реальные
препятствия на всех трёх картах. Пауза и короткие фронты включены в timeline.

На каждом service сравниваются полные scalar motion/camera/owner/command
состояния, счётчики callback и FNV-дайджесты карт/runtime-таблиц. В конце
сравниваются также все байты четырёх карт и flags/door counters. Наблюдение
камеры проверяет отсутствие изменений движения, истории и occupancy. После
скорректированного/отклонённого движения история сбрасывается. Интерполяция
проходит чистый footprint admission; при отказе используется committed pose.

Рабочие карты и collision state восстанавливаются при возврате из shared
route; event bank и scratch считаются разрушенной диагностической памятью.
Host workspace — 113112 байт; compile-time gate требует не более 131072 байт
также на ARM9. Event bank предоставляет отдельный events slab.

Host driver реально загружает и связывает RU/EN presentation, сохраняя один
RU physics world. Для каждой локализации повторяется тот же маршрут;
сравниваются итоговые scalar state, карты, counters и trace hashes. Это не
утверждение о готовой смене языка renderer/UI: их завершение относится к
последующим задачам. Синтетические и native geometry cases не добавляют
вертикальный интегратор; y/bounds/FCMap не участвуют в этом horizontal path.
Class12/13 floor effects полного callback героя остаются отдельной работой.

## Команды

Из `C:/Source/DoK-port`, PowerShell:

```powershell
C:/Source/wasdify/.venv/Scripts/python.exe -B ports/nds/tools/check_collision.py --pins-only
C:/Source/wasdify/.venv/Scripts/python.exe -B ports/nds/tools/check_collision.py --host-only
C:/Source/wasdify/.venv/Scripts/python.exe -B ports/nds/tools/check_collision.py
```

Последняя команда выполняет strict host checks, native oracle и сборку ARM9,
но не заменяет emulator gate. Linux x86_64-musl компилируется и связывается
статически, не исполняется в Windows. Общие Zig 0.13.0 и BlocksDS/GCC
используются из пользовательского каталога; 20 SDK packages сверяются с
неизменённым toolchain lock. Источники/выходы и точные команды сохраняются
в `research/evidence/nds/n10/host-check.json`, source/build manifests.

Host report `shared_suites.routes.levels` содержит отдельные записи
`level=1..3, language=1..2`. ARM9 исполняет unit suite один раз до загрузки,
shared route — один раз на каждом RU-уровне. Поэтому число host проверок
включает также driver assertions и EN-повторы и не равно ARM9 total.
Для текущего набора: 107 unit checks; по 1643 route checks на RU-уровень,
всего 4929; ARM9 total 5036. Эти числа проверяются по свежему host report.

Эмулятор получает физические кнопки/touch через frontend и читает seqlock ABI;
запись RAM/CPU/часов и инъекция результата запрещены. Live маршрут по трём
уровням — диагностическая последовательность, не автоматическая кампания.
Принятый runtime:291 exact x86 callbacks/720 подшагов,294 snapshots,672 camera
observations, три паузы по48 VBlank; навигация подтверждена на каждом уровне.
Конкретные команды, фактические проверки и кадры записаны в паспорт N10. Физические DS/DS Lite, flashcart/loader, реальные FPS,
latency, IRQ и stack peak проверяются на итоговом хеше в N24 по
[политике проверки](validation-policy.md).
