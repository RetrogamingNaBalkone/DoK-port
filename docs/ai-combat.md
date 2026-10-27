# AI: атаки, урон и смерть (G15)

`src/ai.c` теперь исполняет все используемые состояния class 9: сон (0),
движение (1), контактная атака (2), реакция на удар (3), смерть (4).
`gba/ai_probe.c` соединяет этот модуль с общим GBA-ROM и `dok_combat`.

## Источники и адреса

Образцы: неизменённые PKR.EXE/PKE.EXE из `analysis/inventory-{ru,en}.json`.
Повторный `scripts/analyze-ai.ps1` открывает проекты
`research/ghidra/projects/{ru,en}/Kremlin.gpr` в режиме `-readOnly -noanalysis` и сохраняет
`actor.bin`, `evidence.asm`, непроверенный автоматический C и manifest
в `research/evidence/ai/{ru,en}/`. SHA-256 EXE до/после совпали. Автоматический C не
компилируется и не входит в движок. Исследование G15 использует тот же
воспроизводимый `InspectAI.java`, который экспортирует весь actor segment.

Все адреса в таблице — адреса **Ghidra NE loader**. Это не файловые смещения,
не индексы NE-сегментов и не runtime-селекторы DPMI.

| Ветвь | PKR | PKE |
|---|---|---|
| Active callback | `1068:3845` | `1020:3FAE` |
| Пробуждение, timer start, shot_count=0 | `1068:3747` | `1020:3EB0` |
| Принятие удара, SUB16/clamp, state=3 | `1068:37CF` | `1020:3F38` |
| Начало контактной атаки | `1068:3910` | `1020:4079` |
| Конец анимации → melee event 0x40000 | `1068:3966` | `1020:40CF` |
| Hurt при контакте: death frame 0, state=2 | `1068:39C2` | `1020:412B` |
| Hurt без контакта, затем обычная ветвь | `1068:39FE` | `1020:4167` |
| Timer extract, unsigned DIV 10000, strict compare | `1068:3AB4…3AD9` | `1020:421D…4242` |
| Лимит серии, front classes 8/9, sight | `1068:3ADD…3B0B` | `1020:4246…4274` |
| shot_count++, course=N, projectile glyph | `1068:3B0D…3B53` | `1020:4276…42BC` |
| Сброс timer и shot_count | `1068:3B58…3B7F` | `1020:42C1…42E8` |
| Начало смерти, state=4, nextcounter=0 | `1068:3D98` | `1020:4501` |
| Последовательные кадры смерти | `1068:3DD2…3E0D` | `1020:453B…4576` |
| Corpse placement и flags |= 0x10 | `1068:3E31…3E88` | `1020:459A…45F1` |
| Сообщение, sound_stop, снятие event owner | `1068:3E8D…3EC2` | `1020:45F6…462B` |
| Sight march с двумя plane lookups | `1068:1276` | `1020:1780` |

ABI оригинала: 16-битные signed words, far pointers `segment:offset`,
`RETF`, обычно caller cleanup. В листинге проверены `LES`, `MOVSX`,
`SHL EAX/SHRD EAX,EDX`, unsigned `DIV`, `JBE`, signed `JGE`, `INC word`,
передача event dword и порядок stack arguments. DOS vtables и far pointers
заменены индексами объектов/ресурсов. Compact Object36/player60/world33864
на ARM сохранены; новые поля монстра используют свободное место существующего
union, а не расширяют мир. Для активного class9 scalar range +50 неизвестен:
это native nextcounter, а compact o.frame — отображённый кадр. Настоящий
следующий индекс хранится в event.parameter; реконструировать +50 из display нельзя.
Старый G02 scalar adapter +B0/+B6 сохраняет startup constants DOS clock;
после wake эти bytes не представляют активные часы. Engine их не читает:
он использует compact burst_started. Native clock storage equivalence
не восстановлена и не заявляется.

## Подготовка, серии и снаряды

Wake ставит state=1, следующий кадр=0 (native counter=-1), запускает timer,
обнуляет shot_count и копирует course_period в course_counter. Первое AI
событие наступает через 416 ticks. Первый удар по спящему монстру только
пробуждает его, не отнимая здоровье, как проверено в G13/G14.

В обычной ветви монстр временно направлен к герою. Условие стрельбы:
`unsigned(elapsed)/10000 > uint32(sign_extend(burst_delay))`. Поэтому при
задержке 3 первый разрешённый момент — 40000 ticks, при 99 — 1000000.
Это ограничение начала серии; между выстрелами продолжаются интервалы
обычной анимации, отдельного фиксированного fire cooldown нет.

После достижения порога проверяются signed `shot_count < burst_limit`,
front class != 8/9 и видимость героя. При успехе shot_count увеличивается
**до** allocation, course_counter становится N, затем создаётся снаряд
по byte glyph +E7 (`3`/`4`, а не числам 3/4). Allocation failure не отменяет
потраченное место в серии. Если любая проверка не прошла, timer запускается
заново и shot_count обнуляется. При нулевом burst_limit монстр сохраняет
melee, но эта ветвь снарядов заблокирована.

Sight движется по 100 world units; MMap и TMap lookup сами продвигают
позицию, поэтому второй lookup идёт ещё на 100 units вперёд. Предел — 5000
итераций. Герой в MMap разрешает выстрел до flags-проверки; остальные объекты
должны иметь хотя бы один из bits 8/16. Собственная MMap-клетка временно
очищена, как native `1512(' ',0)`, затем восстановлена. Проверка не заменена
универсальным современным raycast. Dynamic MMap запрашивается через
`dok_combat_object_at`: сначала occupied bit, затем поиск владеющего клеткой
projectile slot только при установленном bit. Возвращается template object ID
для class/flags; это не instance slot ID. AI передаёт центр уже clamped
клетки, поэтому static/dynamic lookup используют одинаковую границу мира.
Реально опубликованный projectile даёт front class8 и сбрасывает серию;
его template flags8 разрешают sight march идти дальше, если он не стоит
непосредственно перед монстром. TMap lookup не меняется.

AI подключается через `dok_ai_connect_combat`; перед spawn синхронизирует
`combat.now=ai.now`. Поэтому первое projectile событие назначается на
`ai.now+416`, даже если production combat poll ещё не достиг этой simulation
фазы. AI, visual и projectile сохраняют свои кольца событий; точный глобальный
порядок всех owner original DOS не заявляется.

## Контакт, hurt и смерть

Front class1 останавливает движение. Переход 1→2 начинает attack animation
с кадра 0. При достижении конца native accessor возвращает NULL, тогда
монстр посылает герою event `0x40000` и сразу выбирает attack frame 0 заново.
Player handler для source.class !=8 вычитает **5 HP**; это не monster glyph
+5C и не урон его снаряда. HP героя ограничивается правилами stats G12.
Native не требует отдельной высоты попадания.

Combat DAMAGE ставит монстру state=3. При следующем контакте сначала
выполняется очередной attack step (включая melee, если достигнут конец),
затем показывается death frame0 и state становится 2. Attack nextcounter
сохраняется отдельно. Без контакта native сначала запрашивает death frame0,
но в том же callback обычная animation ветвь перезаписывает его текущим
walking frame; новый C сохраняет этот итог, не вводит дополнительной паузы.
При health<=0 следующая обработка всегда выбирает death, независимо от hurt.

Death начинается с кадра 0. Каждый кадр, включая последний, выдерживает
его signed interval. Cleanup происходит только на следующем callback после
последнего кадра. Последнее death image сохраняется. Native cleanup:

1. Visual height +19 становится 0.
2. MMap в текущей клетке очищается.
3. Если объект текущей TMap имеет class0, corpse glyph записывается в TMap;
   иначе corpse записывается в MMap, сохраняя исходную TMap.
4. Flags получают bit0x10. **Bit0x400 не ставится**, слот остаётся занят.
5. Выводится death_text, останавливается death sound, удаляется event owner.

Runtime хранит corpse marker; scalar adapter +19 и sprite renderer учитывают
нулевую высоту. Слот и последний кадр сохраняются; если кадр прозрачный, тело визуально
исчезает. В исходных БОЯР_У и МЫШЬ_У последний кадр4×3 полностью
прозрачен; у БАБА_У в нём остаются ненулевые пиксели. Corpse проходим
и не принимает projectile hits. MMap/TMap/FCMap/visited не очищаются целиком. RECLAIM по bit0x400
по-прежнему снимает owners до повторного использования слота; обычная смерть
не делает слот пригодным для projectile allocation.

## Уведомления и память

`dok_ai_notice` принимает WAKE/DAMAGE/RECLAIM; DEATH определяется по HP на
следующем callback. Sink получает WAKE, CONTACT, ATTACK, HURT, DYING, DEAD,
MELEE, RANGED и object ID. Ссылки attack/death sound остаются ROM bindings
+F0/+F4; death_text — native field +BE. G16/G17 могут разрешить их по ID.
DYING означает restart/play смерти; DEAD — сообщение/stop. CONTACT приходит
на каждом melee callback и позволяет G16 воспроизвести native
`if !is_playing: play`. Здесь звук и отображение реплики ещё не исполняются:
они принадлежат G16/G17. Игровой cleanup от этих consumers не зависит.

Combat collision overlay остаётся borrowed для одного активного world.
Нужно снять `dok_collision_overlay(0,0,0)` перед уничтожением context;
production context статический, host явно снимает callback. AI connect
тоже заимствует context и не создаёт собственного world/heap.

## Матрица покрытия

`tests/test_ai_combat.c` строит все исходные RU/EN LEVEL непосредственно из
RES и использует 14 групп `docs/enemy-tables.md`, включая различия EN.
Ресурсы анимаций выбираются из исходных bindings соответствующего языка.

| Группа | LEVEL, glyph hex | Сценарии в RU и EN |
|---|---|---|
| E01 | 1,80 | wake/melee/hurt/death/оба corpse planes/timer/series4/reset/LOS/projectile3 |
| E02 | 1,81 | те же, сохранены исходные параметры версии |
| E03 | 1,8C | те же, проверяется собственное исходное HP |
| E04 | 1,A0 | wake/melee/hurt/death/оба corpse planes/timer/zero burst |
| E05 | 1,A1 | те же |
| E06 | 2,80 | те же, исходная attack animation БОЯР_А2 |
| E07 | 2,A1 | те же |
| E08 | 2,A0 | полный сценарий с series4/reset/LOS/projectile4 |
| E09 | 3,80 | полный сценарий с series4/reset/LOS/projectile3 |
| E10 | 3,B3 | wake/melee/hurt/death/оба corpse planes/timer/zero burst |
| E11 | 3,A0 | полный сценарий с delay99/series4/reset/LOS/projectile4 |
| E12 | 3,C6 | полный сценарий с series4/reset/LOS/projectile4 |
| E13 | 3,D6 | полный сценарий с series4/reset/LOS/projectile3 |
| E14 | 3,A1 | wake/melee/hurt/death/оба corpse planes/timer/zero burst |

Host проверяет точную границу таймера, первый projectile deadline, каждый
смертельный кадр до cleanup, отсутствие active/alive owner после смерти, actual projectile front-class8
после spawn/poll/occupied publication (в том числе native edge-clamp),
сохранность corpse slot и +19=0. Это 28 сценариев; отдельный native probe
выполняет по языку 648 timer/burst/front/sight-result векторов, 6
melee/hurt control-flow векторов, 12 player damage векторов (class8/9,
health0/1, signed source damage), 4 corpse TMap класса. Внешние функции
sight/animation/sound/allocation/text/scheduler явно перехвачены, поэтому
результат доказывает эти исходные ветви, а не всю DOS-сессию.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build-native.ps1 -CrossChecks
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/analyze-ai.ps1
C:/Source/wasdify/.venv/Scripts/python.exe scripts/verify_ai_combat_original.py
C:/Source/wasdify/.venv/Scripts/python.exe scripts/verify_ai_combat_host.py
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build-gba.ps1 -RendererProbe -HalfColumns -OutDir build/g15
```

Host original SHA и source/artifact hashes: `analysis/g15-host-verification.json`.
Native evidence: `analysis/g15-original-verification.json`.
Общий ROM/ELF/map: `build/g15/`; ARM acceptance завершён root-проверкой:28 вариантовE01–E14×RU/EN.
Покадровые результаты, extra guards и stack — `analysis/g15-ai-combat-gba.json`;
итоговое связывание evidence/ROM/source — `analysis/g15-review.json`.

Неперенесённые native-подсистемы, не необходимые игровым переходам G15:
DOS sound-driver status/handle semantics, очередь и визуализация text UI,
деструкторы DOS far resources, скрытый stack ABI event closure builder.
Вместо них используются стабильные ID, sink и compact scheduler. Физическая
GBA и полная DOS-сессия не проверены. Linux static/Thumb скомпилированы,
не исполнялись. Язык общего ROM не меняет баланс/геометрию; original RU/EN
остаются отдельными эталонами. Полная 30 FPS игра остаётся G20.
