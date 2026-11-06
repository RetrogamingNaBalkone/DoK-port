# SP17 — контракт сохранений

## Хранилище и интерфейс

`<профиль>/saves/{old_gold,pkr,pke}/slot01.sav` … `slot10.sav`, `quick.sav`.
Профиль выбирает существующая SP23-логика; settings.ini находится рядом с
каталогом saves. Уровень записан внутри файла, а не в пути. Датасеты независимы.
Новая игра, смерть и завершение уровня не записывают файлы автоматически.

F5/F9 — стандартные переназначаемые действия. F9 немедленный; загрузка через
меню при активной сессии, перезапись и удаление требуют подтверждения с отменой
по умолчанию. При повреждении основной записи доступна проверенная .bak.

Уточнение пользователя после первой сборки: экраны слотов имеют ту же
центрированную ширину 420 виртуальных пикселей, рамки, фон и ритм строк, что
обычные меню. Высота ограничена содержимым, список прокручивается; превью 128×80
рядом, на 320×200 — компактный блок над списком. Отдельных success-плашек нет:
«ИГРА СОХРАНЕНА» / «ИГРА ЗАГРУЖЕНА» выводятся через World message/key/until
в существующей панели игровых сообщений, включая переключение RU/EN.

## Аудит состояния

Основа — текущие C++-модули SDL2, их подтверждённые контракты SP05–SP14 и
исследования WASDify save-load-status/state-gaps/failures. Это новый формат
порта; DOS-двоичный формат или ABI не переносятся. Новый анализ исходных EXE
не требуется для перечисления полей C++: игровые алгоритмы не изменяются.
Ранее проверенные адреса и различия OG/PKR/PKE остаются в паспортах владельцев.

| Область | Сохраняется | Восстанавливается из данных / не сохраняется |
|---|---|---|
| World | maps, visited, objects, projectiles, occupancy, player, total_money, map_revealed, level_complete, message/key/until, simulated_ticks | LevelDefinition из RES; audio — новая привязка |
| WorldObject | definition_index, pose, placements, owner, flags, door_occupants, все optional-компоненты | Идентичность и состав сверяются с исходным уровнем |
| Player | attributes/extra_attributes, weapons, current/pending, lives, score, native_da | Нет скрытых ссылок на DOS-память |
| Motion | stopwatch, acceleration/turn_acceleration/direction, bob_direction, last axes, mouse_remainder | Накопленный SDL-ввод сбрасывается |
| Door/weapon | Все поля фазы, ready/state, интервала, saved_flags, shift/frame/acquired | Декодированные кадры и изображения |
| Monster | health/state, next/display frame/clip, course_counter, shot_count, burst_clock, corpse/passage_released | clips, monster_hit callback |
| Projectile | pose, definition/source, damage/floor, frame/speed/flags, impact flags, occupied_cell, clock; все 256 optional-слотов | clips; логические owner ID 0x10000 + slot |
| Animation/switch/surface | Все поля World-компонентов | Кадры из RES; constructor не должен перезапускать имеющуюся фазу |
| Events | Каждый owner, capacity, события, порядок кольца и cursor, deadlines/intervals/parameter | callback ID проверяется по компоненту; снимок только вне dispatch |
| GameClock/Stopwatch | ticks/remainder, started/elapsed/running | Host performance counter сбрасывается, время меню не накапливается |
| GameFlow | Исходная spawn pose, playing phase | Принимаются только живые незавершённые сессии |
| Audio | Logical VoiceSnapshot и имя ресурса | Новый SDL device; громкость текущих настроек |
| Presentation | RGB-превью | Renderer, HUD, язык, интерполяция, FPS, SDL/GL-объекты и кеши |

Поиск rand/random/RNG в исполняемых SDL2-модулях не выявил отдельного генератора;
текущие AI и снаряды определяются перечисленным состоянием. Это вывод о текущем
порте, не новое доказательство отсутствия RNG во всех ветках DOS.

## Формат v1

Один ограниченный 16 MiB файл. Magic DKSV, u32 format=1, u32 rules=2;
метаданные; GameFlow; Audio; явные поля World; SHA-256 всех предыдущих байтов.
Числа little-endian, bool u8, enum u32, индексы u64, float IEEE754 binary32;
строки и векторы имеют u32 длину. Optional — bool и поля при наличии.
Нет raw struct dump, size_t wire values, адресов или runtime handles.

Rules2 фиксирует восстановленный native OG contact/effect cursor. Публичные
сейвы r2 с rules1 продолжают читаться: OG `impact && !effect_frame && frame==0`
нормализуется в cursor1, сохраняя flight image0 до следующего callback.
Уже показанный legacy effect frame1 сохраняется как текущий визуальный кадр и
далее безопасно продолжается с frame2; прогресс и снаряды не отбрасываются.
Остальные поля и датасеты не преобразуются. Новая запись всегда использует rules2;
неизвестные версии отклоняются. Формат полей и канонический отпечаток не менялись.

Метаданные: dataset, level, UTF-8 name (до 240 байт), UTC epoch timestamp,
ticks, health/lives, SHA-256 канонических входов, размеры и RGB-превью до 320×200.
Время отображается в местном часовом поясе. Имя уровня локализуется при показе.

Канонический отпечаток использует распарсенные потребляемые поля LEVEL, карты,
декодированные изображения/анимации/звуки, игровую палитру и LIGHT. Не включает
ignored_step_ver, ignored_tail, размещение блоков и runtime-кеши RES. У OG LIGHT
выбирается тем же фиксированным именем, что и в SoftwareScene. PKR и PKE остаются
разными датасетами; смена языка интерфейса не меняет отпечаток.

## Транзакции

Запись: .tmp → flush → readback → сохранение исправного текущего файла в
.bak.tmp → flush/readback → атомарная замена .bak → атомарная замена основного.
Основной файл не удаляется перед заменой. Один процесс не должен одновременно
писать один слот из нескольких потоков. Временные файлы не загружаются.
Перед ротацией предыдущая запись проходит не только SHA-256, но и полную
семантическую проверку с выбранным RES: файл с правильной суммой и неверными
полями/версией не должен вытеснить исправную резервную копию.

Загрузка: bounded read, checksum/version, dataset/fingerprint, поля и ссылки,
независимый World и подсистемы, затем замена живой сессии. Конструкторы кадров
не должны изменять восстановленные фазы. Точка возрождения сверяется со spawn
исходного уровня, а не с позицией сохранения. При отказе текущий World остаётся.

Linux/ARM и аппаратная приёмка указываются отдельно в результатах задачи.
