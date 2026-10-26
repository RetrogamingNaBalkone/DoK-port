# Передача контекста между чатами

## Текущая граница G17

Обновлено 26 октября 2026 года: **G00–G17 готовы, следующий ID — G18**.
См. [ui-localization.md](ui-localization.md), [tasks/G17.md](tasks/G17.md),
`analysis/g17-review.json`. `src/ui.c` и `gba/ui_backend.c` реализуют original
CP866 FONT8/FONT16, HUD, title/pause/help/statistics, очередь уведомлений
предметов+58, дверей и завершённой смерти монстров+BE. Сцена240×144/120колонок,
UI240wide, полоса сообщений240×16. HP/E — signed числа; W/L и Khex — в углах.
Ключи/максимумы/очки/карта объяснены на экране показателей.

Start=Use, Select=язык, **Start+Select=пауза** (прежний panic удалён).
Up/Down выбирают пункт, A/Start выполняют; B/Select закрывают паузу/подэкран.
Menu-closing held key блокируется до отпускания. Title Continue и возврат
через главное меню **сохраняют существующий мир**. G18 должен добавить
настоящий new game/reset и полный game flow; не считать их уже реализованными.
Пауза сохраняет ticks/remainder/deadlines без catch-up; hardware Timer2/3
и текущий PCM-голос продолжаются. Язык меняет только presentation view:
mutable world33864bytes, карты и общий balance сохраняются.

Frozen `build/g17/dok-diagnostic.gba`:32876132bytes,
SHA `b63ae8087b6f4cff5f121bd919b982ecbf750a2d786c581ae85166c2f7b7aefb`.
EWRAM248564/free13580; IWRAM20684/free3892 до8KiB stack reserve.
Старый HUD из IWRAM удалён, новый UI исполняется из ROM.
Main stack2360/margin5576, IRQ80/margin80. ROM32MiB remaining678300bytes.
Host UI и Linux static/Thumb compile прошли; fresh Ghidra RU/EN SHA совпали,
original x862048 glyph vectors/18 message-copy vectors прошли. Независимый
ARM проверил кнопки, pause/fraction/input, исходные FONT8/FONT16 pixels,
границы,4 страницы, real pickup+death+BE обоих языков. Root проверил PNG.
Renderer/interactions/time/28AI/23PCM regressions прошли; audio underruns0.
Команда `scripts/build-gba-ui.ps1 -CrossChecks -OriginalEvidence` (PowerShell);
pipeline выполнен отдельными шагами, весь driver одним вызовом не повторялся.

Legacy `Probe` явно пропускает title через UI mailbox; UIProbe использует
`preserve_title=True`. Прямые renderer/time/interactions harnesses адаптированы.
Renderer snapshot ждёт первый simulation step (startup parameter98:1→-1).
Idle message band теперь пустая; локализация текста проверяется отдельным UI
тестом. `DOK_PROBE_FAST_VIDEO=1` откладывает Python RGB conversion до PNG,
не отключая ARM rasterization/raw callback. UI mailbox/diag ABI — в тематическом
документе. `g_render_world`/object36/player60 и старые mailboxes сохранены.

Renderer regression4735552…5222592cycles;30FPS остаются G20.
Физическая GBA/оригинальный BIOS/целая DOS-сессия и Linux/MSVC/CMake execution
не проверены. Слуховая оценка относится к принятой записи G16, не новому
пользовательскому прослушиванию G17. Далее исторические передачи.

## Текущая граница G16

Обновлено 26 октября 2026 года: **G00–G16 готовы, следующий ID — G17**.
Спецификация — [gba-audio.md](gba-audio.md), результат — [tasks/G16.md](tasks/G16.md).
`src/audio.c` хранит один заимствованный PCM-голос; `gba/audio_backend.c`
стримит original unsigned PCM8 из ROM с signed conversion в два16-byte буфера.
DMA1/FIFO A и Timer0/1 обслуживают аудио; Timer2/3 сохраняют rational clock.
Renderer profiler теперь использует его квант64cycles. Present DMA3 ограничен
256word transfers: большой transfer блокировал C refill, хотя DMA1 продолжала
читать. Разрешение/число передаваемых пикселей не уменьшены.

Native mono admission/stop/query/global stop и loop latching восстановлены:
rejected same-object play меняет object flag, но не текущий hardware latch.
Sound36 выделяется отдельно от cached PCM; definition identity=(object<<16)|slot,
dynamic projectile identity=40000000|(slot+1), standalone test voice=80000000|PCM ID.
Нельзя заменять identity одним resource ID: другая дверь/clone с тем же PCM
не имеет права останавливать текущий звук. Source proof RU `1060:0145`,
`1068:2368`, EN bind `1018:1965`, clone`1020:2960`; complete fresh Ghidra
exports/manifests — `build/audio/{ru,en}/`, оригинальные SHA до/после совпали.

AI WAKE/default, CONTACT/RANGED/F0, DYING/globalstop+F4, DEAD/stopF4;
player damage/A6, shot/weapon default, projectile impact/removal, item accepted
force/default и door moving/loop/stop/locked consumers подключены. AI HURT звук
не придуман. Death text+BE и остальной UI — **G17**, hero death/game flow — **G18**.
Bindings разрешаются по текущему language/level. Смена языка сохраняет звучащий
ROM view; level reset останавливает голос. C=255/D=128 не объявлены volume/pan.

Frozen ROM `build/g16/dok-diagnostic.gba`,32867908bytes,
SHA-256 `99b4e21fabba12543bffa9217adf7e2a4d78655a45b7b0e57de1b0a0a022273e`.
EWRAM248136/free14008; IWRAM23980/free596 до8KiB stack reserve. ARM world33864,
object36/player60 сохранены. Voice/cursor/staging128bytes. IRQ stack80used/80margin,
main2360used/5576margin. Actual mGBA65536Hz stereo capture всех23 звуков:
whole correlation0.98793..0.999947, underruns0/81888DMA IRQ, max gap26368cycles,
C refill/rearm max1280cycles (не весь ISR). Long474361samples:43.028755s при
ожидании43.033209s, renderer123frames. Paired fixed-scene CPU elapsed overhead
14.48% для22050 nominal,6.77% для11025; framebuffer одинаков.

Пользователь прослушал actual short-loop excerpt и ответил «Звучит нормально»;
provenance/WAV SHA — `analysis/g16-listening.json`. Прослушанный файл сохранён
от ROM5f65... после DMA3 fix; последующие правки меняли identity, не PCM.
Это не physical GBA/Sound Blaster equivalence и не слуховой аудит всех23 звуков.

`analysis/g16-review.json` связывает source/ROM/ELF/map, host/native/ARM/listening
и renderer/interactions/time/28enemy regression отчёты. Все прошли на99b4... .
Команда: `powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build-gba-audio.ps1 -CrossChecks -OriginalEvidence`.
Части pipeline выполнены отдельно. Windows host исполнен; Linux static/Thumb
cross compiled, Linux/CMake/MSVC execution и физическая консоль не проверены.
Silent renderer regression4671616..4693696cycles; **30FPS остаются G20**.

Далее исторические передачи предыдущих границ; их указания «audio/UI не
исполняются» относятся к состоянию до G16.

## Текущая граница G15

Обновлено 26 октября 2026 года: **G00–G15 готовы, следующий ID — G16**.
`src/ai.c` и `gba/ai_probe.c` подключены к общему GBA-ROM: состояния0–4,
контактная attack animation, melee5 HP только после полного цикла, strict
burst timer, лимит серии/reset, native glyph3/4 projectile, hurt и death.
Подробные адреса/ABI/политики и матрица — [ai-combat.md](ai-combat.md).
Табличный release balance общий для языков; исходные RU/EN проверены отдельно.

Wake по consideration или первому sleeping hit сохраняет HP и создаёт owner.
DAMAGE ставит state3, callback при HP<=0 запускает death frame0. Каждый кадр,
включая последний, выдерживает свой interval; следующий callback завершает
смерть. Corpse получает visual height0 и flags|0x10 **без0x400**: slot и
последний кадр сохраняются, owner удалён, projectile hits проходят сквозь него.
БОЯР_У/МЫШЬ_У заканчиваются прозрачным кадром4×3; БАБА_У оставляет видимое
тело. Empty TMap class0 получает corpse в TMap, иначе corpse остаётся в MMap
при сохранённой TMap. FCMap/visited не удаляются. RECLAIM остаётся отдельным
переходом перед reuse flagged slots.

Native burst условие: unsigned elapsed/10000 строго больше signed delay,
shot_count<limit и front class не8/9, затем native 100-unit sight march.
Успешная попытка расходует место серии до allocation; неуспешные guards
перезапускают timer и shot_count=0. AI синхронизирует combat.now перед spawn,
первый projectile deadline=ai.now+416. Dynamic MMap front lookup использует
реальный combat.occupied и slot template ID; static/dynamic lookup смотрят
одну clamped клетку. Реальный летящий front projectile подавляет выстрел.

Scheduler AI по actor_index имеет128 owners. Display o.frame и native
nextcounter раздельны: event.parameter хранит следующий кадр; scalar+50
активного class9 теперь unknown. Старые scalar+B0/+B6 сохраняют G02
startup constants, не активный DOS clock; engine использует compact timer.
Native clock storage equivalence не заявляется. Object36/player60/ARM world33864 сохранены;
burst timer/count/corpse используют запас существующего union. AI connect и
collision overlay заимствуют context одного активного world. Перед выходом
context снять `dok_collision_overlay(0,0,0)`; production context статический.

Ghidra exports/manifests — `build/ai/{ru,en}/`, EXE SHA до/после совпали.
Native x86 на язык:648 burst,6 melee/hurt,12 player damage,4 corpse случая.
Перехвачены внешние sight/animation/sound/allocation/stats/text/owner boundaries;
полный callback с DOS scheduler closure и целая DOS-сессия не эмулировались.
Windows host прошёл14 исходных групп×RU/EN; Linux static/Thumb скомпилированы.
Root ARM проверил28 вариантов: wake, каждый melee/death frame и deadline,
hurt, strict timer, нулевые/полные серии/reset, projectile selection/deadline,
оба corpse planes, owner cleanup, wall/monster/front-projectile guards и
allocation failure. Renderer RU/EN/guards/reset/stack, interactions и rational
clock ARM-регрессии прошли; полный1455-camera sweep не повторялся.

ROM/ELF/map/PNG — `build/g15/`; ROM32864108 байт, SHA-256
`a7d0dc7d938a4e076ba8f2be0d863bdd56c1600e0e2dd712828cf7d5e3e2fa3f`.
EWRAM247872/free14272; IWRAM23980, до8KiB stack reserve596 байт.
ARM AI stack high-water2360/margin5576.
Static renderer4668642…4690694 cycles;30FPS не достигнуты, G20.

Команда: `powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build-gba-ai-combat.ps1 -CrossChecks -OriginalEvidence`.
Части выполнены отдельно; `analysis/g15-review.json` связывает source/ROM,
Ghidra evidence и host/ARM проверки. Физическая GBA/оригинальный BIOS,
полный DOS и Linux/CMake/MSVC execution не проверялись.

Следующий **G16** должен подключить звуковой consumer к existing AI events:
WAKE — idle sound; CONTACT/RANGED — attack sound с native is_playing guard;
DYING — restart death sound; DEAD — stop и death_text для будущего G17 UI.
Sound bindings +F0/+F4 и text field+BE разрешаются по object ID; DOS handles
не переносить. Сейчас эти уведомления доступны через sink, audio/UI не
исполняются, игровые transitions/cleanup от consumer не зависят. G17 — HUD,
G18 — game flow/hero death. Язык не меняет баланс/геометрию.

## Текущая граница G14

Обновлено 26 октября 2026 года: **G00–G14 готовы, следующий ID — G15**.
AI perception/motion подключён к общему GBA-ROM: `src/ai.c`,
`include/dok/ai.h`, `gba/ai_probe.c`. Getter class9 рассматриваемого renderer
объекта использует строгий Chebyshev-порог; глобальной проверки всех sleeping
монстров нет. GBA берёт consideration lease предыдущего draw в simulation
time. Точное DOS render/event interleaving не заявляется. RNG в ветвях нет:
повтор определяется runtime/ticks/input/lease. Курс использует native coarse
angle table (ctor scale1000), до10 collision-resolved проб полуклетки и
счётчик N+1. AI использует original acceleration, герой сохраняет исправление G07.

Спецификация/адреса/ABI/политики — [ai-movement.md](ai-movement.md).
Ghidra exports/manifests — `build/ai/{ru,en}/`, SHA EXE до/после совпали.
Оригинальные x86 probes исполнили на язык1049 углов,200 perception и25 course
cases. Полный active callback, FPU table ctor и scheduler closure целиком не
эмулировались; abs/wake boundaries перехватывались. Host original RES трассы
разные: RU `d5b9deed`, EN `97ed0ec7`; общий согласованный release profile
даёт `a20c2856` в обоих языках. Переключение языка не меняет механику.

Combat WAKE подключён: первый контакт sleeping сохраняет HP и создаёт AI
owner. RECLAIM снимает AI и visual owners до slot reuse. CONTACT с героем
останавливает обычную ветвь движения и передаёт seam G15; пока его interval416.
G15 должен восстановить attack/hurt/death, таймер серии, анимационный цикл и
completion; visual completion не заменяет игровые переходы. При health<=0
G14 прекращает motion event. Displayed frame остаётся валидным после wake;
native nextcounter хранится в scheduler event.parameter как следующий кадр.

Scheduler AI имеет128 owners по actor_index, object ID получается через
actor.glyph→ROM glyph_index. Повторный glyph не дублирует owner. Runtime33864,
Object36/player60 сохранены. Dynamic collision borrowed overlay по-прежнему
принадлежит combat одного активного world; перед уничтожением context нужен
`dok_collision_overlay(0,0,0)`. Host явно снимает overlay; production статичен.

Windows suites, Linux static/Thumb compilation прошли. Host проверил repeated
traces,4 world corners, CONTACT, owner removal/reuse и actual combat RECLAIM.
ARM проверил40 perception edges, deterministic/blocked/repeated-glyph motion,
живой consideration wake, valid displayed image до первого callback и actual
combat WAKE/RECLAIM. Renderer/язык/reset/guards/stack, interactions и rational
clock regressions прошли.1455-camera sweep не повторялся.

ROM/ELF/map/PNG — `build/g14/`; ROM32861724 байта, SHA-256
`9b4a9a28634fa5dbcb6ce15e74acf681c8bae7c91349c7da86f705d3b7e62926`.
EWRAM247800/free14344; IWRAM23980, до8KiB stack reserve596 байт.
Main stack2360/margin5576. Новые функции AI — ROM. Static renderer regression
4668231…4690291 cycles;30FPS не достигнуты, оптимизация G20.

Команда: `powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build-gba-ai.ps1 -CrossChecks -OriginalEvidence`.
Части выполнены отдельно; `analysis/g14-review.json` связывает source/ROM,
Ghidra evidence и проверки. Физическая GBA/оригинальный BIOS, полный DOS,
Linux/CMake/MSVC execution не проверялись. G15 не начиналась.

## Текущая граница G13

Обновлено 26 октября 2026 года: **G00–G13 готовы, следующий ID — G14**.
G14 не начиналась. Оружие и снаряды подключены к общему GBA-ROM:
`src/combat.c`, `include/dok/combat.h`, `gba/combat_probe.c`; A стреляет,
новое одновременное L+R запрашивает следующее имеющееся оружие. Первое
появление и pending hide/show идут по simulation ticks. Ненулевого заряда
достаточно даже ниже стоимости, расход 14/4 происходит только после spawn.
Полёт, same-class immunity, стена/дверь/цель, урон, decay, POPAL/MIMO и
удаление событий реализованы. Полёт начинает с остатка, затем проходит
четверти клетки; контакт ещё держит flight image, эффект начинает с frame 1.

Спецификация/адреса/ABI/политики — [combat.md](combat.md). Native slots
просматриваются 0…255, первый NULL или flags 0x400 побеждает. LEVEL1 имеет
141 свободный номер. Кольцо с одним событием на projectile сохраняет native
cursor/removal skip и отложенную публикацию owner, не позволяет повторно
занять текущий удаляемый слот внутри callback. Slot record 40 байт.
Dynamic occupancy — отдельные 512 байт, без записи dynamic slot glyph в
static MMap dictionary. Borrowed collision overlay учитывает четыре угла;
он блокирует actor movement и снимается при cleanup. Перед уничтожением
его context нужен `dok_collision_overlay(0,0,0)`, production context статический.
Reclaim уже снимает старый visual owner; будущий AI consumer должен снять
свои события. Два экземпляра одного шаблона используют разные texture slots
256…511, а не общий template frame. Native Object 36/player 60/ARM runtime
33864 не выросли; прежние compact резервные поля не переиспользовались.

Для G14/G15 доступны `dok_combat_spawn(owner, template_glyph)` и notifications
WAKE/DAMAGE/DEATH/RECLAIM. Sleeping monster state 0 будится без вычитания
health; consumer обязан выполнить AI transition. Active damage применяет
SUB16/clamp, но combat не меняет hurt/attack/death state и не подменяет AI
visual completion. Hero death — G18, звук/HUD consumers — G16/G17.

Свежие Ghidra C/ASM/actor.bin/world.bin и manifests:
`build/combat/{ru,en}/`. EXE SHA до/после совпали. Прошли 20 исходных x86
readiness-векторов, 72 slots cases, 2 position-copy slices, 2012 damage-decay
векторов, 336 monster arithmetic cases; полный native scheduler closure ABI
в spawn не эмулирован и не объявляется проверенным целиком. Windows suites
и Linux/Thumb compilation прошли. ARM проверил оба оружия RU/EN, empty/low
charge, full/refusal/reuse, closed/open door/wall/monster, wake/damage/death,
effect frame 1/end, отдельные кадры двух эффектов по source RES pixel oracle,
production occupancy/clear, первое pickup→show, hide/show, реальные A/L+R и
20 естественно завершающихся выстрелов без объектов/событий/occupancy leaks.
Renderer/reset/язык/guards/stack, interactions и rational clock прошли.
1455-camera sweep не повторялся; проверены targeted static и instance paths.

ROM/ELF/map/PNG — `build/g13/`; ROM 32858612 байт, SHA-256
`5039eb8c4189474259127390b26742b305f9e9ce7f354fa584ea6fe668a466be`.
EWRAM 239416/free 22728; IWRAM 23980, до 8 KiB stack reserve 596 байт.
Новые холодные функции оставлять в ROM. Main stack 2376/margin 5560.
Startup/static regression 4668074…4689700 cycles, synthetic combat capture
до 11891103 cycles. Цель 30 FPS остаётся недостигнутой; оптимизация G20.

Команда:
`powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build-gba-combat.ps1 -CrossChecks -OriginalEvidence`.
Части выполнены отдельно, aggregate `analysis/g13-review.json` прошёл и
связывает финальные source/ROM/evidence hashes. Физическая GBA/оригинальный
BIOS, полная DOS-сессия, Linux/CMake/MSVC execution не проверялись.
Следующие входы — паспорт G14, combat notifications, collision, scheduler,
enemy parameters и свежие exports. Ниже — история предыдущих границ.

## Предыдущая граница G12

Обновлено26 октября2026 года: **G00–G12 готовы, следующий ID — G13**.
G13 не начиналась. Предметы/stats/inventory подключены к общему GBA-ROM:
`src/items.c`, `include/dok/items.h`, production callback перед zone/movement
в `gba/movement_probe.c`. Все626 оригинальных размещений на язык (TMap),
11 определений на уровень,29 реально размещённых видов/уровней. Положительное
пополнение при полном ресурсе оставляет предмет; отрицательные эффекты, ADD16
overflow перед clamp, max-charge increase+refill, ключи, карта и оружие работают.
Consume меняет только одну клетку, shared object/остальные glyph сохраняются.
Notifications готовы для G16/G17; gameplay не выводит текст и не играет звук.

Спецификация/адреса/ABI/policies — [items-stats.md](items-stats.md).
Реальные числовые эффекты/позиции RU/EN совпали, переводы и масштабы сохранены
раздельно. Balance manifest не менялся. Map flag занимает прежний player
reserved word+54; native это world+0C. Object36/player60/ARM world33864 не выросли.
Первое оружие выбирается сразу; duplicate refill/pending сохраняет текущий
выбор, а transition hide/show должен реализовать G13 вместе с оружием/снарядами.
Исходный код этой цепочки — свежий `build/items/{ru,en}/` C/ASM/actor.bin.

Ghidra read-only exports и EXE SHA-256 до/после совпали.5274 actual x86 stat
vectors и16 native item callbacks (перехват только HUD/sound) прошли.
Windows suites и Linux static/ARM Thumb compilation прошли. mGBA109 stat
boundaries,58 real item cases RU/EN всех3уровней,12 duplicate weapon cases,
shared glyph/repeat/notifications/full refusal прошли. Два visible production
pickup по416-tick motion hook удаляют один billboard и меняют978 pixels каждый.
Renderer/язык/reset/guards/stack, doors/sensors/conveyor/FINISH и rational clock
regression прошли. FCMap class12 используется только для100/102/1000:
неиспользуемый DOK_INTERACTION_STAT остаётся диагностическим seam.

ROM/ELF/map/PNG — `build/g12/`; ROM32851924 байта, SHA-256
`1a49c7694cc3ca1fbf7fa1439c7597f173c4c74224f4611c71489fd1a4884850`.
EWRAM218416/free43728; IWRAM23684, до8KiB stack reserve892 байта.
Новые cold functions в ROM. Main stack2376/margin5560 в renderer/reset;
startup4653188…4673215 cycles. Цель30FPS не достигнута, оптимизация G20.
Renderer не менялся;1455-camera sweep G11 не повторялся, targeted regression
достаточна для pickup integration. Visual clock/crop/scheduler G11 сохранены.

Команда: `powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build-gba-items.ps1 -CrossChecks -OriginalEvidence`.
Части реально выполнены отдельно, финальный `scripts/report_items.py` прошёл.
`analysis/g12-review.json` связывает source/ROM/evidence hashes.
Физическая GBA/original BIOS, полная DOS-сессия, Linux execution и CMake/MSVC
execution не проверялись. Следующие входы — паспортG13, inventory API,
weapon-parameters, positioned instances и visual playback G11.
Ниже — история предыдущих границ; она не отменяет текущий результат G12.

Уточнение плана 26 октября 2026 года: по указанию пользователя удалена G19
и её паспорт — сохранение/загрузка игры не входят в объём порта. Пользователь
указал, что оригинал их не поддерживал; прежнее требование не было обосновано
исследованием. Убраны связанные требования G17, G18, G20 и G21; G20 теперь
зависит от G18. Остальные ID сохранены. В этом уточнении менялась только
документация; актуальная граница реализации приведена ниже.

Обновлено 26 октября 2026 года: G00–G11 готовы по уточнённым критериям.

## Текущая граница G11

**Следующая задача — G12.** Спрайты предметов/врагов, software billboard,
перекрытия и визуальный scheduler включены в общий GBA-ROM. Есть16 positioned
instances для изображений снарядов/эффектов; их создание/движение/урон — G13.
Логика здоровья, атаки, смерти и удаления не заменяется completion анимации.
Движение без поступательной инерции, двери/сенсоры/conveyor/FINISH сохранены.

Спецификация/адреса/ABI/границы: [sprite-rendering.md](sprite-rendering.md).
Новые `src/renderer_sprites.inc`, `src/visual.c`, `include/dok/visual.h`;
renderer/runtime header, ROM source loader, GBA motion/render adapters,
tools/build/CMake и static-regression freeze обновлены. `visual_resource`
занимает прежний резерв **compact** word+14, Object36/ARM runtime33864
не изменились. Native+14 имеет другое значение — адресацию не смешивать.
Текущий crop class12/13 хранится отдельно под known bit8 в compact animation_a;
native counter+50 увеличивается только после конфигурации старого crop.
Known animation A bit1 этим не устанавливается; crop не выдаётся за native+20.

Visual production clock333/333/334 догоняет simulation ticks через416-tick
motion catch-up, с сохранением долга. Видимость для lease берётся из последней
рассмотренной сцены и сбрасывается при переходе уровня. Interval0 — один шаг
за visual quantum; положительные интервалы округляются до следующего quantum.
Raw scheduler сохраняет signed interval/unsigned deadline semantics, включая
−31072. `dok_visual_play` выбирает binding slot, публикует frame0, затем
loop или one-shot completion; native gameplay state не меняется.

Ghidra EXE hashes до/после совпали; проекты/логи/экспорты:
`build/sprites/{ru,en}/`, скрипты `InspectSprites.java`/`analyze-sprites.ps1`.
900 оригинальных x86 billboard-векторов прошли в Unicorn. Host1750
последовательностей/9714 deadline переходов/322 прохода отрицательных
интервалов,9 coarse/fine timelines и точные water crop/end/rearm прошли.
20 item/enemy faces и8 projectile projections сверены с независимым oracle
по исходным RES; ближние masked pixels и полная дальняя occlusion проверены.
mGBA:8 playback-последовательностей RU/EN,24 projectile views, видимые pixels,
automatic multi-step per draw, exact water crop-before-INC/end, guards/stack,
embedded package, язык/poison reset прошли. Все1455 камер без отказов;
door/sensor/conveyor/FINISH и rational time regression прошли. Static suites
замораживают visual timeline явно; её ход проверяет отдельный ARM-тест.

ROM/ELF/map/PNG — `build/g11/`. ROM32 850 956 байт, SHA-256
`231069467bf90b2021956ce0d5a00552424019b1dd3c2721dbce785236d3e493`.
EWRAM218 348, свободно43 796; IWRAM23 684, до8KiB stack reserve892 байта.
Новые функции размещать и измерять осознанно. Main stack2376, нетронутый
запас5560 в измеренных ARM-сценариях. ROM slack703 476 байт не резерв всей игры.
Startup4 651 246…4 668 836, worst camera6 251 069 cycles: цель30FPS не достигнута.

Команда: `powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build-gba-sprites.ps1 -CrossChecks -OriginalEvidence`.
Части выполнены отдельно; финальный sweep —1455 камер. Acceptance
`analysis/g11-review.json` связывает текущие source/ROM/evidence hashes,
остальные отчёты `analysis/g11-*.json`. Windows suites прошли; CrossChecks
скомпилированы. Физическая GBA/оригинальный BIOS, полная DOS-сессия,
Linux execution и CMake/MSVC execution не проверялись. Ниже — история G10
и предыдущих границ, её ограничения не заменяют текущий результат G11.

## Предыдущая граница G10

**Следующая задача — G11.** G10 подключил текстурные пол/потолок, перепады
потолка, LIGHT16 и три GAMEP к общей сцене G09. Движение без поступательной
инерции, коллизии, двери, сенсоры, conveyor и FINISH сохранены. Визуальный
sampler поверхности не запускает игровое событие.

Спецификация/ABI/адреса/границы: [surfaces-lighting.md](surfaces-lighting.md).
Новые hand-written `src/renderer_{surfaces,surface_raster,surface_sides}.inc`
включены в `src/renderer.c`; обновлены renderer header, ROM linker,
GBA startup light-cases, `tools/render_tool.c`, пакер/проверка row0-bank.
FCMap+54 — отступ потолка, floorY=−H/ceilingY=I−961. Отрицательный I
даёт один текстурированный пол: имя solid из G04 было предварительным.
Near=1920 подтверждён в G00 RAM обоих языков; RUS DS:0DFA/ENG DS:0EA2,
не универсальный +B0. Незакрытый фон теперь0, цветная диагностика G05 удалена.

Оригинальные инструкции: `InspectSurfaces.java`, `analyze-surfaces.ps1`,
`build/surfaces/{ru,en}/`, прежние Ghidra-проекты сохранены; SHA EXE до/после
совпали. На язык2065 quads/8260 вершин,6 quads с negative depth,42 synthetic
single-plane,10610 light-векторов C/native,144 crop-вектора прошли.
Constructor array-helper перехвачен; проекция и crop — нет. Не полный DOS-session.
Инвентарь всех шести FCMap и bindings — `analysis/g10-original-verification.json`.

Все1455 камер LEVEL1–3, HUD/палитра,443520 strips/linked payloads, time,
door/sensor/conveyor/finish регрессия прошли.32 ARM-вида воды в RU/EN,
четыре направления и фазы0/1/9/17: по32736 scene pixels совпали с host;
512 байт palette RAM сверены с каждым из шести исходных GAMEP.
13 Windows suites, Linux static/Thumb compile и общие ARM cases прошли.
Guards/poisoned reset и сохранность mutable world при языке проверены.

**Автоматический scheduler анимаций ещё G11.** В G10 sampler принимает
`object.frame`0…17, при обычном запуске вода остаётся в начальной фазе;
тест подаёт фазы вручную. Сдвиги делят размер на18 до умножения. Не привязывать
скорость анимаций к рисованию. Dynamic light near/боевые палитровые эффекты
не выдаются за готовую игровую механику. Полная попиксельная эквивалентность
DOS raster не заявлена: GBA clip/общие рёбра/front-to-back — политика порта.

ROM/ELF/map/PNG — `build/g10/`; ROM32 846 628 байт, SHA
`e9598753692371226d68213e135a31f7fb74f42a21ebd7a52d3587797fb23992`.
EWRAM202 556 (+58 884); IWRAM code22 888/data1232 — **456 байт до stack
reserve**, новые холодные функции размещать осознанно. Main stack2352,
нетронутый запас5584. ROM slack707 804 байта не резерв всей игры.
Startup4 486 080, worst6 104 796 cycles (LEVEL1 near-x, heading240).
Стоимость существенно выше G09: ранний бюджет400000 остаётся отложенным,
30FPS полной игры не подтверждены; новые визуальные упрощения не вводились.
Физическая GBA/оригинальный BIOS, новый полный DOS-session, Linux execution
и CMake/MSVC не проверялись.

Команда: `powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build-gba-surfaces.ps1 -CrossChecks`.
`-OriginalEvidence` добавляет Ghidra/Unicorn; общая библиотека Unicorn read-only
из `C:/Source/wasdify/build/research-deps`. Части команды выполнены отдельно,
итоговый полный sweep —1455 камер. `report_surfaces.py` связал ROM/исходники/
evidence в `analysis/g10-review.json`; остальные отчёты — `analysis/g10-*.json`.
G11 в этом проходе не начинался. Далее ниже — исторические границы G09 и ранее.

## Текущая граница G09

**Следующая задача — G10.** G09 завершён: Start/Use открывает двери в общей
GBA-сцене с коллизиями. Замки проверяют любой совпавший бит ключа; ключи пока
выдаются только тестовым сценарием, подбор предметов — G12. Door scheduler,
горизонтальный/вертикальный crop, проходимость, повторные активации, занятость
и общие объекты по glyph подключены. Инерция G07 не возвращалась.

Спецификация, ABI, исходные адреса и ограничения: [doors-triggers.md](doors-triggers.md).
Новые `include/dok/interactions.h`, `src/interactions.c`; direct displacement
в collision для class12. `runtime.door.extent` использует прежний padding,
Object36/union20/раскладка maps не изменились; пакет ресурсов пересобран.
Class6 действительно имеет базовый no-op Use, рабочий toggle ему не выдуман.
Class13: LEVEL2 `f`→`h`, LEVEL3 `f`→`k`, event0x800, sensor обходит замок.
Повторный glyph в соседней/другой клетке — тот же объект и не новый enter edge.
Class12: conveyor100/102 двигает X/Z с коллизиями; category1000 отдаёт явный
FINISH при score==world total. G18 должен потребить событие; health=−1 и
смена уровня здесь намеренно не эмулируются. Прочие stat events — seam для G12.

Native door берёт число фаз из измеренного FPS (DS:C2), callbacks interval0
зависят от опроса. Явная политика порта: 30 фаз по сетке333,333,334 ticks,
независимо от рисования; герой/зоны —416 ticks. Delay авто-двери применяется
дважды, занятость повторно проверяется через тот же delay. Crop делит размер
до умножения на phase, поэтому открытая дверь может оставлять тонкую полоску.
Текущий renderer поддерживает два реально используемых door crop-режима;
полная анимация/billboards остаются G11, сообщения/звуки — G16/G17.

Ghidra-проекты/журналы/ASM/bytes: `build/interactions/{ru,en}/`,
`InspectInteractions.java`, `analyze-interactions.ps1`. SHA EXE до/после совпали.
На язык6000 Door/base-векторов,622 sensor routes,120 crop-векторов Unicorn
совпали с C/формулой. Sound/UI/scheduler перехвачены; crop без перехвата callees.
Это не полный DOS-session. Инвентарь всех шести исходных LEVEL —
`analysis/g09-original-level-inventory.json`; runtime-сценарии обеих локализаций:
108 door definitions,30 class6,20 sensor-клеток,2006 class12-клеток.

13 Windows suites, Linux static/Thumb compile прошли. Общий suite исполнен
на ARM. mGBA: RU/EN lock/crop/open, проход и occupancy/отложенное закрытие,
реальный Start, sensor LEVEL2/3 и повторный вход, conveyor четырёх направлений,
finish-event, смена языка при активной двери, guards/poisoned reset.
12 динамических кадров по32736 scene pixels совпали с host; все1455 камер,
HUD/палитра,443520 strips/linked payloads и time regression прошли.
Физическая GBA, оригинальный BIOS, Linux execution и CMake/MSVC не проверялись.

ROM/ELF/map/PNG: `build/g09/`; ROM32 838 040 байт, SHA
`defb27a26a0d9aff5a36444d45efb4f90b7342fa0bd0933e30fb91946b3746dc`.
EWRAM143 672 (+14 444 к G08), IWRAM code17 292/data1232,
main stack2352/запас5584. Startup449 760, worst статическая камера624 719 cycles.
Live/test-batch измерения разделены в `analysis/g09-review.json`.
Бюджет400 000 остаётся отложенным,30FPS полной игры не подтверждены;
остаток ROM716 392 байта не резерв всей игры.

Команда воспроизведения:
`powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build-gba-interactions.ps1 -CrossChecks`.
`-OriginalEvidence` добавляет Ghidra/Unicorn; библиотека read-only из общего
`C:/Source/wasdify/build/research-deps`. Все части команды выполнены отдельно;
итог `scripts/report_interactions.py` связал текущие исходники/ROM/evidence.
Новые тесты/probes/inventory/build/report-скрипты перечислены в паспорте G09.
G10 в этом проходе не начинался.

## Граница G08 до взаимодействий

**Следующая задача — G09.** G08 выполнен в своих границах; предыдущие разделы
ниже — история этапов. В общей GBA-сцене LEVEL1 герой ходит с коллизиями.
Исправление G07 без поступательной инерции сохранено, разгон поворота сохранён.
Модуль `include/dok/collision.h`, `src/collision.c`; подключён callback
`dok_collision_move` вместо free-flight в `gba/movement_probe.c`.

Правила, native ABI и защитные отличия: [collision.md](collision.md).
В обоих EXE квадратный footprint Min Dis238, Cell Side480; четыре угла
MMap, затем TMap; разрешает flags bit0x10 по object+0x15. FCMap/y/bounds
в этом пути не участвуют. Сохранены скольжение и необычная перезапись Z-center
в RUS0A90…0AD7, три SAR2-подшага, удаление/запись glyph MMap и door+0x56.
Адреса Ghidra: RUS1098:0448/0901/0E13,1068:0EB1;
ENG1030:052C/09E5/0EF7,1020:13BB; это не DPMI-селекторы/файловые offsets.
Подмена far pointers и границы арифметики явно описаны в collision.md.

Двери пока закрыты и блокируют клетку; события/ключи/открывание — G09.
Невидимые пока монстры блокируют по исходным flags (billboards — G11).
Неизвестный glyph закрыт; безопасный adapter отклоняет выход footprint за
мир, слишком большой подшаг, стирание чужого glyph и занятый итог resolver.
Это намеренная защита порта, не утверждение о точных инструкциях оригинала.
Кнопки и416-tick fixed steps из G07 сохранены, Select меняет только язык.

Ghidra-проекты/журналы/ASM/байты: `build/collision/{ru,en}/`, скрипты
`InspectCollision.java`, `analyze-collision.ps1`, `verify_collision.py`.
SHA оригинальных EXE до/после совпали. 55 152 x86-проверки на исходных
LEVEL1–3 каждой версии,1200 native-обёрток с тремя подшагами: позы/MMap/door
counters совпали. Wrapper far world-move соединяет два контролируемых CPU;
footprint/resolver исполняются без перехвата callees. Это не полный DOS-session.

12 Windows suites, Linux static/Thumb compile прошли; Linux execution и
CMake/MSVC не запускались. Общий collision suite исполнен на ARM: углы,
узкие проходы, границы, MMap/TMap/FCMap, дверь, safety. mGBA:972 fixed steps
в RU и EN на общей G03-карте с одинаковыми полями/картами, реальные кнопки,
удержание у стен, остановка без дрейфа, сохранение mutable world при языке,
guards/poisoned reset. Все1455 камер, HUD/палитра,443 520 strips/linked payloads
и G06 time regression прошли. Физическая GBA/оригинальный BIOS не проверялись.

ROM/ELF/map/контрольные кадры — `build/g08/`; ROM32 833 692 байта, SHA
`7e643146b765003bc90778f00fe78b7a1cf88ee5972075462f7a997cd1a64a64`.
EWRAM129 228 (+4 к G07), IWRAM code16 852/data1232, main stack2352/запас5584.
Startup443 062, worst статическая камера619 952 cycles (LEVEL2 offset60°);
живой motion/input/camera до37 959, CPU-кадр286 336…659 455 в пробах.
Test-only batch128 updates измеряется отдельно. Цель30FPS полной игры пока
не подтверждена; бюджет400 000 остаётся отложенным. Остаток ROM720 740 байт
не считается резервом всей игры. Качество120 колонок сцены/UI240 сохранено.

Воспроизведение: `powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build-gba-collision.ps1 -CrossChecks`.
`-OriginalEvidence` добавляет Ghidra/Unicorn; общий Unicorn читается из
`C:/Source/wasdify/build/research-deps` без изменений. Итог
`analysis/g08-review.json`, остальные отчёты `analysis/g08-*.json`.
Новые пути: collision header/source, shared cases/test, `tools/collision_{probe,replay}.c`,
скрипты analysis/build/verify/report collision; обновлены GBA integration,
native/GBA/CMake сборка, паспорт G08 и план. G09 в этом проходе не начинался.

## Граница G07 до подключения коллизий

Герой управляется GBA-кнопками в общей сцене LEVEL1. **По указанию пользователя
удалена поступательная инерция**: сразу рабочая скорость, сразу остановка,
немедленная смена направления; исходный разгон поворота пока сохранён.
Это намеренная правка оригинала, не выдавать её за точную реконструкцию DOS.
Подробности, исходные адреса и ограничения: [movement.md](movement.md).

Общий модуль `include/dok/movement.h`, `src/movement.c`; input adapter там же.
`dok_motion_original` — проверяемая исходная арифметика, `dok_motion_update` —
исправленный игровой путь. `gba/movement_probe.c` подключён к общему renderer;
квант416 native ticks (≈24,103Гц), G06 hardware clock, долг сохраняется.
Направления/округления, три quarter-substeps сохранены. y/bounds не меняются.
Коллизий и occupancy update пока нет, свободный ход сквозь стены — граница G07.
G08 подключает `dok_motion_move` вместо `dok_motion_free`; исправление инерции
сохранять. Подмена native far/huge pointers и таблиц явно описана в movement.

Кнопки: D-pad↑/↓ ход, ←/→ поворот, L/R стрейф, B бег; A Fire (G13),
Start edge Use (G09), Select RU/EN, Start+Select диагностический panic.
Состояния held/pressed/released, Start до ближайшего fixed step не теряется.
Камера следует runtime player; test-only camera/motion mailboxes после reset0.

Проверки: Ghidra обеих версий, SHA оригиналов до/после совпали;
`scripts/InspectMovement.java`, `analyze-movement.ps1`, `build/movement/{ru,en}/`.
Unicorn исполнил по2667 векторов/язык; clock/collision и wrapper перехвачены,
не считать это полным запуском DOS. Windows11 suites, Linux/Thumb compile,
shared suite на ARM, точная input/ticks траектория, реальные GBA-кнопки,
остановка без дрейфа, Use edge, RU/EN, guards/poisoned reset прошли.
Все1455 камер LEVEL1–3, независимые HUD/палитра,443520 strips/linked payloads
и G06 time suite прошли. Нового DOS-session, физической GBA/оригинального BIOS,
Linux execution и CMake/MSVC в G07 не было.

Команда: `powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build-gba-movement.ps1 -CrossChecks`.
Ghidra/Unicorn отдельно указанными скриптами либо с `-OriginalEvidence`.
Итог `analysis/g07-review.json`; остальные evidence/regression — `analysis/g07-*.json`.
ROM/ELF/map/PNG: `build/g07/`, ROM32 830 976 байт,
SHA `9bb6fd9f83ea4689893e1e166c6ea7168881a2fb39c53c41800af4e223f0baf1`.
EWRAM129 224 (+168), IWRAM code16 852/data1232, main stack2352/запас5584.
Motion/input/camera до5942 cycles в live-пробах; startup442 726, worst619 898
(LEVEL2 `(19920,−300,2160)`,60°). 30FPS полной игры ещё не подтверждены.
Остаток ROM723 456 байт не резерв всей игры. Качество G06A сохранено.

**Следующий этап — G08.** Ниже — результаты прежних проходов.

Уточнение частот оригинала: [original-cadence.md](original-cadence.md).
В RUS callback героя `1068:2127` назначается с периодом416 ticks ≈41,49мс
(номинально24,10Гц), включает движение; фактические вызовы зависят от кадров,
пропущенные периоды не догоняются. В игровом presentation-пути фиксированный
FPS/vsync wait не найден; ожидание3DAh относится к палитре. Read-only Ghidra,
скрипт/журнал/ASM сохранены, SHA оригинала до/после совпал. Это уточнение
исследования, не завершение G07.

Ранний бюджет renderer не достигнут и отложен по решению пользователя.
Тогда следующим этапом был G07; его результат теперь приведён выше.

Оценка приоритета перед G07, 25 октября: рекомендовалось переходить к G07, продолжая
замеры и локальные оптимизации при интеграции. Основания и контрольные точки
G07/G08, G09–G11 и полной игры — в конце `docs/gba-renderer-review.md`.
Это был анализ существующих отчётов без новых запусков; G07 тогда не начинался.

## Текущая граница G06A

Дополнительный проход завершён 25 октября 2026. После сравнения пользователь
явно принял **120 независимых колонок сцены**, каждая выводится парой пикселей
в поле 240×144. HUD и сообщения остаются 240 пикселей по ширине. Это согласованное
различимое изменение; незаметные упрощения также разрешены. Геометрия и исходные
текстуры сохранены. G07 в этом проходе не начинался.

Актуальная методика, формат банка, эксперименты и выводы по GBADoom:
[дополнительный проход](gba-renderer-review.md). `gba-performance.md` помечен
историческим снимком предыдущего варианта 240. ROM/ELF/map/банк/PNG —
`build/g06a-fast/`. `scripts/build-gba-early.ps1 -CrossChecks` завершился успешно.
Бюджет 400 000 отложен (`budget_met=false`, `performance_status=deferred`),
цель 30 FPS полной игры не отменена и пока не подтверждена.

ROM 32 824 608 байт, SHA
`ddda4054a8f16d3fda2e56c5d96783b085f07ce2cea0af3ec1aace69207f2741`.
Startup 440 812 вместо 599 925; worst 617 307 вместо 993 984 (−26,5%/−37,9%).
Новый worst — LEVEL2 offset `(19920,−300,2160)`,60°,58 faces.
RU/EN/reset со сменой языка 437 041…447 949. На стартовом виде обычная
presentation-проба показывает 29,86375 FPS, два VBlank на кадр. Худший CPU-вид
36,8 мс, поэтому стабильные 30 FPS на всех камерах не подтверждены.
EWRAM 129 056, workspace91 132, IWRAM-код 16 852+данные 1232;
стек 2344/нетронутый запас 5592. До 32 MiB ROM осталось 729 824 байта.

Приняты WAITCNT0x4317 без разгона EWRAM, ARM-семплер с развёрткой 16 и повторным
использованием неизменившегося texel, признаки непрозрачности исходных колонок,
первый covered interval/счётчики/face_order в IWRAM, обход сортированных индексов
без перестановки faces, заполнение незакрытого фона для больших стен.
При 240 колонках старт стал 562 426, прежний тяжёлый вид 818 490 вместо 993 984.
В режиме 120 вычисляются чётные X, записываются halfword-пары.

Банк LIGHT row 0 span 1…140 —29 762 068 байт,443 520 проверенных сочетаний,
339 821 уникальная запись. V-start объединены между views, opaque-only strips
не хранят маски. Span144 всё ещё не помещается в 32 MiB; факторизованные
крупные масштабы 382…1024 отвергнуты за слабый выигрыш при расходе около 4 MB.
Большие span/отсутствующие V-start сохраняют точный fallback. G10/G11 потребуют
пересмотра банка под свет/анимации; остаток ROM не считается резервом всей игры.

Все 1455 камер LEVEL1–3 прошли, RU→EN→RU сохраняет мир/сцену каждого уровня.
Reference рисует far-to-near без банка/новых culls; его чётные колонки явно
дублируются, затем добавляется полный HUD. Это не lossless относительно 240
и не доказательство эквивалентности DOS. Пройдены native/cross compile,
linked payload/strips, guards/poisoned reset, стек, независимый начальный
HUD/палитра, ARM time/scheduler suite.

Тестовый `g_camera_capture_hold` исправляет пропуск короткой фазы готовности
при шаге libretro в один video frame: удерживает показанный кадр ещё один
VBlank вне CPU-таймера. По умолчанию 0, обычные cadence-пробы его не включают.
Текущие отчёты: `analysis/render-early-{startup,benchmark}.json`,
`analysis/g06a-{regression,host-verification,time-regression,column-verification,memory-budget,provenance,review}.json`.
Исходная точка прохода — `build/g06a-review-before/`, эксперименты —
`build/g06a-review-*/`, сравнение — `build/g06a-column-comparison/comparison.png`.
G06 baseline не заменён. Оригиналы не изменялись; физическая GBA не проверялась.

Предел оптимизации без новых потерь графики **не доказан**. Гипотезы: статическая
геометрия/связность ортогональной карты в ROM, объединение плоскостей с учётом
дверей/масок, прямой вывод пар в скрытую VRAM. Это не измеренный выигрыш.
Backface culling ранее отвергнуто после изменения видимых пикселей.
На границе G06A движение/коллизии были впереди; G07 выполнен выше, далее G08.

## Цель и порядок

Пользователь поручил выполнять этапы последовательно без остановок между ними;
при необходимости разрешены субагенты. Целевой результат — **один GBA-ROM
с переключением RU/EN, общим балансом/геометрией и целевой частотой 30 FPS**.
Различия данных разрешаются по `docs/gba-balance-proposal.md`: стартовый гибрид
принят как руководство, неподтверждённые ветви геометрии остаются гипотезами.
База карт пока RUS, переключение языка не меняет мир/баланс.

Последнее уточнение 25 октября — сохранить качество до оценки полной игры.
G06A завершён по обновлённым критериям; недостигнутый бюджет явно записан выше.

Уточнение UI: [gba-ui.md](gba-ui.md). Сохраняем пропорции исходного игрового
поля; нижний остаток экрана (~15 px) целиком под строку сообщений. Жизнь и
заряд числами, ключи — в углах поля поверх сцены. Учесть viewport уже в G05.

Реестр и зависимости — `docs/gba-plan.md`, паспорта — `docs/tasks/`.
**G06 завершён**: модель времени и scheduler проверены на host/ARM,
подключены к общему статическому LEVEL1-ROM. **G06A завершён**: камеры и
ранние оптимизации проверены. **G07 завершён**, следующий этап — G08;
затем строго G09–G21 по порядку. Оценка после G05:
`docs/plan-review-g05.md`. Пользователь потребовал линейный план без развилок.
План и паспорта последующих задач приведены к последовательным зависимостям.
Ориентир 400 000 cycles не достигнут и отложен до оценки полной игры. Новые результаты
и ROM перечислены выше; данные G05/G06 ниже — исторические.
Общие Ghidra-проекты нельзя одновременно изменять двумя агентами одного языка.

Текущие артефакты G04: `docs/rendering.md` и тематические geometry/assets/reference;
`scripts/InspectRendering.java`, `scripts/analyze-rendering.ps1` (34 роли на язык),
`include/dok/projection.h`, `src/projection.c`, `tools/projection_probe.c`.
`scripts/probe_projection.py` прошёл: 31216 native-векторов + Thumb compile,
сохранены wrap32, SAR15 и signed IDIV. В G05 модуль реально исполняется на ARM;
полного renderer/всей игры пока нет.
Итоговый `scripts/verify_rendering.py` прошёл:34 роли каждого образца,
462 изображения,6 LIGHT,9 synthetic,1444 trig values,4 точных door vertex
records. `analysis/rendering-verification.json`. UI — **240×144 + 240×16**:
это сохранение DOS VGA pixel aspect5:6, raw clip оригинала 320×160.

## Выполненные результаты

- **G06:** `docs/time-events.md`, `src/{scheduler,clock}.c`, соответствующие
  headers, `gba/{clock,time_probe}.c`, `tests/{time_cases,test_time}.c`.
  `scripts/InspectTime.java`, `analyze-time.ps1`, `verify_time_evidence.py`;
  Ghidra exports/logs/manifests — `build/time/{ru,en}/`, проекты сохранены.
  SHA обоих оригинальных EXE до/после совпали. Новые DOS-запуски
  `build/time/dos-{ru,en}/` дали по 21 HTTP time samples, PIT period65536,
  около 10 тыс. ticks/с. `analysis/g06-time-evidence.json` хранит инструкции,
  provenance/hashes и сами samples. Точные динамические deadlines DOS,
  rollover и callback mutation в DOS не проверены; модель этих краёв —по ASM.
- Native time —PIT IRQ count +16-bit latched phase, unsigned DIV119,
  номинально 1193182/119≈10026,74 ticks/с, uint32. Сохранены literal unsigned
  deadlines (включая ошибку rollover), negative interval bits, ring order,
  first-slot interval helper, source-slot reschedule после compaction,
  shrinking owner loop bound. Не «исправлять» эти свойства без отдельного решения.
  Destroy current/deferred new owner/reentrancy исправлены явно в time-events.
- GBA timers2/3 —постоянный clock /64+cascade, timers0/1 —cycles renderer.
  Rational remainder исключает зависимость времени от render FPS;
  probe fixed quantum100 ticks, максимум 128 шагов/poll, debt не отбрасывается.
  Это сетка порта, не найденная частота native loop. Движение и полноценные
  игровые callbacks ещё не реализованы. Bind/лимит owners предстоит интеграции:
  owner56 байт,384 owners=21504 байта, прежний резерв 16384 не доказан достаточным.
- `build-native.ps1 -CrossChecks`: все 10 Windows-тестов, Linux static builds
  и Thumb compile прошли. Linux binary не запускался. ARM исполнил тот же suite
  в standalone и общей ROM. `verify_time_gba.py` проверил exact rational
  snapshots, fixed-step callbacks/debt, timer low-word rollover за 600 VBlank,
  poisoned-RAM reset. `analysis/g06-gba-verification.json`,
  `analysis/g06-renderer-time.json`; физическая GBA/оригинальный BIOS не проверены.
- Текущий standalone `build/gba-time/dok-diagnostic.gba`:6988 байт, EWRAM 204,
  SHA `72eea5c229c7d396b64b597409d89c936b14a28420523463343f39480fd2b550`.
  Текущий общий `build/gba-renderer/dok-diagnostic.gba`:2787036 байт, EWRAM 89940,
  SHA `ffb2d8b6c6018c1a7759ed61f23b00d59c7050dc294e9b85eb3bc4e804d9697d`.
  G05 guards/RU→EN→RU/reset и независимый host oracle прошли повторно.
  Полный CPU-кадр теперь включает time-poll/input/language switch; свежие
  `analysis/render-benchmark.json` и `render-host-verification.json` относятся
  к этой ROM. Исторические G05-хэши/цифры ниже —предыдущая сборка.
  Новый полный CPU-замер 10189214…17568022 cycles (max со сменой языка),
  steady37 VBlank≈1,614FPS. Main stack high-water2392, untouched5544 байта;
  IRQ/BIOS исключены. Оптимизация ещё не выполнена —это задача G06A.

- **G05:** `docs/gba-renderer.md`, тематические geometry/assets/verification;
  `include/dok/renderer.h`, `src/renderer.c`, `tools/render_tool.c`,
  `gba/renderer_probe.c`, `gba/divmod.S`. Реальная статическая сцена LEVEL1,
  class2 walls/class4/7 center planes из общего G03; cameraRU(15120,−300,22800),
  heading90.28 faces,28699 pixel writes,3 near clips; ворота совпали с G04.
  Mode4,240×144 поле +240×16 сообщения, FONT8 CP866, HP100/E0 в углах.
- `scripts/build-gba-renderer.ps1 -CrossChecks` прошёл полностью:
  9 native тестов, Linux/Thumb compilation, runtime/packing oracle,
  mGBA и независимый host oracle. Последняя полная сборка дала тот же ROM
  `build/gba-renderer/dok-diagnostic.gba`,2782924 байта,
  SHA `d41d04f79a380dd3fae8b9f9691a8acd1344634b597b5c5c572dce554db53177`.
  EWRAM 89776, renderer workspace54284. Watermark main stack 2400 байт,
  untouched5536; IRQ/BIOS выше 03007F00 не измерены.
- `analysis/render-benchmark.json`, `analysis/render-host-verification.json`:
  RU→EN→RU сохраняет все 33864 байта мира,1344 байта faces и 34560 scene pixels.
  Все 38400 индексов с независимо составленным HUD и 512 palette RAM bytes
  совпали для трёх captures. Guards, capacity1 отказ, poisoned-RAM reset passed.
  В ELF/ROM исполняются 16 edge divmod vectors через ARM/Thumb wrappers.
- **30 FPS не достигнуты:**10175200…10187755 cycles за render+HUD+VRAMcopy
  против 561792; steady36…37 VBlank≈1,637 FPS,18,13× бюджета. Для будущей
  интеграции требуется существенная оптимизация; этот результат не считать
  приемлемой конечной частотой. G05-критерий первого кадра и измерения выполнен.
  Пол/потолок diagnostic flat, LIGHT row 0, Q7-U approximation, heading90 only;
  полный raster, billboard, анимации, движение, игровой цикл и HUD пока впереди.

- **G03:** `scripts/pack_gba_assets.py`, `include/dok/rom_assets.h`,
  `src/rom_assets.c`, `tools/rom_assets_tool.c`, `tests/test_rom_assets.c`,
  `scripts/verify_packing.py`; спецификация `docs/gba-assets.md`.
  Детерминированный пакет 2763046 байт,143 resource IDs,286 исходных payload
  вариантов,3 общих уровня×2 языка. Профиль —39 изменений от RUS по proposal;
  прочие 85 исходных numeric differences/87 map cells явно отнесены к RUS базе.
  `analysis/packing-report.json` подтверждает все 6 views, исходные переводы,
  binding IDs/диапазоны,23 corruptions и побайтный детерминизм пакета/manifest.
- `scripts/build-gba-assets.ps1 -CrossChecks` прошёл целиком:
  **9 Windows-тестов**, Linux/Thumb compile, runtime/packing oracle, ARM ROM.
  `build/gba-assets/dok-diagnostic.gba` —2776104 байта, EWRAM 34572;
  пакет в cartridge совпадает с offline assets.bin побайтно.
  Все 6 original-derived views реально reset на ARM. Select меняет RU/EN
  textpool; все 33864 байта mutable мира сохраняются, poisonedRAM reset проходит.
  `analysis/g03-gba-verification.json`, PNG `build/gba-assets/assets-{ru,en}.png`.
  Это технический asset экран, не игровой renderer/HUD.
- AssetsProbe резервирует **8 KiB IWRAM stacks/BIOS** через LLD override:
  локальные 580-байтные views reader могут вкладываться. Default G01/G02 —1KiB.
  Глубина стека пока не измерена; `gba/memory.c` добавляет проверенные AEABI
  copy/clear/unsigned-division helpers для freestanding ROM.

- **G00:** `scripts/dos_reference.py`, `docs/reference-runs.md`,
  `analysis/reference-manifest.json`. Пять неизменённых DOS-запусков
  в `build/reference/{ru-01,ru-02,ru-03,en-01,en-02}/`.
  Стартовые VGA-индексы побайтно повторяются внутри языка. Реальные позы:
  RU `(15120,-300,22800)/90`, EN `(15120,-300,23280)/90`, HP100, energy0, lives3.
  Input — исследованная таблица игры через localhost API, не IRQ1;
  PNG — grayscale индексы, DAC palette пока не собрана. Поворот/движение
  наблюдались, wall-clock удержания не дают идентичной конечной позы.
  Стена/дверь/подбор/оружейный выстрел/AI/смерть/переход пока pending.
- **G01:** `gba/startup.S`, `gba/gba.ld`, `gba/main.c`, build/run/verify scripts.
  ARM startup/IRQ, Thumb C, Mode4, кнопки, счётчики, controlled panic/reset.
  Диагностический ROM 2560 байт; native `.data/.bss`40 байт EWRAM.
  `docs/gba-platform.md`, `analysis/g01-verification.json`.
- **G02:** `include/dok/runtime.h`, `src/runtime.c`, `tools/runtime_tool.c`,
  `tests/test_runtime.c`, `scripts/analyze_runtime.py`.
  Immutable ROM descriptors/typed initial states отдельно от RAM runtime.
  Один объект на glyph, последнее размещение повторов, explicit unknown
  неразмещённых актёров. Все 6 исходных миров/780 definitions/69623 known bytes
  и полные masks/все class fields/resources/texts/maps сохранены.
  `analysis/runtime-verification.json`, `docs/gba-memory.md`,
  `analysis/gba-memory-budget.json`.
- ARM runtime **33864 байта**: objects256×36, actors128×64, player60,
  maps4×4096 плюс указатель/счётчики. Max исходных defs158, actors106,
  field records1655, textpool8849. Actor reserve22 — инженерный запас,
  не доказанный лимит активных снарядов/врагов оригинала.
- `gba/memory_probe.c` + `scripts/runtime_memory.py`:
  `build/gba-memory/` ELF/map/layout/ROM 7640 байт. С реально размещёнными
  резервами будущих buffers EWRAM 178800, свободно 83344.
  Резервы 144896 — не готовые подсистемы. Верхний 1KiB IWRAM пока reserved
  stacks/BIOS; глубина стека не измерена. `analysis/g02-memory-run.json`
  подтверждает ARM synthetic reset/position/release, startup, keys, IRQ,
  panic, poisoned-RAM reset. Это не запуск всех 6 игровых миров на ARM.
- Прошли **7 Windows-тестов**, Linux static builds и Thumb compile checks.
  Cross binaries не исполнялись. Для Zig0.13 нужен явный
  `thumb-freestanding-eabi`: прежний `arm-freestanding-eabi -mthumb` давал ARM.

## Команды и общие инструменты

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build-native.ps1 -CrossChecks
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build-gba-assets.ps1 -CrossChecks
C:/Source/wasdify/.venv/Scripts/python.exe scripts/analyze_runtime.py
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build-gba.ps1 -Verify
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build-gba.ps1 -MemoryProbe -OutDir build/gba-memory -Verify
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/run-gba.ps1
```

- Общий Zig `%LOCALAPPDATA%/Programs/Zig/0.13.0/zig.exe`, LLD18.1.6.
  Build scripts временно задают кеш `build/zig-cache`, PATH не менялся.
- Python `C:/Source/wasdify/.venv/Scripts/python.exe`, stdlib.
- Общий mGBA standalone0.10.5 — `%LOCALAPPDATA%/Programs/mGBA/mGBA-0.10.5-win64/`.
  Автопроверки в отдельном ядре 0.11-212-7a12d6d
  `%LOCALAPPDATA%/Programs/mGBA/libretro-20261024/mgba_libretro.dll`;
  источники/хэши `analysis/gba-tools.json`.
- Общий DOSBox Staging0.83.0(7b400):
  `%LOCALAPPDATA%/Programs/DOSBox-Staging/wasdify-webserver-20261024/`,
  источник read-only стенд WASDify, exe SHA
  `67a907289bad3e25a9be7dd4dc38d882c1c7e74949f8daa103a52476d99f24f5`.
  normal/Pentium/60000cycles,32MiB,svga_s3,VGA320×200 без -r,SDL dummy.

## Исходная база и ограничения

Оригиналы `originals/pkr/{RUS,ENG}` не изменялись; EXE/RES SHA в
`analysis/inventory-{ru,en}.json`. Полные runtime данные и packages только
в ignored `build/`. GBA RAM не должна содержать host converter storage70716
или полный `dok_world_object.bytes/known`; это материал инструментов.
G03 использует отдельные языковые ROM pools/views без двух mutable миров.
Пакет/manifest/generatedassets находятся только под ignored `build/gba/assets/`.

Общая Ghidra `C:/Users/dspev/AppData/Local/Programs/Ghidra/ghidra_12.1.2_PUBLIC`,
проекты `build/ghidra/{ru,en}/Kremlin.gpr`, экспорт `decompiled/`.
Java/headless настройки — `scripts/annotate-stage6.ps1`.
Существенные доказательства этапов 2–6: `docs/resources.md`, `animations.md`,
`sound.md`, `levels.md`, `world.md`; различия/параметры — `enemy-parameters.md`,
`enemy-tables.md`, `weapon-parameters.md`, `level-version-comparison.md`.
Материалы `C:/Source/wasdify` только для чтения, происхождение/ограничения
`docs/wasdify-findings.md`. LEVEL и world имеют разный порядок плоскостей.

Физическая GBA, оригинальный BIOS, GUI/GDB-сессия и
производительность полной игры пока не проверены. Исторические~20 обновлений
текста диагностики не являются оценкой рендера игры или достижением 30FPS.
G06/G06A/G07 закрыты; продолжать с G08 по линейной очереди. Снижение качества отложено.
