# SP13: завершение программной сверки, 6 ноября 2026

Этот отчёт заменяет программные ограничения SP13 о непроверенном порядке
игровых sound events и неопределённой mixer/clock-границе. Аналоговый выход,
субъективное прослушивание и latency физического устройства принадлежат SP18.
Ни один приведённый PASS не означает прохождение всех карт человеком.

## Исходные доказательства и исполнение

Постоянные локальные материалы: `research/evidence/source-port/sp13-native-complete/`.
`ghidra/<ru,en,old-gold>/manifest.json` содержит образец, SHA-256 до/после,
диапазоны Ghidra MZ/NE, хеши байтов, Java exporter и воспроизводимого driver.
Оригиналы EXE/RES до/после всех матриц неизменны. Runtime-селектор DOS,
Ghidra-сегмент и файловое смещение не отождествляются.

`native_sound.py` исполняет исходные Sound wrappers, admission, SB start/stop,
DMA copy/refill и IRQ вплоть до конца PCM. Внешние границы: готовность DSP,
DOS установка/чтение interrupt vector и уведомление OG game clock о делителе.
Sound play/stop/admission/IRQ не заменены stubs. Проверяются far-return,
инструкционный бюджет, баланс стека и каждый фактический OUT/скопированный DMA байт.

| Вход | PKR | PKE | Old Gold |
|---|---|---|---|
| once | `1108:01f7` | `1060:173d` | `18a6:01f7` |
| loop | `1108:0232` | `1060:1778` | `18a6:022a` |
| stop / stop_all | `1108:026d / 0298` | `1060:17b3 / 17de` | `18a6:025d / 0280` |
| playing | `1108:02b9` | `1060:17ff` | `18a6:029d` |
| SB start / stop | `1118:0829 / 08a9` | `1070:083d / 08bd` | `2ad5:0510 / 0531` |
| IRQ | `1118:08bf` | `1070:08d3` | `2b41:0077` |

Sound identity доказана отдельным исполнением getter/constructor двух
разных bindings с одним RES key: новые wrappers по 0x24 байта, общий raw PCM,
независимые flags. Поэтому identity остаётся owner/binding, не RES key.
[Адреса, ASM и три исполняемых getter-проверки](sound-identity.md).
Освобождение/reuse projectile отдельно покрыты native SP11; отмена останавливает
старую identity до повторного использования слота.

## Игровые потребители: 10 004 сравнения, 0 расхождений

Все три датасета и все применимые семь LEVEL. Матрицы проверяют **не только
запрошенные callbacks**, но и допуск единственного голоса, вытеснение, отказ
при занятости, same-ID gate, режим loop и итоговую identity.

| Матрица | Cases | Фактически исполняемые оригинальные ветви |
|---|---:|---|
| `events-ai-report.json` + семь подробных файлов | 3 312 | wake, melee, ranged, hurt/death entry, последний death frame/stop; шесть initial voices: idle, attack, чужой loop, wake, death, hero hurt |
| `events-projectile-report.json` | 2 675 | actual class1/2/9 contact, player hit при HP 100/99/95/7/0, effect cancel; signed damage -32768/-1/0/32767; старый/чужой голос |
| `events-consumers-report.json` | 4 017 | open/close/auto и locked-use; pickup acknowledgement; full player fire + actual weapon readiness; generic health stats; death/retry/game-over; idle/same/foreign loop |

AI/geometry/player hit и projectile/contact/animation исполняются средствами
native стендов SP12/SP11, составленными с `NativeSound`: audio query возвращает
результат исходного backend, не постоянный ноль. У misc consumers внешними
входами остаются уведомления, часы/очередь и результат spawn-cost. Сам player
event handler, weapon readiness, stat arithmetic и порядок audio/stat исполняются.
Spawn-cost 0 моделирует подтверждённый SP11 отказ allocator при занятых слотах;
allocator и reclaim не выданы за повторно исполненные внутри этого fire fixture.
Pickup acknowledgement вызывается как принятый pickup; generic stat admission
и реальный C++ `player_pickup` проверяются отдельно. Callback двери использует
явный interval 7: адаптация original interval 0 к 30-фазному времени не скрыта.

В каждом batch проверяется количество строк до `zip`; native budget/return/stack
не может дать укороченный PASS. Сохраняются raw requests, адреса, владельцы,
resource key и before/after. Сравнение допуска нормализует атомарный
`priority_once` к source `get → stop_all → once`. Отброшенный запрос остаётся
в raw trace; он не объявлен воспроизведённым звуком. SDL fixture отдельно
проверяет сохранение **фазы**, когда same-ID priority не должен перезапускаться.

## Исправленные реальные расхождения

- OG wake, melee и ranged: исходный same-ID gate допускает preemption другого
  голоса через stop_all/once. Обычный `once` порта раньше терял эти звуки.
  `AudioAction::priority_once` добавлен в конец enum, старые значения сохранены;
  решение и запуск атомарны под SDL audio lock.
- OG pickup `1ae1:29f3`: тот же gate. Повторный pickup с уже активной его
  identity не должен перезапускать звук. Kremlin сохраняет stop_all/once.
- Hit audio принадлежит player-hit handler, а не generic stat changer.
  Generic pickup/surface/weapon-stat изменения здоровья больше не создают
  лишний hurt. `damage_player` сохраняет health==0 guard, signed delta и звук
  **до** изменения HP; положительные/нулевые signed результаты не отбрасываются.
- OG hit `1ae1:181f–1868`: при pre-hit HP, кратных пяти, JZ обходит **get** и
  безусловно выполняет stop_all/once. При остальных HP действует same-ID gate.
  RU `1068:1c72–1ca7` и PKE используют обычный once перед stat.
- Fire: source запрашивает once после spawn, **до** charge stat, включая
  cost==0. OG берёт **hero+a2** (`1ae1:17d1–17ee`), Kremlin — weapon+4c
  (`1068:1c29–1c54` / соответствующая PKE-ветвь). Порт раньше использовал
  weapon+4c, проверял cost и вызывал звук после stat. Сохраняется прежний bool
  `fire`: нулевая стоимость не объявляется успешным spawn.

Before witnesses: `events-ai-old-gold-before.json`, `events-wake-before-*.json`,
`before-monster-sound-hashes.json`, `events-consumers-before-all.json` и
`events-consumers-og-before.json`. Последний полный before batch содержит
также ложные отличия старого trace-нормализатора loop: они не объявлены
production bugs. Raw trace позволяет отделить их от actual wake/pickup/fire.
Все финальные матрицы используют исправленный before-loop trace и дают 0.

## Цифровой тракт: 56/56 полных PCM, 0 отличающихся samples

`check_native_sound_pcm.py` доводит все 56 исходных звуков до EOF через реальные
copy/refill/DMA/IRQ. Даже 474 361-byte Kremlin title исполняется полностью.
Сверяются каждый исходный byte, zero terminator OG, порядок DMA packets,
период из фактических native OUT и все S16 samples текущего port renderer.
Порт применяет равномерный логический clock и nearest hold без нормализации
амплитуды и без подбора DC по DOSBox-записи. Старые 48 wrapper policy / 42 rate
fixtures сохранены; этот проход заменяет прежний synthetic hardware-start stub.

SDL dummy probe: identity/foreign busy, same-ID priority phase, preemption,
pause/gain/snapshot/restore PASS для трёх профилей. Девять cross-rate conversions
22050/44100/96000 и три overflow rejects без изменения голоса также PASS.

## Реальный DOS output: waveform и точная граница времени

Новые `dos-matrix-report.json` и 56 raw WAV записаны исполняемыми оригиналами
под установленным DOSBox-Automation 0.85.1 (7567ec4), SB16, 48 kHz stereo,
filter/reverb/chorus/compressor off. Каждый WAV имеет SHA, non-silent check
и подтверждённый canonical original EXE process. Исходные EXE/RES неизменны.

Это **изолированный mixer input fixture**, не запись игрового порядка событий:
title binding подменён копией SVM нужного исходного звука. В Kremlin source PCM
повторён до исходной длины title, чтобы короткий effect пережил startup;
другие RES payload неизменны. OG input — точный исходный SVM. Для OG только
consumer call перед stop (`Ghidra 20c4:00f9`) временно заменён в RAM на
`ebfe909090`; исходные relocated пять байтов и runtime linear address записаны
в каждом `consumer_hold`, после capture восстановлены и повторно прочитаны.
**Sound/admission/IRQ/PIT/DMA код не менялся; вся RAM EXE неизменной не объявлена.**
Незавершённые/тихие ранние попытки явно отделены от matrix captures.

В `mixer-report.json` применён прежний порог: не менее восьми 100-ms окон,
4-kHz block averages, median NCC >=0,9. Все **56/56 PASS**. Минимальная median
NCC: OG 0,920399; PKR 0,968801; PKE 0,970411. Короткий periodic OG item
сначала дал 0,8767: coarse поиск одного пика выбирал неверную дробную фазу.
Новый алгоритм уточняет восемь лучших coarse фаз с точностью одного 48-kHz
sample, порог не менялся; raw waveform не менялась.

Bit-exact равенство WAV **не заявляется**. Измеренные fitted gain ranges:
OG 0,46294–0,49799, PKR 0,47391–0,50242, PKE 0,47667–0,50249;
median normalized RMS соответственно до 0,38636 / 0,24773 / 0,24146.
OG fitted DC 11425–12264 S16 units; это измерение эмитированного DOSBox output,
не основание произвольно масштабировать исходные 6-bit samples в игре.
Локальный phase-fit clock ratio на коротких повторяющихся звуках неоднозначен,
поэтому он не используется как единственное доказательство частоты.

Независимый `check_sound_transfer.py` подаёт staircase с известным sample count
через тот же **неизменённый** native SB code; `transfer-report.json` содержит
три дополнительные реальные WAV и точные cycle edges. Он отделяет clock от
NCC-фазового поиска. Для Kremlin nominal cycle 0,258048 s, измеренный median
0,258041667 s, residual -6,33…+14,50 µs, допуск два output samples (41,67 µs).
Для OG nominal 0,254091999 s, measured median 0,254583333 s; все cycle residual
-29,50…+9887,17 µs укладываются в явно принятые 15 ms. Эта jitter не добавляется
в равномерный SDL logical clock. Измеренные постоянные входные plateaus также
не дают постоянных соответствующих выходных уровней DOSBox: записанный
output transfer имеет transient/DC effects. Его fitted gain/DC не объявлены
линейной аппаратной моделью. Точные native DAC/DMA bytes и divisor совпадают;
измеренная временная/mixer-граница современной адаптации теперь конечна,
а не оставлена неопределённой будущей проверкой.

## Повторение

```powershell
py -3.14 -X utf8 -B ports/sdl2/tools/export_sound_events.py
py -3.14 -X utf8 -B ports/sdl2/tools/check_sound_identity_oracle.py
py -3.14 -X utf8 -B ports/sdl2/tools/check_native_sound_pcm.py
py -3.14 -X utf8 -B ports/sdl2/tools/build_sound_probes.py
py -3.14 -X utf8 -B ports/sdl2/tools/check_sound_event_ai.py
py -3.14 -X utf8 -B ports/sdl2/tools/check_sound_event_projectiles.py
py -3.14 -X utf8 -B ports/sdl2/tools/check_sound_event_consumers.py
py -3.14 -X utf8 -B ports/sdl2/tools/capture_sound_matrix.py
py -3.14 -X utf8 -B ports/sdl2/tools/check_sound_mixer.py
py -3.14 -X utf8 -B ports/sdl2/tools/check_sound_transfer.py
```

Новые probes: `sound_event_monster_probe.cpp`, `sound_sequence_probe.cpp`,
`sound_consumer_probe.cpp`. Они линкуются с текущим Release `libdok_core.a`;
не включают копий gameplay/audio algorithms. Full-source exports и WAV остаются
в локальном research; в Git — воспроизводимые scripts и этот компактный итог.
Общая финальная Release/CTest/package приёмка SP17 выполняется root после
явного audio production code-freeze; её результат не подменяется этим отчётом.
