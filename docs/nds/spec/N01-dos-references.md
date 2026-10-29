# N01: аудит DOS-эталонов и подготовка сценариев

28 октября 2026 года. Выполнен аудит существующих файлов без нового запуска
DOSBox, Ghidra или игрового кода. Это часть N01: реестр подтверждает доступные
доказательства и задаёт будущие проверки, а не объявляет полное прохождение DOS.

## Первичные образцы и адресация

Каждый DOS-сценарий RU использует `originals/pkr/RUS/PKR.EXE` с `PKR.RES`,
EN — `originals/pkr/ENG/PKE.EXE` с `PKE.RES`. Ожидаемые SHA-256:

| Образец | SHA-256 |
|---|---|
| PKR.EXE | `12f78afc0eba05f76b5dc0bb99da38d7daa20585024c0ba958fe828aa3864d28` |
| PKR.RES | `af7135fbcfc765c1e963682a29b108ffe90d652d96fa130cf9db6b217277b816` |
| PKE.EXE | `cce7bc5993d0734ad87e93db37a70b8b52faedfff14ac48d6e317e5ac996a37b` |
| PKE.RES | `865da340621fa6f90548a84e9ae9bf0a85071befb32a40ca1d359e488f74d458` |

Источники: `analysis/originals/inventory-{ru,en}.json`,
[описание G00](../../originals/reference-runs.md),
[воспроизведение экспорта](../../originals/reproducing.md).
Внутренние номера версий оригиналов неизвестны.

Адреса кода в дополнительных исследованиях ниже — Ghidra NE-loader.
Адреса героя/data в отчётах DOSBox — линейная гостевая память конкретного
процесса; offsets полей не являются файловыми смещениями или NE-сегментами.
DPMI-селектор нельзя превращать в линейный адрес умножением на 16.

## Что действительно сохранилось

Все пять G00-серий из `analysis/originals/reference-manifest.json` найдены в
`research/evidence/reference/<ID>/`. В каждом каталоге существуют `report.json`,
`dosbox.conf`, оба журнала, 12 пар `.idx`/`.png`, шесть `data-NN.bin` по 65536
байт и `level1-memory.bin` на 16777216 байт. Все **60 индексных кадров** совпали
по SHA-256 со своими отчётами; все пять отчётов совпали с SHA-256 из manifest.
Текст каждого конфига совпал с полем `configuration` отчёта. PNG проверены
на наличие; визуально просмотрены boot/enter-01…06 RU03 и boot EN02.
PNG не имеют отдельно записанного исторического SHA-256.

| ID | SHA-256 report.json | Изолированный конфиг | Старт / поворот |
|---|---|---|---|
| ru-01 | `beb1af39a5c2c44390e25a5cb682f2ab8f29d01582ba86c91f5fba0b3137a1b3` | До добавления двух флагов | RU, heading 90→80 |
| ru-02 | `9ae0d05cf87f356a38e0b59a5d721d08cf1f21ac507e493edc08c319ada2b943` | До добавления двух флагов | RU, 90→82 |
| en-01 | `120ebda6057338a29425de0578bbe89b03782418d3fa9c20021d71d4213360a7` | До добавления двух флагов | EN, 90→83 |
| en-02 | `be20dcd7d215bca08564469667ae1ed3c2a5fafdb198bc20db9fa8a5a314ae6a` | `--noprimaryconf --nolocalconf` | EN, 90→81 |
| ru-03 | `bba0ed05955c8025b06ab8a494130b2556e29687c8d493f802e14c0fddb8a626` | `--noprimaryconf --nolocalconf` | RU, 90→80 |

Во всех пяти отчётах `originals_unchanged=true`, EXE before/after/copy совпадают.
Ранние отчёты не содержат полного списка хешей RES и прочих файлов до/после;
их нельзя дополнять задним числом. Дополнительная историческая сверка
`research/evidence/reference/source-verification.json` существует и совпала с
manifest SHA-256 `c4e05da36c891821df583c4edda8611d7e73ed6567ca9421c2dbb21c29f02064`.

Старый manifest и строки команд внутри неизменённых исторических отчётов
ещё используют `build/reference/...` и `scripts/dos_reference.py`. После
перестройки репозитория это соответствует `research/evidence/reference/...`
и `tools/originals/dos_reference.py`. Это отображение путей, а не новое
измерение или изменение происхождения.

Текущий SHA-256 скрипта —
`904b2ab42d4630ec9edcb4228e6cc25d0c4e46c1cd243fcd6913f859f0aa2fbf`;
исторический SHA в manifest —
`62d40213e4c2cf4bcba8ae3ddf1de920d8cbe7a4bfdb255a223a5c873a4d4b5d`.
Их нельзя объявлять одной и той же побайтовой версией.

Дополнительно существуют `research/evidence/time/dos-{ru,en}/report.json`:
обе серии `complete`, по 21 `time_probe`, 8 кадров и подтверждённой стартовой
позе LEVEL1. Все **16 индексных кадров** совпали с хешами своих отчётов.
Они добавляют наблюдения времени, но не новые уровни или маршруты.
`ru-probe`/`ru-probe2` в reference — предварительные пробы режима видео;
не включать их в число пяти основных серий или приёмку игрового маршрута.

## Условия и точность существующих кадров

Стенд DOSBox Staging найден в общей установке
`%LOCALAPPDATA%/Programs/DOSBox-Staging/wasdify-webserver-20261024/dosbox.exe`.
Текущий SHA-256 совпал с историческим:
`67a907289bad3e25a9be7dd4dc38d882c1c7e74949f8daa103a52476d99f24f5`.
Версия в пяти отчётах — `0.83.0 (7b400)`. Условия:
`svga_s3`, RAM 32 MiB, normal/pentium, 60000 обычных и protected cycles,
SDL video/audio `dummy`, `nosound=true`, SB16 220/IRQ7/DMA1/HDMA5.

Ввод — запись в найденную таблицу клавиатуры игры. Это не проверка физической
клавиатуры, IRQ1 или BIOS. Шесть Enter по 0,12 с с ожиданием 1,5 с проходят
заставки/выбор уровня; затем Right 0,35 с, Forward 0,6 с, Space 0,12 с,
Ctrl 0,4 с, после каждого действия ожидание 0,5 с. Длительности — часы хоста.
Одинаковая длительность не означает одинаковое число игровых обновлений.

| Язык | Поза старта x,y,z / heading | HP / энергия / жизни / счёт | SHA-256 level1-start.idx |
|---|---|---|---|
| RU | `15120,-300,22800 / 90` | `100 / 0 / 3 / 0` | `2998ed9d6f592e16ddd662b467e6635d15272e3181d5baec09f09d4c88ae2a1c` |
| EN | `15120,-300,23280 / 90` | `100 / 0 / 3 / 0` | `72993890dfcf84a6e63f7be16f5beb881b868090c6d39bcb00d7da09b18c22a9` |

У действий есть `player_before/player_after`, содержащие pose, HP, энергию,
жизни и счёт. Они считываются отдельно от framebuffer, без атомарного снимка.
Для boot/enter кадров отдельная pose не сохранена. Между чтением памяти
и кадра игра продолжает исполняться. `cpu_final` получен после действий,
а `level1-memory.bin` до них: регистры нельзя считать одновременными дампу.

В кадровом отчёте нет подтверждённого номера presentation, scheduler tick,
фазы воды/двери/палитры или активного события эффекта. Стабильность трёх чтений
означает только одинаковые 64000 байт VGA по физическому `0xA0000`.
Настоящая VGA-палитра и звук не захвачены; PNG — серое представление индексов.
Эти кадры пригодны для индексов/геометрии статического старта, но не для
приёмки цвета, музыки или одинаковой фазы динамического эффекта.

## Матрица покрытия шести исходных миров

Обозначения: **набл.** — есть целая DOS-сессия с соответствующим наблюдением;
**часть** — только указанная часть; **нет** — требуемого динамического
доказательства нет. Срезы x86 и проверки GBA в эту матрицу не включены.

| Исходный мир | Начало | Ходьба / бой | Двери | Высоты / вода | Смерть / retry | Заставки / завершение |
|---|---|---|---|---|---|---|
| RU LEVEL1 | набл., 3 G00 + time | часть: turn/forward; боя нет | нет, только Space attempt | нет | нет | часть: boot, предыстория, меню, BINTRO1; выхода нет |
| EN LEVEL1 | набл., 2 G00 + time | часть: turn/forward; боя нет | нет, только Space attempt | нет | нет | часть: boot/шесть Enter сохранены; выхода нет |
| RU LEVEL2 | нет | нет | нет | нет | нет | нет |
| EN LEVEL2 | нет | нет | нет | нет | нет | нет |
| RU LEVEL3 | нет | нет | нет | нет | нет | нет |
| EN LEVEL3 | нет | нет | нет | нет | нет | нет |

У RU03 просмотрены: boot — титульная картинка, enter-01 — предыстория,
enter-02 — выбор трёх уровней, enter-03 — текст перед LEVEL1, enter-04 —
чёрный кадр, enter-05/06 — игра. Это отдельные остановки, а не захваченная
анимация перехода. Ctrl при нулевой энергии и без подобранного оружия
не доказывает выстрел. Одинаковые кадры после Space не доказывают дверь.

## Дополнительные доказательства и их границы

| Подсистема | Доступные первичные материалы / привязка | Что это подтверждает и чего не заменяет | Будущая задача |
|---|---|---|---|
| Часы / очередь | `research/evidence/time/{ru,en}/evidence.asm`, manifests/logs; `time/dos-{ru,en}/report.json`; [G06](../../gba/time-events.md) | Работа clock на старте: по 21 наблюдению. Нет frame sync или длительного runtime rollover oracle | N08 |
| Cadence | `research/evidence/cadence/ru/{evidence.asm,analysis-manifest.json,hashes.json,ghidra.log}`; [сводка](../../originals/original-cadence.md) | Только RU callback/presentation ASM, герой `1068:2127`, период 416; это не замер общего FPS и не повтор EN | N08, N09 |
| Камера / статический кадр | `research/evidence/rendering/{ru,en}/evidence.asm`, `reference/{ru-03,en-02}`; [сводка](../../originals/rendering-reference.md) | Геометрия начальной сцены и pose; не вся перспектива и не динамика | N06, N07, N09 |
| Двери и отражение полотна | `research/evidence/interactions/{ru,en}/`; `build/gba/g20b-playtest2/original/{ru,en}/{evidence.asm,manifest.json,ghidra.log}`; [уточнение](../../gba/ui01-playtest2.md) | BP+1C конструктора RU `10B0:03EE`, 16 исходных x86-векторов по документу; не целая DOS-дверь. Отражение полотна не равно эффекту отражения в воде | N11, N12 |
| LIGHT / высоты / вода | `research/evidence/surfaces/{ru,en}/{evidence.asm,analysis-manifest.json,ghidra.log}`; `analysis/gba/g10-original-verification.json`; [сводка](../../gba/surfaces-lighting.md) | ASM и контролируемые constructor/LIGHT/crop срезы. LEVEL2: phase 0…17 и направления 0408/0808/1008/2008 по исследованию; нет динамического DOS-кадра воды, полного отражения сцены или palette oracle | N13 |
| Map | `research/evidence/automap/{ru,en}/{automap.asm,manifest.json,ghidra.log}`, `automap/execution.json`; [ограничения](../../gba/ui01-dos-map.md) | 12 записей RU/EN×LEVEL1–3×map item, diff=0; по документу 2 окна на случай, 24 сравнения. x87/stack locals контролируются; нет oracle DOS-раскрытия visited в живой сессии | N20 |
| Flow / retry / выходы | `research/evidence/flow/{ru,en}/{evidence.asm,analysis-manifest.json,ghidra.log}`, `analysis/gba/g18-original-flow.json`; [сводка](../../gba/game-flow.md) | Report passed, `whole_DOS_session=false`. RU startup `1008:0719`, EN `1008:086D`; native death/restore и условия выхода проверены с внешними hooks. Не целая смерть или завершение уровня | N17, N22 |

Названные каталоги/отчёты существуют при аудите. Отчёты GBA читаются как
источники методов и отдельных доказательств оригинала; их ROM/host comparisons
не переносятся в NDS как готовые тесты. В частности, пауза мира на GBA-карте,
GBA-веер visited, масштабирование и фиксированная сетка времени — решения GBA.
Сохранившиеся шесть файлов DoorMirror (ASM, log, manifest RU/EN, 91889 байт)
в N01 скопированы в `research/evidence/nds/n01/door-mirror/{ru,en}/`.
Исходные файлы в `build/` оставлены; побайтовое совпадение и SHA-256 проверены,
происхождение записано в `door-mirror/preservation.json`. Упомянутый историческим
manifest `center.bin` уже отсутствовал и не восстановлен. Это сохранение
старого экспорта, а не новый анализ или полный воспроизводимый комплект опыта.

## Подготовленные сценарии RU/EN × LEVEL1–3

Ниже каждый сценарий параметризован шестью независимыми парами
`ru/LEVEL1`, `ru/LEVEL2`, `ru/LEVEL3`, `en/LEVEL1`, `en/LEVEL2`, `en/LEVEL3`.
RU и EN выбирают собственный EXE/RES, а не NDS RU-мир с переведённым текстом.
Для LEVEL2/3 нельзя подставлять стартовую pose LEVEL1. После выбора мира
его pose/объекты сначала восстанавливаются по его LEVEL и ASM, затем проверяются
в работающей игре. Динамические сценарии выполняются только при наличии
соответствующего объекта в исходном мире; отсутствие фиксируется по RES.

| ID в каждом из 6 миров | Процедура и обязательные кадры | Что записывать дополнительно | Ответственный пробел |
|---|---|---|---|
| start | Чистая копия/процесс; пройти intro и явно выбрать 1, 2 или 3; кадр BINTROn и первый стабильный PLAY | Пункт выбора, resource LEVELn/GAMEPn, pose, HP/energy/lives/score; boot/intro phase | N06, N21, N22 |
| move-wall | От стартовой pose пройти записанный маршрут к ближайшей выбранной стене; поворот, скольжение, упор | Исходная клетка/направление, press/release и ticks, pose до/после контакта, occupancy; не телепортировать | N09, N10 |
| door | На том же записанном маршруте выбрать glyph/клетки двери; Use, открывание, открытое состояние, закрывание; повтор без/с ключом, если есть замок | Object/glyph, key inventory, native state/frame, обе клетки полотна, pose, событие и callback phase | N11, N12 |
| height-water | Выбрать FCMap-клетки с разными ceiling параметрами; отдельный неподвижный ракурс воды при её наличии, затем движение у края | H/I и фактическая высота, object/frame 0…17, image flags, LIGHT near/row, palette, tick; отражение сцены проверять отдельно | N13 |
| pickup-shot-fight | Пройти к оружию/заряду, подобрать; найти конкретного врага; wake, выстрел, попадание, ответная атака, смерть врага | Item/actor identity и клетка, HP/energy/inventory до/после, projectile/event identity, animation frame, distance/pose | N14, N15, N16, N17 |
| death-retry | С записанного состояния получить штатный смертельный урон; retry с оставшимися жизнями, затем отдельный случай последней жизни | HP/lives, restored pose, двери/items/enemies/inventory/visited и очередь до/после, death sound, flow code | N17, N18, N20, N22 |
| exit | Пройти к подтверждённой клетке B2 штатным маршрутом; EINTROn, возврат выбора уровня; отдельно отмена и ENDTXT | Условия/score/total, pose и flow до выхода, ресурс/страница текста, transition phase, музыка | N21, N22 |

Для каждого кадра будущий журнал должен связывать sample SHA, чистый старт,
все действия, выбранный мир/язык, runtime data base и способ разрешения
far pointer, героя/камеру, поле фазы эффекта и источник времени. Если поле
фазы не установлено, записывать `unknown`, а не фазу 0 по умолчанию. Желателен
согласованный snapshot framebuffer/palette/state либо явная граница возможного
расхождения между чтениями. Захват полной сцены/музыки не заменять raw resource.
Для sound — отдельный audible capture с конфигом, потому что текущий стенд silent.

Полные журналы и кадры будущих NDS-исследований хранить в
`research/evidence/nds/<задача>/dos/<язык>/level<N>/<scenario>/`, компактные
итоги — по report-policy. Не сохранять повторные одинаковые дампы без причины.

## Работоспособные команды существующего инструмента

Из корня проекта, PowerShell. Следующие пять команд воспроизводят **условия
и последовательность G00**, всегда с современными флагами изоляции; это не
обещание одинаковых финальных timed pose и не восстановление исторических
команд старых трёх запусков. Каталоги должны отсутствовать. Команды приведены
для необходимости повтора; в N01 повторно не запускались.

```powershell
C:/Source/wasdify/.venv/Scripts/python.exe tools/originals/dos_reference.py ru --output research/evidence/nds/n01/dos/ru-01-repeat --actions --port 8137
C:/Source/wasdify/.venv/Scripts/python.exe tools/originals/dos_reference.py ru --output research/evidence/nds/n01/dos/ru-02-repeat --actions --port 8137
C:/Source/wasdify/.venv/Scripts/python.exe tools/originals/dos_reference.py en --output research/evidence/nds/n01/dos/en-01-repeat --actions --port 8137
C:/Source/wasdify/.venv/Scripts/python.exe tools/originals/dos_reference.py en --output research/evidence/nds/n01/dos/en-02-repeat --actions --port 8137
C:/Source/wasdify/.venv/Scripts/python.exe tools/originals/dos_reference.py ru --output research/evidence/nds/n01/dos/ru-03-repeat --actions --port 8137
```

Запускать последовательно. Занятый HTTP-порт приводит к отказу; завершается
только запущенный инструментом процесс. Новая пара clock-проб при необходимости:

```powershell
C:/Source/wasdify/.venv/Scripts/python.exe tools/originals/dos_reference.py ru --output research/evidence/nds/n08/dos/ru-time-repeat --time-probe --port 8137
C:/Source/wasdify/.venv/Scripts/python.exe tools/originals/dos_reference.py en --output research/evidence/nds/n08/dos/en-time-repeat --time-probe --port 8138
```

`--help` реально выполнен в N01 и завершился успешно. Текущий инструмент
поддерживает только Enter-последовательность LEVEL1, жёстко ищет его стартовую
pose и не имеет `--level`, маршрутов, capture palette/audio или phase snapshot.
`--enters` меняет число Enter и не выбирает LEVEL2/3. Не выдавать его за runner
всех перечисленных сценариев. До новых поздних прогонов нужно добавить отдельный
NDS research runner с выбором мира/маршрута и журналом pose/phase; интерфейс
пока не существует. Его проектирование принадлежит соответствующим задачам,
без наследования архитектуры GBA-движка.

У `tools/originals/dos_reference.py` есть явная legacy-зависимость:
`from verify_gba import png`, модуль находится в
`ports/gba/scripts/verify_gba.py`; каталог добавляется в sys.path самим скриптом.
Он использует PNG writer, не GBA-ROM и не GBA runtime. Поэтому standalone
копия одного dos_reference.py пока не самодостаточна. Это зависимость общего
исследовательского инструмента, не основание архитектуры будущей NDS-игры.
Перенос writer в общий tools-модуль требует отдельной проверки пользователей;
в N01 код не менялся.

## Повтор аудита без исполнения DOS

Команда ниже проверяет исторические хеши, наличие кадров/дампов и конфиги,
не записывает игровые данные и не требует DOSBox-процесса:

```powershell
@'
import hashlib, json
from pathlib import Path
sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
manifest = json.loads(Path('analysis/originals/reference-manifest.json').read_text(encoding='utf-8'))
frames = 0
for entry in manifest['runs']:
    root = Path('research/evidence/reference') / entry['id']
    report_path = root / 'report.json'
    assert sha(report_path) == entry['report_sha256']
    report = json.loads(report_path.read_text(encoding='utf-8'))
    assert (root / 'dosbox.conf').read_text() == report['configuration']
    assert report['originals_unchanged']
    assert (root / 'level1-memory.bin').stat().st_size == 16777216
    assert all((root / f'data-{i:02}.bin').stat().st_size == 65536 for i in range(1, 7))
    for name in ['stdout.log', 'stderr.log']:
        assert (root / name).exists()
    for frame in report['frames']:
        assert sha(root / (frame['name'] + '.idx')) == frame['sha256']
        assert (root / (frame['name'] + '.png')).exists()
        frames += 1
assert sha(Path('research/evidence/reference/source-verification.json')) == manifest['post_run_source_verification']['sha256']
print(f'OK: {len(manifest["runs"])} reports, {frames} indexed frames')
'@ | C:/Source/wasdify/.venv/Scripts/python.exe -
```

Обязательного повторения пяти стартов сейчас нет: сохранённые доказательства
согласованы. Поздние сценарии требуют подготовки runner/маршрутов и будущего
исполнения по назначенным N08–N22 задачам. Критерии этих задач нельзя закрывать
только данной матрицей или прежними GBA-тестами.
