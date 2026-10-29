# N11: взаимодействия мира

Архитектура принята до реализации. Новый native C11-модуль NDS восстановлен
по PKR/PKE ASM; GBA-код, его профиль30 и сетка333/333/334 не переносились.
Интерфейсы: [interactions.h](../../../ports/nds/include/dok/interactions.h),
[interactions_move.h](../../../ports/nds/include/dok/interactions_move.h).
Реализация: [interactions.c](../../../ports/nds/core/interactions.c),
[interactions_move.c](../../../ports/nds/core/interactions_move.c).

## Источники и адреса

Адреса ниже — Ghidra NE loader исходных EXE. Это не файловые смещения,
номера NE-сегментов, DPMI-селекторы или входы вставок WASDify.
Автоматический псевдокод хранится отдельно от редактируемого движка.

| Поведение | PKR RU | PKE EN |
|---|---|---|
| Базовый event handler | `1068:0521` | `1020:0A01` |
| Проба Use героя | `1068:1B10` | `1020:20C4` |
| Проекция одной точки | `1098:0309` | `1030:03ED` |
| Door event handler | `1068:347E` | `1020:3B33` |
| OPEN / CLOSE / AUTO | `1068:3262/334E/3427` | `1020:3917/3A03/3ADC` |
| Sensor routing | `1068:091E` | `1020:0E1C` |
| FCMap перед движением | `1068:2127` | `1020:26D7` |
| Stat / finish condition | `1068:1CC2` | `1020:2276` |
| Direct movement wrapper | `1068:10A4` | `1020:15AE` |

PKR.EXE SHA-256 `12f78afc0eba05f76b5dc0bb99da38d7daa20585024c0ba958fe828aa3864d28`;
PKE.EXE SHA-256 `cce7bc5993d0734ad87e93db37a70b8b52faedfff14ac48d6e317e5ac996a37b`.
Свежий экспорт и независимый oracle описываются отдельным N11 evidence;
контролируемые перехваты звука/текста/clock не равны полному DOS-сеансу.

## Владение и жизненный цикл

Единственные карты принадлежат N05 `dok_initial_world`. Определения RU,
их CP866-ресурс и исходные поля заимствуются, а не переписываются.
N10 остаётся единственным владельцем mutable flags и WORD occupancy по glyph.
N11 хранит по glyph только `phase`, `extent`, байты `state`, `ready`, `marker`;
размер записи8байт, таблицы2048байт. Повторные клетки glyph разделяют эту запись.
Персонального состояния створки на клетку нет: клетка имеет собственную геометрию,
но фаза объекта общая.

State также заимствует scheduler и services, хранит полный previous-FC reference,
epoch/revision и диагностические счётчики. Object ID не считается glyph.
Ссылка `{epoch,id,generation}` проверяется по world seeds. Инициализация дверей:
phase0, extent1, state2, ready1, marker из определения; occupancy0 уже задаёт N10.
Native flags после полного CLOSE восстанавливаются из исходного определения.
Динамические actor lifetimes, произвольные mods и image animation не добавляются.

После bind, до первого dispatch, оболочка регистрирует все class7 owners:
native capacity4 означает три usable slots N08. Герой регистрируется оболочкой
отдельно. Регистрация внутри dispatch запрещена. Активация заранее
зарегистрированной двери из sensor callback использует N08 pending-owner policy.
При смене уровня сначала заканчиваются collision context и render lease,
сбрасывается N08 scheduler, затем создаются новые borrowers с новым epoch.

Во время interactions запрещён другой collision context; во время scene lease
оболочка запрещает любую gameplay mutation. Все вызовы синхронны на одном потоке,
без malloc, SDK, файлового I/O или скрытых часов. Sink не входит повторно в
interactions/events и не меняет мир. Его output заимствован только до возврата.

## События, ключи и состояния двери

`0x800` — Use. Door принимает его только при ready!=0, захватывает extent и
очищает бит **до** проверки замка. При mask!=0 достаточно любого общего бита
`(mask & keys)!=0`. Ключи не расходуются. Source класса13 использует FFFF и
обходит замок. Источник героя/сенсора проверяется полной ссылкой.

Class6 использует базовый handler: Use не потребляется и не создаёт toggle.
Базовый handler снимает `0x40000`, только когда source существует, а flags
не содержат8 и16. Door снимает `0x40000` при отсутствии8, независимо от16.
Неизвестное игровое назначение этого бита не придумывается.

Состояние2 ставит OPEN через416 native ticks. Mode1 одновременно ставит AUTO
через signed DWORD delay, переданный как исходные32бита. Состояние1 при mode0
ставит CLOSE через416; mode1 оставляет повторный Use открытой двери no-op.
Ready остаётся1 до первого callback: ранние повторы могут заполнить три slots.
N08 `NO_SPACE` сохраняет native отказ без отката уже выполненного extent/event
write, увеличивая `schedule_refusals`; остальные ошибки возвращаются явно.
Четвёртый активный slot, дедупликация и новая очередь не добавляются.

OPEN при state2 и phase<WORD(extent−1) ставит flags|8, ready0, исходный signed
WORD interval и semantic sound-ensure; затем phase++ по WORD и passability16
только при `phase>trunc(extent/4)`. Следующий callback после последней фазы
ставит state1, flags|16, sound-stop, NOT marker, status, ready1 и cancel-current.

CLOSE сначала проверяет **signed WORD** occupancy>0. ЗначениеFFFF означает−1
и не блокирует. При свободной двери state1/phase>0 уменьшает phase, ставит
ready0 и interval; passability остаётся до завершения. Следующий callback
при phase0 ставит state2/phase0, восстанавливает исходные flags, инвертирует
marker и отменяет текущий slot. AUTO ждёт occupancy<=0, state1 и ready!=0,
затем ставит CLOSE **ещё через delay** и отменяет себя. Занятый AUTO продолжает
исходный период; подмена его нулём неверна.

N08 сохраняет собственные исходные особенности порядка, удаления/компактации,
first-slot interval helper и unsigned deadline comparison. N11 не выполняет
catch-up и не заменяет scheduler фиксированным шагом анимации.

## Use и зоны до движения

SDK X/B объединяются принятым controls N09 в один release-only Use. N11 не
опрашивает кнопки. Hero Use берёт ровно одну точку на CellSide впереди:
distance=−CellSide, x+=SAR15(low32(distance*sin)),
z−=SAR15(low32(distance*cos)). Lookup TMap делит signed DWORD координаты
с trunc0, сужает quotient до WORD и прижимает индексы. Raycast не применяется.
После target handler герой всегда снимает Use, включая class6/space/no-op.

Hero-floor вызывается перед N09 motion. Подборы MMap относятся к N14 и здесь
не имитируются. FCMap class13 сравнивается с previous **объектом**, а не клеткой:
соседняя клетка того же glyph не повторяет событие; выход на другой объект и
возвращение повторяют. Previous сохраняется и для пустого FCMap объекта.
Sensor получает target glyph из+66 и DWORD bits из невыравненного+67,
затем вызывает тот же handler с source-сенсором.

Class12 ставит slow04000000 и делит sign-extended value WORD+68 на signed
DWORD C2 с trunc0; quotient сужается в WORD. Нулевой делитель отвергается
вместо native division fault. N09 motion сам захватывает и очищает slowbit
исходной маскойFBE7DFC3; command refresh00182A3C менять не требуется.

Category100 и101 прибавляют signed WORD к X,102 — к Z, low32 wrap сохранён.
Это **один** direct resolver/occupancy wrapper, не три quarter steps N09.
Additive adapter делегирует новому публичному `dok_collision_displace`.
Тот использует исходные lookup/resolver/commit N10 ровно один раз; x/z
публикуются только после end, остальные motion scalars сохраняются.
Quarter-step guard N09 здесь не применяется: при штатном C2=4 conveyor
value1400 даёт displacement350. Разбиение на несколько коротких шагов
изменило бы source resolver и occupancy и потому запрещено.

Новый API добавлен к collision.c после принятых N10 функций, без изменения их
тел; объявление добавлено перед `#endif` collision.h. Это обоснованное расширение
принятой границы двумя различными native wrappers, а не основание удерживать
неверное ограничение350 ради старых source hashes. Root сохраняет byte-exact
старые два файла и manifest в `research/evidence/nds/n11/prior-source/`;
N11 gate отдельно проверяет старые pins по архиву, новые source pins и полную
регрессию N10. Старые N10 ROM, отчёты и доказательства не переписываются.

Direct admission проверяет полный ref, epoch, bounded origin и отсутствие
чужой origin occupancy; резервирует один revision. После единственного native
resolver применяет прежние NDS final-bounds/occupied guards, затем одну
DEC/clear/INC/write транзакцию. Raw candidate не отбрасывается заранее
и не ограничивается половиной клетки. Native map clamp вне реального мира
по-прежнему отличается от bounded NDS final-pose policy; это отдельная
классификация доказательств, а не обещание native unsafe coordinates.

Category1000 сравнивает borrowed signed WORD score с world category99_total.
При равенстве выдаётся FINISH, без смены уровня, health workaround, кампании
или записи сохранения. Повторные callbacks могут повторить FINISH, как исходное
условие. Прочие категории передаются STAT будущей N14, без выдуманной системы
характеристик. Keys/score до N14 поступают из явно названных fixtures.

## C2 и независимость представления

`dok_interactions_cadence` воспроизводит arithmetic исходного end-loop:
unsigned10000/frame_ticks, ADD oldC2 modulo2³², logical SHR1, signed DWORD
минимум4. Door читает signed **младшее WORD** C2 с минимумом8; floor использует
полный signed DWORD. Нельзя заменить оба пути одним clamp или константой30.

Исходный DOS обновляет C2 по длительности кадра после rendering. Принятая
NDS-адаптация обновляет C2 по фактическим **service sweep samples**. Service pump
доступен на каждом VBlank, независимо от запроса нового представления30/60.
Каждый sweep остаётся одним native N08 dispatch; fixed simulation grid и
догоняющие callbacks отсутствуют. При задержке rendering следующий service
sample отражает реальную задержку. Это сохранение арифметики с явным изменением
источника cadence, а не доказательство полной DOS временной эквивалентности.
Core получает C2 явно и не измеряет GPU/FPS. Проверка0/30/60 относится к одной
явной input/service/C2 timeline, а не произвольно разным polling schedules.

## Представление и отложенные потребители

`dok_interactions_phases` копирует epoch/revision, phase[256] и extent[256].
Renderer читает этот snapshot после законченного simulation callback; до
scene release фаза, flags и карты не меняются. Время представления не двигает
door state machine. Source passability открывается раньше полной фазы, и это
не подменяется отсутствием полотна в кадре.

Геометрия/crop/отражение реализуются отдельным door renderer над N07. Полностью
открытая панель при extent>1/phase>=extent−1 скрывается — явная NDS-коррекция
исходной остаточной полосы integer crop, требуемая паспортом. По glyph общая
фаза, по клетке отдельное полотно и исходное направление отражения.

Sink публикует LOCKED с key mask, SOUND_ENSURE/STOP, STATUS, STAT и FINISH.
Звук/текст, pickup inventory и переходы потребляют N18/N19/N14/N22 соответственно;
N11 не создаёт альтернативные подсистемы. Semantic ENSURE запрашивает исходное
«играть, если не играет»; физический sound-playing query здесь не имитируется.
Output order синхронный, глобальная очередь с произвольным лимитом отсутствует.

Shared unit suite: [interaction_cases.h](../../../ports/nds/tests/interaction_cases.h),
[interaction_cases.c](../../../ports/nds/tests/interaction_cases.c).
Workspace174856байт на Windows64 помещается в startup world union393216,
не заявляется помещающимся в scratch131072. Итоговые проверки, ARM9 размеры,
oracle vectors, hashes и эмуляторный маршрут фиксируются в паспорте после gate.

Отдельный [host world driver](../../../ports/nds/tests/test_interaction_world.c)
загружает реальные три RU-мира и языковые bindings RU/EN. На одной явной
960-service timeline он проверяет все18 дверных объектов каждого уровня,
замки, class6 no-op, occupancy/AUTO, entry sensors и finish, повторяя прогон
с0/30/60 phase observations. Hash включает карты, flags, counters, door state,
previous-FC, outputs, callback count, C2/now/revisions и активные scheduler slots.
Частота observations не меняет эти состояния.

На LEVEL2 отдельно выбирается по одной реально размещённой свободной клетке
каждого conveyor glyph `<`, `>`, `{`, `}`. Для C2=4/5/6/60 проверяются16 direct
перемещений: результат композиции принятого native resolver и NDS final guards,
полная MMap, неизменность TMap/FCMap/visited и всех motion scalars кроме x/z/slow.
Поза героя здесь явно переставляется fixture; это не доказательство достижимости
всех этих клеток игровым маршрутом. Возвращается весь исходный live state после
каждого набора fixtures. На контрольных клетках final guards не сработали;
отдельный unit проверяет native-open-edge/NDS-bounded отличие. Этот host driver
доказывает интеграцию новых компонентов; независимое сравнение с исходными x86
инструкциями остаётся отдельным oracle, а не выводится из двух новых C-путей.

Дополнительная [router suite](../../../ports/nds/tests/interaction_router_cases.h)
проверяет связку принятых public input/controls/events с N11. Один ordered input
подаётся в два controls latch: main-action и hero. Main action после sweep
потребляет Use; более редкий hero callback независимо получает сохранённые
movement/fire pulses. Общий take одного latch на каждом main sweep терял бы
короткие движения до hero deadline и потому не применяется. X/B в одном release
дают один Use; отдельные release samples остаются отдельными Uses. На pause
boundary очищаются оба latch и input queue; held keys подавляются до release.
Shared suite содержит50 проверок и workspace175704байта Windows64. Она проверяет
составленный public-API pipeline; фактический порядок SDK main дополнительно
проверяется runtime trace, а не выводится из наличия этой host suite.
