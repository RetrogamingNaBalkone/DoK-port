# SP06: состояние, конструкторы и жизненный цикл

4 ноября 2026 года. Инвентарь относится к SDL2, а не к раскладке DOS или
проектам GBA/NDS. Поля исходных объектов сопоставлены с логическим состоянием;
адреса vtable, выделенной памяти и неинициализированные байты не сериализуются.

## Доказательства и адреса

Свежий read-only Ghidra-экспорт:
`research/evidence/source-port/sp06-world/{ru,en,old-gold}/`.
Manifest каждого профиля содержит SHA-256 EXE до/после, проект, диапазоны,
хеши ASM/байтов и драйвера. Образцы соответствуют SP01; хеши не изменились.
Адреса PKR/PKE — Ghidra NE, OG — Ghidra MZ, не файловые смещения и не
runtime-селекторы. Автоматический C использован для навигации, store/call
проверены по `objects.asm`. Экспортирует существующий общий read-only
`ExportSourcePortScheduler.java`; драйвер SP06 задаёт другие диапазоны.

| Область / class | PKR | PKE | OG | Логическое состояние |
|---|---|---|---|---|
| База всех классов | 1068:02f5 | 1020:07cf | 1ae1:01c5 | Owner capacity4/count0, flags, definition; animation remaining0/frame0; flags0800 выбирает первый кадр |
| 0, поверхность | фабрика1068:0000 | фабрика1020:018c | 1ae1:3c77 | База + неизменяемые поля LEVEL |
| 1, герой | 1068:1657 | 1020:1b61 | 1ae1:128f | PlayerState, MotionState; lives3, weapons null, score/current/pending/native_da0; player event416 |
| 2, стена | фабрика1068:0000 | фабрика1020:018c | 1ae1:3cba | База; OG H имеет настоящий flags0800/Flic |
| 3, предмет | фабрика1068:0000 | фабрика1020:018c | 1ae1:3cfd | База; свойства/уведомление LEVEL, удаление клеток в maps |
| 4 | фабрика1068:0000 | фабрика1020:018c | 1ae1:3d41 | База; геометрические свойства определения |
| 5, актёр | 1068:09c5 | 1020:0ec9 | 1ae1:0829 | MotionState; позиция не выдумывается для неразмещённого объекта |
| 6 | 1068:30a2 | 1020:374b | 1ae1:2b18 | SwitchState: native54/56/58 и interval52 равны0; семантика не переименована предположением |
| 7, дверь | 1068:30ff | 1020:37ae | 1ae1:2b78 | DoorState phase0/extent1/interval0/state2/ready; occupants0; flags из определения |
| 8, шаблон снаряда | 1068:22b6 | 1020:2866 | 1ae1:1fed | Определение сохраняется; экземпляр ProjectileState создаётся при spawn с собственным owner |
| 9, монстр | 1068:36b1 | 1020:3e14 | 1ae1:3178 | MonsterState health из LEVEL/state0, отдельный первый кадр и часы; MotionState |
| 10, оружие | фабрика1068:0000 | фабрика1020:018c | 1ae1:3d84 | WeaponState state4, shift/frame0, acquired false |
| 12, эффект поверхности | 1068:06d6 | 1020:0bbc | 1ae1:059c | SurfaceState phase18/interval0; getter/водный raster принимает SP07 |
| 13, сенсор | 1068:085e | 1020:0d50 | 1ae1:06fa | База + immutable target/flags LEVEL; последнее касание хранится у героя |

Class11 фабрики не принимают. Порядок: конструктор → reader LEVEL → отражение
карт/размещение. Одно определение соответствует одному объекту; повторяющийся
glyph не клонируется. Окончательная поза — последняя клетка MMap. Первичная
сверка шести Kremlin миров и OG105 — `check_assets.py`; typed LEVEL принят SP04.
Числовые actor-поля, не имеющие отдельного потребителя порта, не выдаются за
полный native memory dump: MotionState хранит реальные аккумуляторы SDL2,
а остальные необходимые параметры берутся из immutable определения.

Far this/stream/resources заменены owner/index/name. RETF и caller stack
cleanup, CWD/MOVSX для signed intervals, WORD store и порядок вызовов
сохранены семантически; DOS heap/vtable/UI side effects не копируются.

## Общая анимация: контракт SP07

PKR callback/getter `1068:0421/04ae`, PKE `1020:0901/098e`,
OG `1ae1:02fe/0389`; next accessor PKR `1050:0054`, PKE `1018:0f62`,
OG `1aa1:0055`. ASM подтверждает:

- Конструктор при flags0800 ставит frame−1 и вызывает next до первого кадра.
  Состояние в порте: frame0, параметры A/B и signed interval из кадра0,
  remaining0; событие ещё не зарегистрировано.
- Getter при remaining0 сначала пишет200, затем пытается schedule с MOVSX
  interval. Повторный getter при remaining>0 не продлевает бюджет.
- Callback при remaining>0 уменьшает его, выбирает следующий кадр; END
  ставит−1, повтор next даёт0. Интервал меняется через slot0 scheduler.
  При remaining<=0 следующий callback отменяет себя.
- Между 200-м callback и отменой getter может добавить второе событие.
  Это исходный эффект, тестируется, а не скрывается флагом scheduled.
  Переполненная очередь оставляет budget200 и не добавляет событие; порт
  также не завершает игру исключением. Отрицательные интервалы не исправляются.

`Animations` владеет неизменяемыми decoded clips; `WorldObject.animation`
содержит **всё изменяемое состояние**: frame, A/B, interval, remaining.
Специализированные классы8/9/10 сохраняют свои players. `image(owner)` — const,
не активирует и не продвигает мир, возвращает стабильный адрес кадра до
уничтожения службы. Runtime flags изображения равны8. A/B остаются нейтральными
signed параметрами, не объявлены координатами/масштабом без доказательства.
Повторное создание bindings над существующим World не сбрасывает playback.

Принятая адаптация SP12 применяется и здесь: getter activation вынесена
из рендера на player tick с `original_cell_bounds`, независимо от FPS/backend.
TMap активируется до solid-wall early-out; MMap за стеной пропускается.
Покрываются видимые предметы, декорации и анимированная стена OG H; один glyph
в нескольких клетках имеет один бюджет. FCMap surface getter и вода — отдельная
SP07: сейчас Scene вычисляет flowing phase из simulation ticks; новые поля
SurfaceState не объявлены уже подключённым восстановлением этого getter.

SP07 начинает с передачи Animations::image в SoftwareScene для каждой
object_images binding, включая **OG glyph H=72 class2** и **$=36 class3**;
PKR/PKE X/Y/Z=88/89/90 class3. Не ограничивать обновление billboard: H — стена.
Рендер остаётся const относительно состояния. Проверить меняющиеся размеры,
свойства кадра, software/GL кеши и общий shared glyph. Сейчас main уже создаёт
службу, вызывает consider на player tick и маршрутизирует base_animation;
Scene до SP07 всё ещё показывает первоначальный кадр.

## Полный инвентарь для SP17

S — сохранять логически; R — восстановить из выбранного RES/определений;
T — временное, сбросить на границе загрузки. Это требования к будущему
SaveSnapshot, не реализация сохранений.

| Владелец | Поля | Класс |
|---|---|---|
| Идентичность | DatasetId, LevelId, выбранный источник/совместимость содержимого | S, затем проверить R |
| LevelDefinition | settings, glyph_index, исходные maps, все typed fields, строки/ресурсы | R; не путать с изменёнными maps |
| World | maps всех3 плоскостей, visited, player index, total_money, map_revealed, level_complete, message CP866 и message_until | S |
| World clock | GameClock ticks_ и rational remainder_, simulated_ticks_ | S; нельзя округлять остаток |
| World guards | advancing_, audio std::function | T; advancing запрещён при snapshot, audio перепривязать |
| WorldObject | definition_index, owner, optional pose(x/y/z/heading), placements, flags, door_occupants, presence каждого optional state | S (не восстанавливать flags только из LEVEL) |
| PlayerState | attributes[11], extra_attributes[3], weapons[9] owner IDs, current/pending, lives, score, native_da предыдущего sensor owner | S |
| MotionState | clock; acceleration/turn_acceleration, turn_direction, last_forward/strafe, mouse_remainder | S; float bits/версия формата явно |
| DoorState | phase, extent, interval, state, ready | S |
| WeaponState | state, shift, frame, acquired | S |
| MonsterState | health/state, next/display frame и display clip, course_counter/shot_count, burst_clock, corpse | S |
| AnimationState | frame, A/B, signed interval, remaining | S |
| SwitchState/SurfaceState | все подтверждённые ctor words, phase/interval | S; потребители SP07/SP10 не объявлены готовыми |
| ProjectileState[256] | presence, pose, definition_index, source owner, damage/floor/frame/speed/flags, impact/actor_impact/effect_frame, occupied_cell, clock | S |
| Projectile occupancy | весь массив -1/slot; взаимная проверка occupied_cell, static maps и live slots | S + validate |
| Stopwatch (везде) | started_, elapsed_, running_ | S; одного read(now) недостаточно |
| Events | owners и capacity/slots/count, next/previous/linked, global cursor/active | S; ring order влияет на поведение |
| Events переходные | pending_, owner.pending/destroyed, current_, dispatching_, callback_active_, keep_, clear_requested_ | T; снимать только после finish, очередь pending пуста, флаги неактивны |
| Weapons | world reference, animations cache | R; mutable fields только World |
| Monsters | world/projectiles references, clips, monster_hit closure | R; destructor снимает closure перед destruction Projectiles |
| Projectiles | world reference, clips, monster_hit closure | R; dynamic states/occupancy в World |
| Animations | world reference, clips | R; конструктор сохраняет уже существующий AnimationState |
| GameFlow | spawn Pose, phase | S; создавать со старым spawn, не текущей позой после load |
| MonoVoice | active SoundId, resource identity вместо sound*, loop_, period_, output_rate_, source_clock_, sample_count_, phase_ | S/платформенное согласование SP13; указатель R |
| SdlAudio | sounds cache/device/callback | R; volume/requested_volume/paused — settings/session; аудио lock на snapshot |
| Аудиокоманды | World отправляет синхронно, очереди в текущем коде нет | snapshot между callbacks; не из SDL audio callback |
| ViewMotion | delta_[4], tick_ | T: reset на load/retry; не влияют на симуляцию |
| Scene/GL/HUD | изображения, палитры, geometry/material caches, viewport и выходные буферы | R; не включать host pointers/GPU IDs |
| Main/Input/Frontend | use_pending/use_previous, pressed keys, mouse capture, cheat, wall-clock previous frame, меню | T: очистить; пользовательские settings отдельно |

Snapshot допустим только между завершёнными simulation sweeps и при
согласованном audio lock. Транзакционный load строит независимый World и
bindings, проверяет ссылки/кольцо/слоты, затем меняет сессию; не mutate живой
мир по частям. Ни один текущий getter не выдаётся за готовый serialisation API.

## Слоты и сброс

Static owner=index+1; dynamic projectile owner=0x10000+slot. Определения всех
glyph сохранены даже без размещения. Dynamic glyph выбирается из свободных
определений; живое определение не стирается. Во всех 899 исходных definitions
нет flags0400. Native PKR `1068:2849/284f`, PKE callback `1020:2d8b`, OG
`1ae1:230c` помечают этим флагом **завершённый динамический снаряд**, снимают
1000, очищают его клетку и отменяют event. `1098:017d` затем выбирает первый
null либо flags0400 slot. Порт освобождает завершённый optional сразу, что
сохраняет выбор того же минимального свободного slot; исходные определения
не блокируют этот жизненный цикл. Прежний комментарий про reclaim actors
исправлен: обычная смерть монстра не объявляется присваиванием0400.
Синтетические LEVEL с retired static definitions не входят в исходные наборы.
Exhaustion всех свободных slots и освобождение проверяются отдельно.

Завершение projectile снимает occupancy, cancel текущего event и optional slot;
пустой Events owner переиспользуется, число таких owners ограничено256.
Тест проходит reuse каждого шаблона дважды. Destructor Monsters снимает closure
в Projectiles. Main уничтожает audio/HUD/flow/monsters/projectiles/weapons/
animations/scene до World. Новый World обнуляет время/visited/slots/maps/status;
retry — отдельный GameFlow-контракт, не полная перезагрузка мира.

## Воспроизведение и границы

```powershell
py -3.14 -B ports/sdl2/tools/export_world_audit.py
powershell -NoProfile -ExecutionPolicy Bypass -File ports/sdl2/tools/build.ps1 -Configuration Release
build/sdl2/windows/dok_lifecycle_tests.exe originals/Old_Gold_3D/OLD_GOLD.RES originals/pkr/RUS/PKR.RES originals/pkr/ENG/PKE.RES
py -3.14 -X utf8 -B ports/sdl2/tools/check_world_lifecycle.py
py -3.14 -X utf8 -B ports/sdl2/tools/check_assets.py
```

Ctor audit — ASM-сверка, не выполнение всех native конструкторов с DOS heap.
Release CTest15/15; lifecycle:7 миров/20 анимаций/50 reuse, exhaustion и
synthetic class5 PASS. Семь Windows-процессов прошли30 кадров; report/logs
в `research/evidence/source-port/sp06-world/checks/`. RES SHA-256 неизменны.
Lifecycle tests — контракт C++ плюс проверенные исходные значения, не полные
прохождения. Состояние/инициализация приняты отдельно от полноты поведения
SP07/SP10–SP13. Surface/class6 consumers и renderer
сверка остаются явно назначенными владельцам выше.
