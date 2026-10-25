# G03: ресурсы общего GBA-ROM

24 октября 2026 года. Формат контейнера версии 1 для одного картриджа
с RU/EN и общим игровым профилем. Спецификация соответствует реализации
`scripts/pack_gba_assets.py` и `include/dok/rom_assets.h` / `src/rom_assets.c`.
Проверки и размеры итоговой сборки — `analysis/packing-report.json`.
Пакет и генерируемые assembly/C остаются в ignored `build/gba/assets/`;
исходные EXE/RES в ignored `originals/` не изменяются. В репозиторий входят
код, спецификация и метаданные.

## Разделение данных

Подготовка выполняется на ПК: `analyze_runtime.py` разбирает исходные LEVEL
через проверенные C-модули и создаёт G02-контроли. `pack_gba_assets.py`
читает проверенные RES и эти JSON, строит бинарные G02-записи, применяет
общий профиль и пишет пакет.
Консоль заимствует диапазоны пакета из ROM. Весь RES, текстовый LEVEL и
`dok_runtime_storage` в GBA RAM не загружаются.

143 ресурса каждого языка, кроме текстовых LEVEL1–LEVEL3, сохраняются
адресуемыми логическими ID. LEVEL представлены разобранными бинарными
записями; исходные тексты остаются в материалах проверки и не помещаются
в рабочий пакет.
Равные байтовые payload могут разделять один диапазон пакета. Текстовые
пулы LEVEL остаются раздельными для RU и EN: наивное соединение пулов
LEVEL3 требовало бы 17607 байт при лимите одной view 16384.
Геометрия и числовой профиль игрового мира общие; язык выбирает строки
и соответствующий вариант ресурса, сохраняя один изменяемый RAM-мир.

## Контейнер версии 1

Все многобайтные числа little-endian. Смещения — беззнаковые байтовые
смещения от начала пакета, не адреса GBA, файловые адреса исходного RES,
NE-сегменты или DPMI-селекторы. Начало пакета и его адресуемые массивы
выравниваются на 4 байта. Читатель проверяет длины до создания borrowed views.

Заголовок занимает 64 байта:

| Смещение | Размер | Поле |
|---|---:|---|
| `+0x00` | 8 | Magic `DOKGBA1\0` |
| `+0x08` | 2 | Версия, 1 |
| `+0x0A` | 2 | Размер заголовка, 64 |
| `+0x0C` | 4 | Полный размер пакета |
| `+0x10` | 2 | Число уровней, 3 |
| `+0x12` | 2 | Число языков, 2 |
| `+0x14` | 4 | Число логических ресурсов |
| `+0x18` | 4 | Смещение каталога ресурсов |
| `+0x1C` | 4 | Смещение таблицы level views |
| `+0x20` | 2 | Размер записи view, 80 |
| `+0x22` | 2 | Размер записи ресурса, 32 |
| `+0x24` | 4 | ID общего профиля, 1 |
| `+0x28` | 4 | Flags, 1: little-endian и фиксированный ABI записей |
| `+0x2C` | 20 | Резерв |

Запись ресурса занимает 32 байта. Её позиция в каталоге определяет
логический ID: `index + 1`; ноль означает отсутствие ресурса.
Каталог упорядочен лексикографически по исходным байтам имён CP866,
независимо от системной локали. Один ID обозначает имя, а не язык payload.
Стабильность относится к этому набору имён и версии схемы; изменение
набора должно сопровождаться обновлением manifest и проверкой ссылок.

| Смещение | Размер | Поле ресурса |
|---|---:|---|
| `+0x00` | 16 | Исходное имя CP866 |
| `+0x10` | 4 | Смещение RUS payload |
| `+0x14` | 4 | Размер RUS payload |
| `+0x18` | 4 | Смещение ENG payload |
| `+0x1C` | 4 | Размер ENG payload |

Имена завершаются NUL и дополнены нулями до 16 байт. ID назначаются после
исключения текстовых LEVEL, поэтому они не равны исходным индексам RES.
Соответствие исходным записям и SHA payload хранится в manifest.

Байты изображений, собственных контейнеров анимации, PCM/SVM, шрифтов,
палитр и light tables сохраняют доказанный исходный формат. Форматы
описаны в [resources.md](resources.md), [animations.md](animations.md)
и [sound.md](sound.md). Упаковка не превращает сериализованные DOS-указатели
в рабочие указатели: существующие декодеры игнорируют эти поля.

## Бинарная запись уровня

Одна запись view занимает 80 байт. Данные определений и начального состояния
используют pointer-free G02-записи; C-указатели создаются только в небольшой
`dok_runtime_level`, заимствующей массивы пакета.
В таблице шесть записей: LEVEL1 RU, LEVEL1 EN, LEVEL2 RU, LEVEL2 EN,
LEVEL3 RU, LEVEL3 EN. Код языка `0` означает RU, `1` — EN.

| Смещение | Размер | Поле view |
|---|---:|---|
| `+0x00` | 16 | Восемь настроек LEVEL, `uint16[8]` |
| `+0x10` | 10 | object, actor, field, text, cell counts, `uint16[5]` |
| `+0x1A` | 2 | player index + 1; 0 при отсутствии |
| `+0x1C` | 2 | Сумма предметов категории 99, signed word |
| `+0x1E` | 2 | Резерв |
| `+0x20` | 4 | Смещение glyph lookup, `uint16[256]` |
| `+0x24` | 4 | Смещение definitions, по 32 байта |
| `+0x28` | 4 | Смещение fields, по 8 байт |
| `+0x2C` | 4 | Смещение текстового пула |
| `+0x30` | 4 | Смещение трёх карт MMap/TMap/FCMap |
| `+0x34` | 4 | Смещение initial objects, по 36 байт |
| `+0x38` | 4 | Смещение initial actors, по 64 байта |
| `+0x3C` | 4 | Смещение initial player, 60 байт |
| `+0x40` | 4 | Palette span: offset/size, `uint16[2]` |
| `+0x44` | 4 | Light span: offset/size, `uint16[2]` |
| `+0x48` | 4 | Смещение binding table |
| `+0x4C` | 2 | Число bindings |
| `+0x4E` | 2 | Резерв |

Spans в definition, field, palette и light относительны к текстовому пулу
этой view. Их native-offset хранит происхождение исследованного поля,
а не адрес памяти. Четвёртая карта visited создаётся при reset в RAM.
Строки пула — исходные байты CP866 без обязательного завершающего NUL;
длина задаётся span. Ограничение исходных реплик до 39 байт уже применено
проверенным LEVEL-конвертером, без UTF-8 внутри игрового пакета.
Один glyph соответствует одному определению; повторные размещения сохраняют
проверенное правило последней записи, а не создают копии актёра.

Header/view/resource/binding записи читаются побайтно. Для pointer-free
G02-массивов reader использует явно проверенный little-endian ABI:
`static_assert` проверяет размеры и значимые offsets, а база пакета и
каждый массив должны быть выровнены. Это контракт версии 1, а не обещание
работы произвольного C-layout на другой платформе. Упаковщик записывает
нулевые reserved/padding. Reader проверяет reserved header/view/binding,
заполнения имён, field/object/actor/player, а также допустимые known bits;
неизвестные header flags отвергаются.

Фиксированные массивы версии 1 имеют следующий layout. В таблице смещения
десятичные, `h/H` — signed/unsigned 16 bit, `i/I` — signed/unsigned 32 bit,
`B` — байт; span — два `H` (offset, size).

| Запись | Поля по byte offset |
|---|---|
| Definition, 32 | `0:H class`, `2:H flags`, `4:H sprite_flags`, `6:h width`, `8:h height`, `10:B glyph`, `11:B marker`, `12:H actor_index`, `14:H field_begin`, `16:H field_count`, `18:H placements`, `20:span sprite`, `24:span animation`, `28:span sound` |
| Field, 8 | `0:H native_offset`, `2:B kind`, `3:B reserved`, `4:i number` либо `4:span bytes` |
| Object, 36 | `0:H flags`, `2:H frame`, `4:H animation_budget`, `6:H interval`, `8:h animation_a`, `10:h animation_b`, `12:H known`, `14:H reserved`, `16:union state[20]` |
| Actor, 64 | `0:I known`, `4:i x`, `8:i y`, `12:i z`, `16:i base_speed`, `20:i step_distance`, `24:i accumulator_a`, `28:i accumulator_b`, `32:I motion_flags`, `36:h heading`, `38:h angle_34`, `40:h mode`, `42:h layer`, `44:h direction`, `46:h step`, `48:h ready`, `50:h command`, `52:h command_84`, `54:h parameter_98`, `56:h bound_9e`, `58:h bound_a0`, `60:B glyph`, `61:B previous_glyph`, `62:B reserved[2]` |
| Player, 60 | `0:h health`, `2:h energy`, `4:h health_max`, `6:h energy_max`, `8:h parameters_66_70[6]`, `20:H keys`, `22:h parameters_76_7a[3]`, `28:h selected`, `30:h pending`, `32:h lives`, `34:h score`, `36:H weapons[9]`, `54:H reserved`, `56:I auxiliary_da` |

Union Object определяется `class_id` соответствующего definition.
Смещения ниже относительны к началу 20-байтового union по Object+16:

| Вариант state | Поля |
|---|---|
| Door | `0:i travel_limit`, `4:h progress`, `6:h command`, `8:h extent`, `10:h velocity`, `12:h state`, `14:H original_flags`, `16:B phase`, `17:B enabled` |
| Switch | `0:h state`, `2:h command`, `4:h progress`, `6:h target` |
| Projectile | `0:i velocity`, `4:i y`, `8:h damage`, `10:h half_damage`, `12:h layer`, `14:h state`, `16:B previous_glyph`, `17:B reserved[3]` |
| Monster | `0:h health`, `2:h state`, `4:h course_counter`, `6:B auxiliary_e6`, `7:B course_counter_known` |
| Weapon | `0:h weapon_layer` |

Точные смысловые типы — `include/dok/runtime.h`; union сохраняет размер 20
независимо от класса. `known` отражает известность подтверждённых полей,
а нули в reserved/padding не объявляют неизвестные native-байты известными.

## Ссылки на ресурсы и зависимости

Именные spans G02 сохранены для сверки. Прямая связь с логическим resource ID
хранится отдельно в binding table; запись занимает 8 байт:

| Смещение | Размер | Поле binding |
|---|---:|---|
| `+0` | 2 | Object index с нуля; `0xFFFF` для глобального ресурса уровня |
| `+2` | 2 | Slot |
| `+4` | 2 | Resource ID |
| `+6` | 1 | Resource kind |
| `+7` | 1 | Резерв |

Slot `0xFFF0` — common sprite, `0xFFF1` — common animation, `0xFFF2` —
common sound, `0xFFF3` — palette, `0xFFF4` — light. Для class-specific
IMAGE/ANIMATION/SOUND slot равен исследованному native-offset поля.
Kind использует значения `dok_level_kind`: IMAGE=3, ANIMATION=4, SOUND=5;
для глобальных palette/light записывается 255.
Таким образом одинаковое имя может иметь несколько мест использования,
сохраняя один логический ID. Все ссылки разрешаются offline; отсутствующее
имя является ошибкой упаковки, а не ссылкой на первый ресурс архива.

## Общий профиль и локализации

Профиль 1 следует принятому пользователем
[gba-balance-proposal.md](gba-balance-proposal.md). Его выбор — отдельное
дизайнерское решение; он не объявляется восстановленным исходным вариантом.
Базой общей геометрии остаются RUS-карты всех трёх уровней. Отличающиеся
английские участки сохранены в исходных RES и материалах проверки для
сравнения и дальнейших испытаний; языковой переключатель не применяет их
к текущему миру.

Предписанные отличия LEVEL1: бояре `81/86` используют ENG скорость 20
и активацию 9000 при HP 42; боярин `8C` — ENG HP 12; период курса бояр —
ENG N24; мыши — RUS N8; Баба-яга `A0` — гибрид HP 54, скорости 60,
масштаба 7 и активации 3400. Масштаб предметов оружия берётся из RUS там,
где он равен 4, а ENG — 3. Это исходные единицы параметров; изменение частоты AI и перевод
в игровое время относятся к следующим этапам.

Фактические before/after значения, исходная локализация каждого выбора,
SHA LEVEL и числовые метки изменённых полей фиксируются manifest упаковщика.
`sources` содержит SHA/размеры EXE, RES и исходных LEVEL; `runtime_inputs` —
SHA шести проверенных G02 JSON; `profile_document_sha256` — SHA принятого
документа баланса. Упаковщик требует совпадения G02 inputs с
`analysis/runtime-verification.json` и проверяет исходные EXE/RES до и после
работы. `profile_changes` содержит 39 применений числового профиля LEVEL1.
`source_differences` каждого уровня сохраняет RU/EN/chosen для исходных
числовых различий и список изменённых клеток карт с отложенной ENG-политикой.
Проверены 85 числовых различий и 87 клеток карт. Индексы `cell` в этом
manifest относятся к отражённым и нормализованным G02-картам в порядке
MMap/TMap/FCMap; это не номера байтов или строк в исходном текстовом LEVEL.
Сохранение неизменённого RUS-значения — также выбор происхождения, даже
когда оно не создаёт числовой delta. Русская позиция героя LEVEL1 остаётся
общей для обоих языков; переключение языка не является reset уровня.

`dok_rom_switch_language(runtime, view, assets, language)` требует,
чтобы `runtime->level == view`. Читатель создаёт и проверяет новую view,
проверяет общность карт и initial objects/actors/player по разделяемым
ROM-адресам, равенство настроек, счётчиков, glyph lookup, первых 20 байт
каждого definition и всех числовых class fields,
затем заменяет caller-owned view на месте. Позиции, здоровье, оружие,
изменённые карты и остальное RAM-состояние сохраняются. View и пакет должны
жить дольше runtime; перемещать view после привязки нельзя. Начальное
создание мира выполняется отдельно через `dok_runtime_reset`.

`dok_rom_open` полностью проверяет каталог, шесть views и их resource links
без heap. Имена ищутся двоичным поиском с учётом регистра исходных байтов.
Bindings отсортированы по `(object_index, slot)` и уникальны; reader сверяет
ID с соответствующим именным span и наличием payload выбранного языка.
`dok_rom_level` копирует настройки и glyph lookup в caller-owned view,
а большие массивы заимствует из неизменяемого ROM. Выходные значения API
при ошибке не заменяются. SHA проверяется offline; reader на консоли
проверяет структуру, диапазоны и ссылки, не вычисляя SHA всего картриджа.

Последовательность инициализации: `dok_rom_open`, затем `dok_rom_level`
для выбранного уровня/языка, затем `dok_runtime_reset`. Каждый status
проверяется до следующего вызова. Для последующей смены языка вызывается
только `dok_rom_switch_language` с теми же assets/view/runtime.
В C API номер уровня начинается с нуля; диагностический CLI принимает
LEVEL1–LEVEL3 как номера 1–3. Полученный resource ID передаётся в
`dok_rom_resource_at`; возвращённые `data/size` указывают прямо в ROM
и могут быть переданы существующим декодерам изображения, анимации или PCM.

## Воспроизведение и проверка

Команды выполняются из корня проекта с общим Zig/Python из
[building.md](building.md):

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build-gba-assets.ps1 -CrossChecks
```

Та же последовательность отдельными командами:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build-native.ps1 -CrossChecks
C:/Source/wasdify/.venv/Scripts/python.exe scripts/analyze_runtime.py
C:/Source/wasdify/.venv/Scripts/python.exe scripts/pack_gba_assets.py
C:/Source/wasdify/.venv/Scripts/python.exe scripts/verify_packing.py
build/native/dok-rom-assets.exe build/gba/assets/assets.bin 1 ru
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build-gba.ps1 -AssetsProbe -OutDir build/gba-assets -Verify
```

`--output <dir>` меняет каталог упаковщика, `--runtime-dir` — каталог G02
контролей, `--ru-res/--en-res` — пути исходных архивов с обязательным SHA.
По умолчанию создаются `assets.bin`, `manifest.json`, `assets.S`, `assets.h`.
Assembly использует `.incbin` в read-only секцию с `.balign 4`; символы —
`dok_gba_assets`, `dok_gba_assets_end`, `dok_gba_assets_size`.
`--emit-c` дополнительно создаёт `_Alignas(4) const` C-массив.
В VS Code доступна задача **DoK: build and verify bilingual GBA asset ROM**.
Детерминизм проверяется для пакета и manifest; generated assembly содержит
путь к выбранному `assets.bin`, поэтому этот путь зависит от `--output`.

Проверены два независимых запуска упаковщика: `assets.bin` и `manifest.json`
побайтно совпали. Пакет содержит 2 763 046 байт, SHA-256:
`17fb976d2b0d87cc428fe3cb280f4baff21ff2c986b651246f57be4c59a9eff8`.
В каталоге 143 ID и 286 проверенных исходных языковых вариантов. Шесть views
имеют 642/654/941 bindings для LEVEL1/2/3 в каждом языке. Независимый parser
проверил 343 диапазона с 178 уникальными диапазонами, выравнивание 4 и
отсутствие частичных перекрытий; точное совпадение диапазонов разрешает
разделять одинаковые данные.

`verify_packing.py` независимо строит ожидаемый общий профиль из исходных RES,
сверяет все карты, числовые поля, локализованные строки, известные native-байты
и resource bindings с binary parser и C reader. Проверены 23 повреждённых
реальных пакета: обрезание, версия/размеры/диапазоны, alignment, ёмкости,
неверные ID/имена/kind, расхождение числового профиля и shared initial state
между RU/EN. Reader отверг каждый. C-тесты дополнительно переключают все
три настоящих уровня в обе стороны, сохраняя изменённые здоровье, инвентарь,
карты и резервные RAM-слоты побайтно. Пройдены девять Windows-тестов;
Linux executable и Thumb objects скомпилированы, Linux-исполнение не выполнялось.
EXE/RES после проверки не изменились.

## Выполнение общего ROM на GBA

`build/gba-assets/` содержит ELF, LLD map, ROM, build-report и PNG.
ROM размером **2 776 104 байта** включает весь пакет; SHA-256:
`bc8663208754e53eb0588fb78b78b92749d0e53180c8ffa2f10c245478a281cf`.
Статические `.data + .bss` занимают **34572 байта EWRAM**: здесь действительно
выделены runtime-мир, небольшие views и диагностика, а большие данные
заимствуются из ROM. Это другая сборка, чем G02 memory-probe с массивами
резервов будущих подсистем; её меньшую `.bss` нельзя принимать за новый
бюджет всей игры.

В режиме `-AssetsProbe` linker исключает верхние **8 KiB IWRAM**
`0x03006000..0x03007FFF` под стеки и BIOS. Это учитывает локальные view
в reader; System SP остаётся `0x03007F00`, IRQ SP — `0x03007FA0`.
Default G01/G02-сборки сохраняют прежний резерв 1 KiB. Глубина фактического
стека не измерена; резерв не является результатом high-water проверки.

Отчёт [g03-gba-verification.json](../analysis/g03-gba-verification.json)
фиксирует реальное выполнение в mGBA `0.11-212-7a12d6d`:

- Пакет в памяти cartridge побайтно равен offline `assets.bin`.
- Все шесть original-derived views прочитаны, проверены и сброшены через
  настоящий `dok_runtime_reset` на ARM.
- До переключения изменены здоровье и visited; RU→EN→RU сохраняет все
  33864 байта runtime. Проверка через SELECT дополнительно сравнивает
  RAM-мир побайтно, а локализованный текстовый пул меняется и возвращается.
- После reset с предварительным заполнением выделенной EWRAM `0xA5`
  проходят повторная инициализация пакета и шести views.

Экран probe показывает `LANG RU/EN`, аппаратные счётчики и кнопки;
скриншоты `assets-ru.png`, `assets-en.png`, `assets-ru-return.png` просмотрены.
Сами переведённые реплики, intro и игровой HUD этим экраном ещё не рисуются;
полноценный локализованный интерфейс относится к G17.
Общий пакет и переключение языка выполнены; renderer уровня, GBA-аудио,
игровой цикл, оценка принятого баланса и цель 30 FPS относятся к следующим
этапам. На физической GBA эта сборка пока не проверена.
