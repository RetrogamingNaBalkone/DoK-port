# N19: работающий HUD и локализация

29 октября 2026 года. Основа — PKR/PKE, сохранённые и обновлённые экспорты
Ghidra и исходные ресурсы. [Исходные доказательства](N19-evidence.md),
[паспорт](../tasks/N19.md). GBA-код не использован.

## Представление

`ports/nds/ui/hud.c` рисует индексный растр ровно 256×38; верхняя сцена
остаётся y=0…153. Исходные FONT8/FONT16 содержат 256 CP866-глифов 8×8/8×16,
MSB first. Новых букв или сжатия глифов нет. Иконки и бордюр берутся из
исходных ресурсов, масштабирование — nearest по центрам пикселей по N06.
Заливка имеет индекс86, текст2; прозрачность иконок — исходный индекс0.

По прямому уточнению пользователя в этом цикле числа приближены к своим
иконкам. Это осознанное изменение принятого N06-макета: теперь нет ведущего
пустого пространства от выравнивания числа вправо. Координаты относительно HUD:

| Элемент | x,y,w,h |
|---|---|
| Сердце | 4,3,17,14 |
| HP, левое выравнивание | 23,2,48,16 |
| Заряд: иконка | 82,2,13,16 |
| Заряд: число, левое выравнивание | 97,2,48,16 |
| Отношения к максимумам | 23/97,18,48,1 |
| Ключи 1/2/4 | 156/172/188,3,10,14 |
| Оружие `О:n` / `W:n` | 220,2,24,16 |
| Попытки и счёт `П:n  С:n` / `L:n  S:n` | 4,20,до144,8 |
| Полное сообщение | 4,28,224,8 |
| Номер страницы | 228,28,24,8 |

Реальное расстояние между непрозрачными пикселями иконки и первой буквой
составляет 2–3 пикселя для проверенных чисел. Шесть клеток вмещают каждый
signed WORD −32768…32767. При каждом изменении поле очищается целиком,
поэтому `100→9→0` и переходы знака не оставляют старых разрядов. У weapon
допустим весь bounded API диапазон0…8, хотя реальные предметы используют
меньший набор. Для key bit8 отдельная иконка не выдумана; bit8 не изображается
как один из обычных трёх ключей.
Полосы отношений ограничены0…48 пикселями; при неположительном максимуме
или неположительном значении они пусты. Это безопасное NDS-представление
крайних signed16 состояний, без деления на ноль; сами числа показываются
полностью и не заменяются полосой.

`dok_ui_paginate` сохраняет каждый печатный CP866-байт до первого NUL/`$`.
CR потребляется, LF начинает следующую строку. Целые слова переносятся при
возможности, длинные разбиваются. Пробелы не удаляются. HUD имеет28 колонок
и одну строку на страницу; сообщения образцов требуют не более двух страниц.
Общий paginator также принимает30×9 для будущих заставок N21.

## Runtime, память и язык

`platform/ui/runtime.c` принимает только представление состояния. При подборе
сообщение разрешается из RU object reference через локализованный field+58;
смерть врага — field+BE. Двери используют семь исходных строк каждого языка.
Числа приходят из живого N14-инвентаря, без локализованной копии mutable мира.
Жизни являются отдельным runtime WORD actor+D6: это **не** одно из
`native_66_70[]`. В текущем shell оно начинается с3; управление смертью,
повторной попыткой и переходами должно передать сюда фактическое значение.

В world loader всегда сохраняется EN-модель в `world.base`, тогда как
`world.effective` и `world.initial` остаются RU и принятым NDS-профилем.
`nds_ui_language` заново связывает только отдельный `ui.presentation`
descriptor с RU/EN, сбрасывает страницу сообщения и помечает HUD изменённым.
`world.presentation` остаётся связанным с языком загруженного sprite bank:
AI/combat/effect animation owners продолжают использовать прежние resource IDs.
Мир, epoch, serial, инвентарь, события и анимации не пересоздаются. До/после
связывания считаются SHA-256 всей `world.initial` и live `world.presentation`;
равенство обеих пар дополнительно проверяется. Ранний live-прогон обнаружил
ошибку смены общего resource-ID descriptor при неизменённом sprite bank;
исправление разделило UI и simulation descriptor, сохранив живую симуляцию.
Оба font/icon banks побайтово совпали в исходных RU/EN, поэтому достаточно
одной проверенной8192-байтной art bank; локализованные строки остаются разными.

`NdsUiRuntime` находится в свободном хвосте131072-байтного I/O региона после
выровненного `ProbeIO`. Постоянный9728-байтный HUD mirror находится после
выровненного `sizeof(dok_render_scene)` в его262144-байтном регионе. Обе
границы защищены `_Static_assert`. SDK heap и reserve не занимают UI-буферы.
Стек и новые heap-allocations для UI не добавлены.

Загрузка `fat:/dok/ui.dat` происходит до игрового цикла; header и SHA bank
проверяются. Формат и SHA описаны в evidence. Исходный payload не включается
в ROM или Git. При изменении HUD CPU строит9728 индексных байт в mirror,
сравнивает строки с VRAM и DMA передаёт только изменённые halfword runs.
После публикации `memcmp` VRAM/mirror должен совпасть. Main BG-палитра
при обновлении UI не записывается; только читается в отдельный snapshot для
независимого oracle. Оружие продолжает использовать строки0…153 той же
VRAM E bank, HUD —154…191. 3D texture/palette VRAM не меняется.

Observer publication охватывает весь срок изменения borrowed mirror и palette:
sequence становится odd **до** первой записи, затем mirror/palette и все
поля probe сбрасываются из cache при odd, и только после этого публикуется
even отдельным flush. Это существенно для read-only libretro observer,
который может остановить ARM9 посреди обновления. Ранний диагностический
прогон выявил чтение нового разряда оружия с предыдущим metadata; исправлен
срок seqlock, числовое поле и пользовательское представление не менялись.

Уведомления сохраняют все исходные байты, но длительность — явная адаптация
NDS:20054 native ticks, примерно2 секунды на страницу, без паузы мира.
Многостраничная строка показывает каждую страницу до исчезновения; новое
уведомление заменяет предыдущее. Время берётся из source clock callbacks,
поэтому пауза останавливает и сообщение. Это не выдаётся за исходную DOS
схему: та использует `C2<<2` и countdown по вызовам HUD, включая отдельно
доказанный sticky переход0→−1. Намеренное исправление читабельности
сохранено отдельно от восстановления x86.

## Диагностика и воспроизведение

`Makefile.n19` включает `GAMEPLAY_UI=1` и уже принятый N18 audio ARM7.
Старые N18 Makefile, ROM, отчёты и evidence не переписываются. Прежние
`main.c`, `gameplay.c`, `gameplay.h` сохранены до правки в
`research/evidence/nds/n19/initial/`; изменения включаются отдельным флагом.
У GBA/SDL2 нет изменений.

`check_hud.py prepare` сохраняет59 прежних N18 cases и добавляет по одному
source_kind28 на уровень. Это явные display fixtures поверх живого мира:
восемь наборов signed16, все ключи, weapon8; затем каждая исходная строка,
каждая её страница, сначала RU и затем EN. Один stage держится четыре
presentations. Поля N14-инвентаря не подменяются синтетическими числами.
Обычные X/Use и прочие gameplay inputs не переназначены.

`g_nds_ui_probe[64]` — read-only observer ABI1, sequence odd/even:

| Индекс | Значение |
|---|---|
| 0…2 | magic19504B44, version1, sequence |
| 3…8 | language, level, world epoch, serial, bank address, HUD VRAM address |
| 9…16 | HP, charge, max HP, max charge, lives, score, keys, weapon |
| 17…22 | message kind(0/1glyph/2native), ID, page, page count, start, deadline |
| 23…28 | updates, uploaded bytes, uploaded row runs, ticks, language changes, fault |
| 29…35 | stage, stage count, native field slot, source bytes, source pointer, frame, local stage |
| 36…43 / 44…51 | SHA-256 initial world до/после language bind |
| 52…55 | BG palette VRAM address, permanent HUD mirror address, VRAM equality count, actual palette snapshot address |
| 56…57 | RAM pointers на32-byte SHA live simulation descriptor до/после UI-language bind, опубликованные внутри того же seqlock |

Signed16 значения в probe sign-extend до signed32/u32 wire words.
Numeric fixtures используют `limits={100,9,0,-32768,32767,-1,1,10}`:
HP/lives=`limits[k]`, charge/score=`limits[7-k]`, maxima100/200, keys=k,
weapon=k кроме k7→8. На message stages используется k0.

Команды из корня, общий Python3.14.6 и закреплённый Wonderful/BlocksDS:

```powershell
$ndsPython='C:/Users/dspev/AppData/Local/Python/pythoncore-3.14-64/python.exe'
& $ndsPython -B -X utf8 ports/nds/tools/ui/extract_ui.py
& $ndsPython -B -X utf8 ports/nds/tools/ui/audit_ui_original.py
& $ndsPython -B -X utf8 ports/nds/tools/check_hud.py prepare
& $ndsPython -B -X utf8 ports/nds/tools/check_hud.py host
& $ndsPython -B -X utf8 ports/nds/tools/check_hud_host.py
& $ndsPython -B -X utf8 ports/nds/tools/check_presentation_alias.py
& $ndsPython -B -X utf8 ports/nds/tools/check_ui_language_runtime.py
& $ndsPython -B -X utf8 ports/nds/tools/check_hud.py final-release-ru
& $ndsPython -B -X utf8 ports/nds/tools/check_hud.py final-release-en
& $ndsPython -B -X utf8 ports/nds/tools/run_hud_emulator.py --target build/nds/n19/final-release-ru --language 1
& $ndsPython -B -X utf8 ports/nds/tools/run_hud_emulator.py --target build/nds/n19/final-release-en --language 2
```

Отдельный actual-runtime host gate выполняет настоящие `nds_ui_language`,
`nds_ui_notice` и `nds_ui_update`; SDK flush/DMA заменены только host stubs,
поэтому он не заявляет ARM9/DMA исполнение.35622 checks,6 сценариев
(3 уровня×2 simulation locale),18 RU→EN→RU binds,1596 text draws и4644
animation-player steps подтвердили неизменность всего мира, simulation
descriptor, sprite bank и metadata. Mutation control со старой ошибочной
записью UI descriptor в `world.presentation` отвергнут независимым full-world
сравнением именно при EN-switch; исходный SHA guard в этом control отключён.

Два коротких **реальных ARM9** прогона `smoke-ru`/`smoke-en` с отдельным
`scene-ui-smoke.dat` предъявили по665 стадий всех трёх уровней:6469120
INDEX8 bytes и столько же actual RGB pixels на запуск, пропусков нет.
Они сохраняются отдельно от полного62-case regression. Измеренный emulator
memory closure: startup516096+managed2293760+SDK204800+reserve655360=3670016 B.
UI не расходует remaining8192B SDK margin над обязательными196608.

Полные62-case RU/EN прогоны завершены functional PASS:872/869 наблюдаемых
HUD-состояний, все665 стадий, без пропусков. INDEX8 bytes и actual RGB pixels
сверены независимо по8482816/8453632 элементов;24 audio catalog checks на
запуск сохранены, FAT не изменён. Root visual-review просмотрел12 PNG
текущих ROM на native256×192, включая signed16 extrema и обе страницы
длинных RU/EN сообщений. Окончательные ROM по260608 B:

| Locale | SHA-256 |
|---|---|
| RU | `4c92439d1cfaae152c6bb97ec48e5dc627b59ab810d5a63143dd7edc0018b31d` |
| EN | `c8d972ef1fb227df039e32511cc074f2adfb9a0aa2e8296d04826acc972e8ed5` |

Совместный performance gate не пройден: normal history имеет один3VB
interval, p99=2 и minimum1s cadence29.422671289 emulator FPS (case14).
Live routes≤2VB, minimum29.913049144. `history.budget_passed=false` в обеих
локалях сохраняется явно; общий бюджет дорабатывается в N21P. Эти измерения
не являются аппаратными FPS DS/DS Lite и не отменяют пройденные HUD gates.

После финальных full-run и visual gates:

```powershell
& $ndsPython -B -X utf8 ports/nds/tools/archive_hud.py
& $ndsPython -B -X utf8 ports/nds/tools/summarize_hud.py
& $ndsPython -B -X utf8 tools/reports/summarize_reports.py
```

Численные/исходные, host/ARM9 и реальные эмуляторные проверки сохраняются
раздельно. Физическая DS/DS Lite, читаемость LCD, настоящий DMA/VBlank запас,
FPS и палитровые эффекты на железе остаются N24.
