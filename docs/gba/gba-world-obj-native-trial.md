# Native гибрид с прямыми world OBJ: не выбран

27 октября2026, G20A. Рабочий игровой прототип выводит часть стен и masked
объектов исходными текстурами через PPU, сохраняя программный слой для потолков
и fallback. Он прошёл описанные ниже проверки, но **на всех пяти тяжёлых видах
медленнее2775**. Локальную доводку этой ветки прекращаем. Следующий главный
опыт — настоящий sector clip/span pipeline. Performance ROM в текущий goal
не входит; goal/G20/G20A остаются открыты.

## Конвейер и сохранённые свойства

`ports/gba/src/renderer_world_obj.inc` вызывается в прежнем ближнем порядке вертикальных
граней, до обычного CPU raster. Стены с перспективой разбиваются на полосы
исходника16×32; по обратному Q7 U вычисляются экранный центр и локальные
диагональные affine matrices. Постоянная матрица в пределах полосы —
приближение перспективы, а не точная проективная трансформация. UV crop,
слишком крупное увеличение, отсутствующие ROM columns и исчерпание ресурсов
оставляют соответствующие части на CPU. Двери с crop/animation frames без
статических columns не объявлены аппаратно ускоренными.

Источник сохраняет исходные размеры, palette indices и все16 LIGHT rows.
`ports/gba/scripts/gba_world_obj_assets.py` строит lossless tiles и дедуплицирует их;
эффективная прозрачность учитывает **image.stored_flags | definition.sprite_flags**.
Неоднозначное использование одного ресурса с разной прозрачностью получает
fallback. Банк масштабов выключен, mip/процедурные материалы не введены.
Высоты пониженных потолков, планировка, draw distance, AI и gameplay не менялись.

Видимость программного слоя и OBJ связана через coverage. Для полностью
непрозрачного tile отмечается интервал; masked tile проверяет native footprint
логического пикселя, а смешанные граничные клетки дорисовывает CPU. Старший
программный пиксель закрывает world OBJ; OAM порядок упорядочивает сами OBJ.
CPU fallback пропускает занятые участки. Это сохраняет перекрытия, но требует
дополнительной подготовки, а частичный offload не устраняет весь старый UV/setup.

Есть32 резидентных texture slots по512B в верхних16KiB OBJ VRAM. Нижние16KiB
несут CPU120×128. Ключи/загрузки переиспользуются между кадрами, texture DMA
выполняется только при замене slot. CPU слой использует четыре HBlank-reused
OBJ slots4..7/matrix0; matrix1 reserved под DMA padding. World slots8..127,
matrices2..31. HUD/оружие остаются на BG0, базовые плоскости — на BG2.

Включён DISPCNT bit5, необходимый для HBlank OAM access. Бюджет954 cycles/строку
проверяется консервативными8-line buckets, с резервом CPU полос и поиска OAM.
Слишком дорогие patches не выдаются PPU и остаются CPU. DMA3 upload защищён
от VBlank rearm, оценка окна выгрузки учитывает новые ROM texture uploads.
Предел и условия доступа: [GBATEK, OBJ и OAM](https://mgba-emu.github.io/gbatek/).

## Авторитетные замеры

`research/experiments/g20a-world-obj-native-final/`, native-clock mGBA, WAITCNT4317, реальный
PPU, PCM22050Hz, без overclock.5 тяжёлых frozen видов,6 прогревочных и6 измеренных
предъявлений; unprofiled timing, hold выключен во время измерения.

|Вид world/cell/heading|Контроль FPS|OBJ FPS|Расход CPU к контролю|
|---|---:|---:|---:|
|L1/520/225|9,95|4,27|2,67×|
|L1/3640/225|11,95|7,47|1,49×|
|L2/1592/135|9,95|4,98|2,16×|
|L2/2871/225|9,95|2,49|4,41×|
|L3/1544/315|14,93|6,64|2,54×|

Короткие live24 follow-camera RU/EN:12,36/8,33/11,20FPS против19,37/19,11/27,57
у2775 на том же коротком протоколе. Это не turn120/walk120 и не прохождение
уровня. Time-conservation error0, PCM underruns0; motion debt до406 ticks
сохраняется, что не является выполнением бюджета симуляции.

Прогретый texture cache во всех пяти целевых видах выгружает **0 новых texture
bytes**. Отрицательный результат нельзя объяснять постоянной загрузкой текстур.
Виды реально используют11–16 OBJ patches и аппаратно покрывают примерно
500–1600 логических отсчётов, но CPU часть кадра остаётся значительной.

Инструментированный профиль **без forced PCM** отделяет setup/admission/OAM
от coverage. Setup стоит0,37–3,46M cycles, opaque coverage0–0,083M, masked
coverage0,14–0,96M. На L2/2871 setup3,46M + masked0,96M при старом raster,
который продолжает обслуживать остаток сцены. Эти суммы нельзя подставлять
в unprofiled PCM FPS; они объясняют структуру расхода именно этой реализации.

Полный новый sweep440 не запускался: пять тяжёлых видов уже дали основание
не продвигать этот вариант. Чистый renderer только на OBJ или GPU output из
секторного clip-представления этим опытом **не опровергнуты**. Для них нужно
устранить саму необходимость текущего CPU coverage, а не полировать этот sampler.

## Проверки, исправления и ограничения

`ports/gba/scripts/verify_gba_world_obj_native.py`:40 поз RU/EN, включая шесть явных
Pause/Mode4/Resume циклов. Проверены точные hashes исходной projected geometry
и visibility leases против2775, scratch layout/вся таблица строк, residency
ROM→VRAM, OAM/matrices, DISPCNT bit5, guards и независимый scanline upper bound
(максимум938 из954).1 091 448 foreground PPU pixels проверены по независимой
композиции UI/CPU/OBJ;36 308 показаны непосредственно из OBJ при прозрачном CPU.
Coverage имеет реальный пиксель-носитель, а не только выставленный бит.

18 парных PPU кадров: HUD/оружие exact, до449 RGB pixels сцены отличаются.
Парные native240×160 PNG просмотрены; texel phase/ступеньки отражают affine
приближение. Приёмка движения на физической GBA не выполнена. Функциональные
AI/combat/items/water/whole-game suites именно новой ветки не заявлены.

Ранние V1–V8 **заменены** финальной сборкой. В общей области старых intervals
Mode4 мог перезаписать first/last screen-row maps, не затронув magic в начале
scratch. Native путь ошибочно считал состояние целым: coverage мог ссылаться
на пустой экран, искажая также время. Теперь Mode4 сбрасывает owner pointer,
следующий native draw создаёт всё состояние заново; вся таблица проверяется
в каждой из40 поз. Ранние FPS не служат основанием выбора/отказа. Также
исправлен asset opacity lookup, который сначала учитывал только image flags.

Отдельный [source-art PPU probe](gba-world-obj-bringup.md) повторён с правильным
HBlank access:54/56 exact, два non-multiplex pressure failures по100pixels
сохранены явно. Его прежняя V4 проба без DISPCNT bit5 не используется как
доказательство допустимости HBlank режима на GBA.

## Воспроизведение и передача

ROM `807f3e295143d5f01d97b5e262960543f160996800ddb719bce811f78663916c`,
25929584B, tile bank9752576B. EWRAM261424/free720; IWRAM24072/free504 до отдельного
8192 stack reserve. Наблюдаемый main2528/margin5408, IRQ80/margin80.
State помещается в зарезервированном4KiB tail covered[], static cache явно
ограничен перед ним. Чистое повторение `research/experiments/g20a-world-obj-native-repeat/`
воспроизвело новый ROM, probe и2775 побайтно. Прежние128/96/72 artifacts сохранены.
Нового игрового artifact/default нет.

```powershell
& C:/Source/wasdify/.venv/Scripts/python.exe ports/gba/scripts/prototype_gba_bsp.py --out research/experiments/g20a-world-obj-native-final --variants hw_v128_world_obj hw_v128_entity_rect hw_v128_world_obj_probe
& C:/Source/wasdify/.venv/Scripts/python.exe ports/gba/scripts/verify_gba_world_obj_native.py --build-dir research/experiments/g20a-world-obj-native-final/hw_v128_world_obj --reference-build research/experiments/g20a-world-obj-native-final/hw_v128_entity_rect --out research/experiments/g20a-world-obj-native-final/qa
& C:/Source/wasdify/.venv/Scripts/python.exe ports/gba/scripts/report_gba_world_obj_native.py
```

Сводка и SHA всех evidence: `analysis/gba/g20a-world-obj-native.json`. Вход — тот же
GBA asset package SHA3efa5d…4351; offsets относительны package, координаты
мировые/экранные GBA-порта и MMIO GBA. Оригинальные EXE не анализировались
и не изменялись. Следующий шаг — sector top/bottom clip + shared planes/spans;
не возобновлять локальный tuning проигравшего гибрида. После выбора/разумной
оптимизации архитектуры текущий goal завершается итоговым handoff с расходами,
без реализации отдельной Performance-сборки.
