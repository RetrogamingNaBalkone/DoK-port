# Предметы, характеристики и инвентарь (G12)

Проверено 26 октября 2026 года. Общий GBA-ROM: `build/gba/g12/`.
Перенос — `ports/gba/src/items.c`, `ports/gba/include/dok/items.h`; production-вызов перед
`dok_interactions_zone`/шагом движения в `ports/gba/platform/movement_probe.c`. Renderer ничего
не подбирает. Интерфейс сообщений/звука/HUD — синхронные scalar notifications,
потребитель текста/звука будет подключён в G16/G17.

## Образцы и адресация

PKR.EXE SHA-256 `12f78afc0eba05f76b5dc0bb99da38d7daa20585024c0ba958fe828aa3864d28`,
PKE.EXE `cce7bc5993d0734ad87e93db37a70b8b52faedfff14ac48d6e317e5ac996a37b`.
`tools/ghidra/analyze-items.ps1` проверяет хеши до/после Ghidra. Проекты остаются
`research/ghidra/projects/{ru,en}/Kremlin.gpr`; свежие листинги/автоматический C/actor bytes,
журналы и manifest — `research/evidence/items/{ru,en}/`. `InspectItems.java` читает
существующие функции actor-сегмента. Псевдокод не включается в сборку.

Ниже **адреса NE loader Ghidra**, не файловые смещения, не индексы NE-сегментов,
не runtime-селекторы DPMI. Native+offset относится к original byte object,
не к 36-байтовому compact object. Исходные EXE/RES не изменялись.

| Назначение | RU | EN |
|---|---|---|
| Изменение stats | 1068:1CC2 | 1020:2276 |
| Регистрация оружия | 1068:1EEC | 1020:249C |
| Игровой callback: pickup→zone→movement | 1068:2127 | 1020:26D7 |
| Получить glyph текущей map cell | 1068:1435 | 1020:193F |
| Перезаписать glyph текущей map cell | 1068:1512 | 1020:1A1C |
| Item message/sound и решение об исчезновении | 1068:2F8A | 1020:362D |
| Reader class3 | 1068:2FE9 | 1020:368C |
| Reader class10 (слот оружия) | 1068:2C7C | 1020:331F |

ABI проверен по листингу: `__cdecl16far`, возврат `RETF`, caller снимает
аргументы со стека. Far this — offset:segment (два слова), category/value —
16-битные слова; stats читает `[BP+6]`/`[BP+8]` this, `[BP+A]` category,
`[BP+C]` value. Пролог сохраняет DS и загружает DS=SS. Native pointers/vtable
не перенесены: compact inventory хранит object-index+1, 0 означает отсутствие.

## Проверенные эффекты

Во всех шести LEVEL 11 определений class3/10. Все реально используемые предметы
стоят в **TMap**, ни одного в MMap/FCMap. Полный список клеток, исходные flags,
ресурсные ссылки, сообщения CP866 и различия — `analysis/gba/g12-item-inventory.json`.
В каждом языке 626 размещений; 29 размещённых видов «уровень × glyph».

| Glyph | Эффект | Количество LEVEL1/2/3 |
|---|---|---|
| X/Y/Z | OR ключей 1/2/4, stat10 | по 1/1/1 каждого |
| @ | здоровье +10, stat0 | 22/27/0 |
| 8 | здоровье +15, stat0 | 4/10/78 |
| ) | общий заряд +50, stat1 | 65/62/100 |
| * | здоровье −10, stat0 | 80/55/92 |
| ! | здоровье −15, stat0 | 14/0/0 |
| 9 | показать карту, stat200 | 1/1/0 |
| W/V | максимум общего заряда +50 и заполнение; оружие slot1/2 | по 1/1/1 каждого |

Проверка RU/EN сделана явно: category/value/слоты/flags и клетки предметов
совпадают. Переводы и масштабы не объединены. Общий пакет по-прежнему использует
существующий balance manifest и русский базовый мир; переключение языка меняет
презентацию с сохранением mutable inventory. G12 не меняет профиль баланса.

| Category | Native field | Проверенное правило |
|---|---|---|
| 0 | player+5E/62 | текущее/макс. здоровье |
| 1 | player+60/64 | текущий/макс. общий заряд |
| 2/3 | player+62/64 | увеличить максимум, заполнить текущее до него |
| 10 | player+72 | битовое OR ключей |
| 99 | player+D8 | счётчик с ADD16 |
| 200 | world+0C | записать1, значение аргумента игнорируется |

Для 0/1 положительный value при current==maximum возвращает0, предмет остаётся.
Иначе выполняется **ADD16 с переполнением**, затем signed comparison: если
результат<0, записать0; иначе если результат>maximum, записать maximum.
Например 32760+20→−32756→0. Отрицательное изменение при health0 принимается,
даже если значение осталось0. Нулевое изменение принимается. Для 2/3 максимум
тоже ADD16, но ограничения нет: отрицательный максимум заполняет отрицательное
текущее. Это восстановленная арифметика, не предложенный баланс/исправление.

Native fallback выполняет `player+5E+category*2 += value`; проверенные compact
поля 4–9 и 12–14 поддержаны с именами offsets без выдумывания назначения.
Категория11 (native+74 отсутствует в compact) и произвольные неизвестные
категории отклоняются. Это явная policy безопасности; такие предметы отсутствуют
во всех LEVEL. 100/101/102 и FINISH1000 остаются в G09 interactions, где
перемещение проходит проверку коллизий, а FINISH выдаёт условие для G18.
Инвентаризация FCMap всех шести исходных LEVEL подтвердила: реально
размещённые class12 зоны имеют только category100/102 и1000. Зон здоровья или
заряда нет. Неиспользуемый `DOK_INTERACTION_STAT` остаётся диагностическим seam;
он не подключается к inventory без нового реально используемого типа зоны.

## Подбор и shared glyph

Native RU2127 сначала читает текущую TMap cell. Class3: stats accepted→virtual
method+18→если true, пробел в этой TMap cell. Class10: stats без проверки
возврата→тот же method→пробел→регистрация оружия. Метод2F8A показывает сообщение
из native+58, вызывает звук и возвращает `category!=1000`.

Не меняются flags/shared definition/frame или другие клетки того же glyph.
После успешного подбора повторный scan пустой клетки ничего не делает. При
отказе положительного пополнения предмет можно подобрать после расхода ресурса.
Предметы отрицательного здоровья расходуются и при health0. Glyph99 score
среди размещённых предметов нет; переходы уровня/смерть — G18.

Оружие: native+81 — номер слота; class10 всё равно пополняет max charge при
повторной регистрации. Существующий slot не заменяется, pending_weapon
обновляется. Первое оружие выбирается сразу при пустом текущем slot. Если
оружие уже выбрано, переключение через анимацию убирания/доставания относится
к G13: G12 сохраняет pending slot, не симулирует completion визуальной анимацией.

Компактный `dok_runtime_player` остаётся60 байт, ARM runtime33864, Object36.
Прежний reserved word player+54 теперь `map_revealed`: per-level flag вместо
native world+0C. Начальный serialized word остаётся0, ROM format не менялся.
Reset очищает карту/инвентарь из initial state, язык их сохраняет.

Уведомления STAT/PICKUP/WEAPON/MAP не отображают текст и не играют звук.
PICKUP содержит object ID и эффект: G17 может найти raw CP866 span+58, marker,
звук в borrowed definition. STAT category2/3 даёт новый максимум, он же current.
Непринятое пополнение не выдаёт уведомлений. Интерфейс не выделяет память.

## Воспроизведение и границы проверки

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File ports/gba/scripts/build-gba-items.ps1 -CrossChecks -OriginalEvidence
```

`verify_items_original.py` исполняет 5274 набора исходных x86-инструкций stats
RU/EN и16 вызовов native item callback; перехвачены только HUD/sound far calls.
C-выход сравнивается с исполненными инструкциями, включая signed boundaries,
overflow, keys, score и map flag. Это не полноценная DOS-сессия. HOST suite
дополнительно проверяет rejected/repeat/shared glyph/duplicate weapon и sink.
Linux static и ARM Thumb модули компилируются; Linux execution не проверялся.

ARM-проверки в `analysis/gba/g12-items-gba.json`: независимый oracle player state,
положительные/отрицательные/overflow stats, каждый реально размещённый вид всех
уровней в RU/EN, удаление одной клетки и сохранность общего объекта/других клеток,
повторный scan. Production416-tick hook и исчезновение видимого billboard
проверяются отдельно. Mailbox вызывает те же wrappers/context/sink, что gameplay.
PNG и ELF/ROM/map остаются в `build/gba/g12/`.

ROM32851924 байта, SHA-256
`1a49c7694cc3ca1fbf7fa1439c7597f173c4c74224f4611c71489fd1a4884850`.
Static EWRAM218416, свободно43728; IWRAM23684, до8KiB stack reserve892.
Измеренный main stack2376, нетронутый запас5560 в renderer/reset сценариях.
Полный стартовый кадр4653188…4673215 cycles; сравнение с G11 без вывода о
достигнутой производительности всей игры. Новые items functions лежат в ROM.

Регрессии renderer/язык/poison reset/guards, doors/sensors/conveyor/FINISH и
rational clock — `analysis/g12-*-regression.json`. Renderer не менялся, полный
1455-camera sweep G11 не повторяется. Цель30FPS не объявляется достигнутой:
дальнейшая оптимизация остаётся G20. Физическая GBA и оригинальный BIOS,
полная DOS-сессия и CMake/MSVC execution не проверялись.

Следующий ID — **G13**. Входы: inventory/stats API, pending slot,
`docs/originals/weapon-parameters.md`, positioned instances/visual playback из G11,
`research/evidence/items/{ru,en}/automatic.c`/листинг с оригинальной цепочкой регистрации.
