# N09: ввод и граница UI

Независимое ядро ввода NDS, 29 октября 2026 года. Раскладка закреплена
[планом](../plan.md), формат одной выборки принят в
[N03](N03-contracts.md), шкала времени — в [N08](N08-time.md).
Это новая платформенная архитектура; GBA-код не использован. Сами тесты
нового буфера не доказывают эквивалентность DOS-клавиатуре или кинематике.
Восстановление движения и его источники проверяются отдельно в N09.

## Владение и шкала

Публичный API: [input.h](../../../ports/nds/include/dok/input.h), независимая
реализация: [core/input.c](../../../ports/nds/core/input.c). Strict C11,
без SDK, malloc, файлов и изменений игрового мира. Caller предоставляет
типизированный `dok_input_buffer` и владеет им на всём сроке использования.
Поля с `_` нужны для typed storage, не разрешают приложению менять внутреннее
состояние. API выполняется одним главным потоком, producer из IRQ не допускается.

N03 `dok_input_frame` остаётся одной исходной выборкой. Его `sequence` —
номер, а не время. N09 добавляет `dok_input_sample={frame,counter}`:
`counter` — монотонный uint64 счётчик платформы в counts, с descriptor N03/N08.
Он не является заворачивающимся uint32 native tick/deadline. Равные timestamps
допустимы: порядок сохраняется по строго возрастающему ненулевому sequence.
Числа counts не приводятся к uint32 и не сравниваются с native deadline.

Буфер хранит **64 полные выборки**, занимает **2624 байта** на проверенном host
и ARM ABI с 8-байтовым выравниванием uint64. Это bounded ёмкость NDS, не исходный
предел клавиатуры. При заполнении `capture` возвращает `NO_SPACE` и не меняет
буфер, предыдущий held, suppression или sequence. Перезаписи старых samples и
слияния нескольких press нет. Caller обязан сохранить отвергнутый sample для
повтора после drain либо явно остановить runtime с fault до следующего poll.

`capture` проверяет N03 shape, ненулевой sequence, его рост и отсутствие
уменьшения uint64 counter. Он также отвергает sample, оказавшийся раньше уже
обслуженного cutoff. Exact delta между held соседних кадров остаётся обязанностью
sampler: N03 shape допускает неполную историю, а новый буфер может присоединиться
к уже работающему sampler. Фильтр не выдумывает отсутствующих hardware edges.

`pop_through(cutoff)` возвращает не более одной выборки с `counter <= cutoff`.
Caller повторяет его до `available=0`, сохраняя порядок. Cutoff не уменьшается.
Empty/not-yet-due — успешная операция; output sample остаётся прежним.
При отказе output и буфер не изменяются. Нативные C-структуры не являются
файловым форматом, IPC или игровыми сохранениями.

## От выборок к командам

`dok_input_map` чисто переводит три physical masks независимо:
`native_held`, `native_pressed`, `native_released`. Select/Start передаются
отдельными `next_weapon_pressed` и `pause_pressed`. Opposite bits не гасятся:
оригинальный callback последовательно обрабатывает противоположные команды,
поэтому арифметический остаток после SAR нельзя заменять нулевым signed axis.

| DS | Native command |
|---|---|
| Y / A | TURN_POS `0x4` / TURN_NEG `0x8` |
| Up / Down | FORWARD `0x10` / BACKWARD `0x20` |
| Left / Right | STRAFE_LEFT `0x80000` / STRAFE_RIGHT `0x100000` |
| R | FIRE `0x200` |
| L | RUN `0x2000` |
| X или B | USE `0x800` |
| Select | следующий доступный weapon, отдельный edge |
| Start | pause/resume, отдельный edge |

Два Use эквивалентны по игровой команде. Raw mapping X+B в одной выборке даёт
один общий USE bit для каждой physical mask. Gameplay Use вызывается по release:
два отпускания в разных ordered samples сохраняют два действия. Никакого
неявного key repeat по числу кадров нет. Общий буфер не выполняет оружие,
взаимодействия или вызов движения.

Короткая пара press/release, захваченная двумя аппаратными polls до одного
логического service, остаётся двумя упорядоченными samples даже при последнем
held=0. Владелец gameplay service обязан переносить эти edges в явные pending
actions; чтение только последнего held теряет обещанную семантику. В N09
интеграция хранит pending press/release до native service, отдельно от этого буфера.
Presentation/render observations не опустошают его и не исполняют события.

Явный service latch реализован отдельно:
[controls.h](../../../ports/nds/include/dok/controls.h),
[controls.c](../../../ports/nds/core/controls.c). Он получает каждый filtered
sample строго по sequence, хранит uint32 counters press/release для12 кнопок
и отдельный counter Use. Один service потребляет не более одного edge каждой
кнопки и одного Use; held сохраняется, краткое движение/огонь/бег получает
один service pulse через `held|pending_press`. Это объявленная NDS-политика
сохранения коротких taps, а не утверждение, что DOS считывал клавиатуру таким же
способом. Counter overflow отвергает feed до любых записей. Release X+B одной
выборки даёт один pending Use, разные release samples — несколько services.
В native command маске service Use устанавливается только из потреблённого
pending release Use (`use_triggered`), press и удержание X/B его не вызывают.
Сырые `dok_input_map.native_held`
продолжают отражать физическое удержание; gameplay latch явно отделяет от него
исходный consuming break/release Use. По actual IRQ оригинала make устанавливает
state3, consuming reader принимает только state2 после break; это не press-edge
команда. Полные адреса и изолированное original execution — в доказательствах N09.
Held R по-прежнему даёт FIRE на каждом service.

## UI consume-until-release

`dok_input_ui_boundary` вызывается синхронно после последнего physical capture:

1. Удаляет все ещё ожидающие gameplay samples.
2. Блокирует все physical keys, удержанные в последней raw выборке.
3. Отменяет текущее touch и блокирует его до отпускания.

Следующий `capture` скрывает held/pressed/released заблокированной кнопки.
Её release тоже поглощается и только затем снимает её отдельный blocked bit.
Новая press после release проходит обычно. Другие новые кнопки не блокируются
автоматически. Во время UI владелец вызывает boundary после каждого raw poll
и при возврате в игру: очередь не заполняется длительностью меню, а кнопки,
зажатые в меню, не становятся выстрелом, Use или поворотом после возврата.
Владелец также очищает свои pending actions на той же границе: буфер ими не владеет.
Boundary не сбрасывает монотонную шкалу времени; world clock отдельно ставит
на паузу N08. Native tick паузы и SDK counter UI не взаимозаменяемы.

## Touch и нижний экран

Touch сохраняется в N03 frame в координатах нижнего экрана 256×192.
Он не участвует ни в одной native command mask, следовательно не создаёт yaw.
N09 передаёт данные будущему владельцу карты N20; распознавание drag/кнопок
масштаба, раскрытие карты и её отрисовка здесь не реализованы. Смена UI отменяет
gesture до release. Координаты release не имеют игрового смысла.

## Адаптер BlocksDS

[platform/input/input.h](../../../ports/nds/platform/input/input.h) и
[input.c](../../../ports/nds/platform/input/input.c) отделены от strict C11 core.
Caller сначала инициализирует продолжающийся N08 clock и передаёт его N03
services. Адаптер не перенастраивает timer2 и не вызывает `systemCounterSetup`.

Каждый poll выполняет `scanKeys`, `keysHeld`, при необходимости `touchRead`,
затем один `read_counter`. Собственный previous held формирует exact pressed
и released; sequence начинается с1 и не заворачивается. На первом poll уже
удержанные кнопки считаются pressed. SDK bit values всех12 кнопок проверяются
compile-time assertions. KEY_TOUCH передаётся отдельным touch, KEY_LID/DEBUG
исключены. `keysCurrent` не используется: установленный SDK помечает его unsafe
и deprecated. `keysDownRepeat` тоже не используется: его repeat зависит от
числа `scanKeys` и не соответствует временной модели N09.

Проверенный SDK — BlocksDS1.24.0-1, GCC16.2.0. Первичные локальные заголовки:
`C:/Users/dspev/.local/msys64/opt/wonderful/thirdparty/blocksds/core/libs/libnds/include/nds/arm9/input.h`
и `nds/input.h` той же установки. По SDK `scanKeys` сохраняет ARM7/ARM9 keypad
атомарно; адаптер использует именно этот интерфейс. Invalid touch shape,
обратный counter или исчерпание sequence возвращают явный отказ, сохраняя
adapter/output. Hardware уже мог быть опрошен: такой отказ требует fault,
а не продолжения с заявлением о сохранении всех физических transitions.
Тап целиком между двумя hardware polls программно восстановить невозможно.

## Проверки

Общий host/ARM9 suite:
[input_cases.c](../../../ports/nds/tests/input_cases.c),
[header](../../../ports/nds/tests/input_cases.h), host entry
[test_input.c](../../../ports/nds/tests/test_input.c).

На этапе реализации strict host suite выполнил **99038 checks**, failures0:
все4096 physical masks отдельно held/pressed/released, 1024 ordered samples,
16 полных оборотов ring, explicit overflow с неизменностью объекта,
все4096 UI suppression masks, раздельное снятие blocked keys, 256 UI polls,
touch cancellation/re-press, пять интеграционных buffer→controls случаев,
equal timestamps, будущий cutoff, uint64 max,
backward counter/sequence/cutoff и transactional refusals. Четыре крайних
touch-координаты не дали ни одной native команды. Интеграция проверяет все12
коротких press/release до service, три независимых Use services, очищение
pending перед UI, подавление held/release после UI и повторную press, а также
насыщение press/release/Use counters без частичного изменения latch. Удержание
X+B в течение девяти services не даёт Use, последующий общий release даёт
ровно один Use, а held FIRE/RUN/движение продолжают передаваться. Release ключей,
поглощённых UI, не даёт Use; новая press даёт только FIRE, новая release — Use1.
Непрерывное физическое
управление, общая интеграция движения, ARM9 run и итоговые SHA фиксируются
в [паспорте N09](../tasks/N09.md), а не считаются проверенными этим host suite.

Фактический локальный host repeat из корня репозитория, PowerShell:

```powershell
& 'C:/Users/dspev/AppData/Local/Programs/Zig/0.13.0/zig.exe' cc -std=c11 -pedantic-errors -Wall -Wextra -Werror -O2 -Iports/nds/include -Iports/nds/tests ports/nds/core/input.c ports/nds/core/controls.c ports/nds/core/contracts.c ports/nds/tests/input_cases.c ports/nds/tests/test_input.c -o build/nds/n09/host/test_input.exe
& build/nds/n09/host/test_input.exe
```

GNU11 SDK adapter отдельно собрался строгими `-Wall -Wextra -Werror` для
ARM946E-S; первый compile выявил enum-compare в assertions, исправлен явным
приведением SDK enum к uint32. Это исправление проверки типов, не изменение
раскладки. Реальная DS/DS Lite и итоговая совместимость microSD/loader остаются N24.
