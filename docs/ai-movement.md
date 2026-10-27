# Обнаружение и движение AI: G14

`src/ai.c`, `include/dok/ai.h` и `gba/ai_probe.c` восстанавливают обнаружение,
пробуждение и обычную ветвь движения class 9. Это редактируемый переносимый C.
Атака, реакция на урон и смерть остаются G15; контакт с героем передаётся через
`DOK_AI_CONTACT`, движение в этой ветви не выполняется.

## Источники и адреса

Оригинальные PKR.EXE/PKE.EXE из `analysis/inventory-{ru,en}.json`; SHA-256 до
и после анализа совпали. `scripts/analyze-ai.ps1` открывает сохранённые проекты
`research/ghidra/projects/{ru,en}/Kremlin.gpr` с `-readOnly -noanalysis`. `InspectAI.java`
сохраняет actor/world/math сегменты, инструкции, автоматический C, vtable и
manifest в `research/evidence/ai/{ru,en}/`. Автоматический C не компилируется и не входит
в поставку. Адреса ниже — Ghidra NE loader, не файловые смещения, индексы
NE-сегментов или runtime DPMI-селекторы.

| Назначение | PKR | PKE |
|---|---|---|
| Конструктор монстра | `1068:36B1` | `1020:3E14` |
| Пробуждение | `1068:3747` | `1020:3EB0` |
| Active callback | `1068:3845` | `1020:3FAE` |
| Getter изображения / проверка близости | `1068:3ECA` | `1020:4633` |
| Проверка course_counter | `1068:3B86` | `1020:42EF` |
| Приращение course_counter | `1068:3D2A` | `1020:4493` |
| Lookup впереди с изменением позиции | `1098:0309` | `1030:03ED` |
| Пробное движение с collision resolve | `1098:0E13` | `1030:0EF7` |
| Целочисленный выбор угла | `10C8:01C5` | `1058:01C5` |
| Построение тригонометрических таблиц | `10C8:0000` | `1058:0000` |

Getter принадлежит виртуальному слоту +8: конструктор устанавливает
`object+10 = 060D` в RU и `0616` в EN; сохранённые far pointers по
`1190:0615` / `10D0:061E` указывают на getter из таблицы. В RU renderer
`1098:4F7B…4FB3` берёт MMap glyph, world slot, проверяет class и вызывает
`CALLF [vtable+8]` **до** вычисления размеров billboard. TMap-путь аналогичен
`1098:3540…356D`. Следовательно, это обнаружение рассматриваемого рендером
объекта, а не глобальная проверка всех спящих врагов каждый тик.

## Правила и порядок

Спящий getter вычисляет `max(abs(hero.x-enemy.x), abs(hero.z-enemy.z))` и
пробуждает при **строгом** `< activation_distance(+BA)`. Луча видимости или
проверки стены в этой функции нет. Стена может исключить объект из обхода
рендера; это отдельное условие вызова getter.

Пробуждение ставит state 1, native nextcounter +50 = −1, запускает таймер,
обнуляет shot counter, копирует period +AE в course_counter +AC и назначает
первое событие через 416 native ticks. Compact displayed frame не равен
nextcounter: отображаемый кадр остаётся корректным до первого callback,
а следующий кадр хранится в `event.parameter` (после wake — 0).

Обычный callback сначала копирует позицию и смотрит на клетку впереди по
**старому** heading, на расстоянии `-cell_side`. MMap имеет приоритет. При
class 0 выполняется TMap lookup, причём тот же временный вектор сдвигается
ещё раз; это сохранённый эффект исходных `1098:0309` вызовов. Class 1
выбирает боевую ветвь: G14 сообщает CONTACT, оставляет живого owner и не
двигает актёра. G15 должен заменить этот временный 416-tick интервал
исходным циклом attack animation.

В ветви движения при `counter == period` счётчик сбрасывается. Пробный
heading направлен к герою. Из исходной позиции выполняется до десяти
collision-resolved полушагов клетки, пока пробная позиция не перейдёт в
другую клетку. Финальный heading берётся по вектору от реального актёра
до полученной пробной позиции. Собственный MMap glyph на время пробы
снимается и затем восстанавливается. Если равенства нет, счётчик
увеличивается как 16-битное слово и сохраняется прежний heading.
Таким образом, после сброса интервал равен N+1 проходам этой ветви.

После выбора курса публикуется следующий исходный animation frame,
затем вызывается исходная модель разгона `dok_motion_original` с командой
forward. Изменение G07 об отсутствии инерции применяется к герою;
AI использует восстановленные аккумуляторы. Collision G08 сохраняет
занятость, двери и скольжение. Следующее событие получает signed interval
кадра. Типы `IMUL`, `SAR`, `IDIV`, `LES`, `RETF`, caller cleanup и 16-битный
wrap счётчика проверены по листингу; far pointers заменены индексами.

Источник случайности в этих ветвях **отсутствует**. Повтор определяется
начальным runtime, последовательностью consideration IDs и native tick/input.
RNG/seed в AI не вводится. Таблица углов создаётся оригинальным math ctor:
`trunc(abs(sin/cos(degree*pi/180))*1000)`, отношение умножается на 100,
а диапазоны заполняются предыдущим градусом; отдельно устанавливается 45
для индекса 100. Это не точный `atan2` и не шкала 32768 таблицы движения.
`ai_atan.inc` содержит восстановленный результат этой процедуры. Файловый
`atan.bin` из Ghidra нулевой: runtime-таблица живёт в BSS, её нельзя брать
как готовые числа из EXE. Воспроизведение формулы — `table()` в
`scripts/verify_ai_original.py`.

## Runtime, GBA и границы G15

Scheduler использует 128 owners по `actor_index`, по одному AI owner на
исходный glyph. Object ID при dispatch получается через actor.glyph и
неизменную ROM glyph table. Это экономит 7680 байт относительно 256 owners;
обычный scheduler сохраняет порядок вставки, удаления и mutable-count skip.
Повторные клетки одного glyph не создают нескольких owners. RECLAIM из
реального combat sink снимает AI owner **до** замены slot, вместе с visual
owner. Runtime 33864/Object 36/player 60 не выросли. Коллизии используют
существующий borrowed combat overlay одного активного world; AI не заменяет
его и не сохраняет ссылку на временный world.

Production `gba_visual_poll` берёт предыдущую сцену `image_known`, очищаемую
renderer на каждом draw, и подаёт рассмотренные объекты в `dok_ai_consider`.
Это явная GBA sampling policy: getter вызывается по consideration lease в
simulation time, а не внутри draw. Поэтому wake может запаздывать на один
рассмотренный кадр; эквивалентность точного DOS render/event interleaving
не заявляется. Повтор с тем же lease/input воспроизводим. Остановка visual
пробы замораживает эту live цепочку; отдельный AI mailbox позволяет выполнять
точные callbacks при замороженном draw.

Combat WAKE будит без списания HP. DAMAGE/DEATH пока не заменяют hurt/death
AI; при нулевом HP G14 прекращает motion event. G15 обязан восстановить
боевые состояния и completion, не передавать их visual completion.
Защитная проверка compact collision не выпускает актёра за world margin;
это проверено во всех четырёх углах, включая диагностическую цель вне карты.
Native edge-clamp lookup для проб сохранён. Неподдерживаемые повреждённые
координаты с signed32 overflow не объявляются эквивалентными оригиналу.

Оригинальные параметры LEVEL1 RU/EN сохраняются в отдельных host worlds,
включая 20000/9000, 60/20 и N10/N24 у glyph 81/86. Их контрольные трассы
различаются. Общий release package намеренно использует согласованный
`gba-balance-proposal` для обоих языков; переключение языка не меняет механику.

## Проверки и воспроизведение

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build-gba-ai.ps1 -CrossChecks -OriginalEvidence
```

Host выполняет повторные трассы общего package и обоих исходных RES,
границу дистанции, wake без изменения HP, removal/reuse, четыре world corners
и настоящий combat RECLAIM→AI unlink. Linux musl static и Thumb
кросс-компилируются. Исходные x86 инструкции проверяют 1049 углов, 200
perception cases и 25 course cases **на язык**. Abs helper и wake boundary
перехватываются; FPU math ctor, scheduler closure и полный active callback
не объявляются исполненными целиком. Collision и motion дополнительно
опираются на прежние отдельные native probes G07/G08.

ARM проверяет proximity edges, повторные movement traces, заблокированное
движение, повторный glyph, живой consideration path и корректное изображение
до первого callback. ELF/ROM/map/PNG — `build/g14/`; результаты и память —
`analysis/g14-{ai-gba,host-verification,original-verification,review}.json`.
Полная DOS-сессия, физическая GBA, оригинальный BIOS и Linux/MSVC/CMake
execution не проверялись. Цель 30 FPS остаётся G20.


## Продолжение G15

Ветка CONTACT завершена в [ai-combat.md](ai-combat.md): attack cycles,
серии снарядов, hurt/death и corpse placement. Display frame по-прежнему
отделён от nextcounter. Scalar adapter +50 активного class9 возвращает unknown,
потому что o.frame не представляет native счётчик.
