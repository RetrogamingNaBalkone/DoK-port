# UI01: второй проход пользовательского плейтеста

27 октября 2026 года. ROM `artifacts/gba/DoK-port_UI01.gba`,16012100 байт,
SHA-256 `ca27ff500f799d2391eba1f8e4ed1550184a07bda2a38ca66a501d482e0a3a4a`.
ROM/ELF/map, PNG, WAV и журналы — `build/g20b-playtest2/`;
чистый повтор `build/g20b-playtest2-repeat/` совпадает побайтно.
Сводка с хешами — `analysis/g20b-ui01-playtest2.json`.

## Копирайт и текстовые экраны

Исходный `ЗАСТАВКА_СПР` обеих RES содержит одноцветный NewCom (индекс196),
но многотональный год1995: в x58…80,y192…198 есть196,244,246,251 и другие.
Это проверка пикселей ресурса, не оценка заметности ореола на DOS-мониторе.
Теперь в области года оставлено исходное светлое ядро196, прочие оттенки
заменены чёрным. NewCom и © сохранены исходными81×11 пикселями.
Одноцветный год — адаптация GBA; оригинальные RES не изменены.

BINTRO/EINTRO/ENDTXT: заголовок x8,y4, текст x8,y24,28 столбцов×14 строк.
Прежний x0 перекрывал трёхпиксельную рамку. Пагинация сохраняет все байты
и пустые строки; все14 ресурсов RU/EN проверены независимым FONT8 oracle.
PNG страниц — в `ui-check/` сборки.

## Двери: пропущенное отражение оригинала

Прежнее утверждение, что DOS тоже сдвигал оба полотна одинаково, неверно:
оно опиралось только на image crop. Каждый glyph имеет общий объект/фазу,
но **каждая клетка — отдельное полотно**. Делить спрайт пополам не требуется.

Адреса здесь — **Ghidra NE loader**, не файловые offsets и не DPMI-селекторы.
Для theta0 PKR `1098:49CC…49E7` передаёт последним аргументом1,
`4A5C…4A77` —0; вызовы EN `1030:5405/5508`. Для theta90:
RU `1098:4E42…4E5D/4ED2…4EED`, EN `1030:5938/5A3B`.
Выбор1 определяется стеной класса2 к западу (theta0) / северу (theta90).

Конструктор RU `10B0:03EE…04DF`, EN `1040` (вход0000) читает `BP+1C`:
при1 меняет местами экранные X/Y концов, сохраняя порядок полей глубины.
Far this — BP+6, входная точка — far pointer, RETF, стек очищает caller.
Проверены PUSH, signed IMUL/SAR/IDIV и порядок полей. Crop0x48 сдвигает
текстуру в её координатах; отражённое полотно движется в обратную мировую
сторону. В C восстановлены обратные U центральных плоскостей классов4/7
у соответствующей стены. Scheduler/фаза/замки/occupancy не менялись.

Read-only экспорт: `InspectDoorMirror.java`, `analyze_door_mirror.py`,
`build/g20b-playtest2/original/{ru,en}/`. SHA-256 EXE до/после совпали:

- PKR: `12f78afc0eba05f76b5dc0bb99da38d7daa20585024c0ba958fe828aa3864d28`.
- PKE: `cce7bc5993d0734ad87e93db37a70b8b52faedfff14ac48d6e317e5ac996a37b`.

`verify_door_mirror_original.py` исполнил исходный конструктор на x86:
16 сочетаний RU/EN×theta0/90×heading0/90/180/270. Обойдён только служебный
инициализатор массива; caller предоставляет обнулённую запись. Это проверка
инструкций, не целая DOS-сессия и не доказательство полного совпадения всей
перспективной растеризации. mGBA снял фазы0/7/13/20/29 пары d LEVEL1:
при фазе13 обе внутренние области точно показывают фон открытого прохода,
обе внешние сохраняют текстуру. PNG `ui-check/playtest-door-*.png`.

## Карта на Select

PLAY → Select → MAP (состояние7), B/Select возвращают в игру.
Start+Select открывает паузу, включая Select-first. Кнопки заблокированы
до отпускания. Язык не меняется. Часы, движение, AI и взаимодействия
остановлены, музыка меню не включается. Справка RU/EN обновлена.

Карта64×64 по2×2 пикселя показывает героя/направление, стены и состояние
дверей. Используется существующая visited-plane, нового framebuffer нет.
Целочисленный веер до10 клеток раскрывает комнату без дыр; стены/закрытые
двери останавливают лучи. Он вычисляется при входе в другую клетку;
сброс visited инвалидирует кеш. Предмет `map_revealed` открывает весь план.

Компоновка и веер раскрытия — **адаптации GBA**. DOS помечает клетки по
построенным видимым render records (RU1098, visited DS9396, сохранённый
`world_build_render_geometry`). Полное восстановление его automap UI,
условных символов и точной границы раскрытия здесь не заявляется.
Раньше UI01 не имела доступного экрана карты и игнорировала Select в PLAY.

## Проверки и команды

Host Windows, Linux/Thumb compile, ARM vectors, UI/flow/audio/time,
отдельная time-selftest ROM, sprites/water/latency прошли. Host проверяет
раскрытие комнаты, стену и reset кеша. Шесть world/language случаев проверяют
карту, удержание Select, B/Select, оба порядка chord, часы и предмет карты;
дополнительно8 фаз однокадрового Select в игре. Guards целы.
Музыка записана на двух периодах; WAV сохранён.

294 аппаратных перехода/5008 callbacks:0 смешанных кадров,18 целых чёрных
при смене tiled/bitmap layouts. Из396 PPU-поз отражение изменило260,
136 совпали. Отдельный `--unreflected-control` отключает только эту ветку
в приватном C: **396/396 совпадают с предыдущей UI01**. Production-исходник
при этом не меняется; контроль не публикуется как игровая ROM.

EWRAM261436/free708, IWRAM24424/free152 до отдельных8KiB стеков.
Main2584/margin5352, IRQ80/margin80. G20 budget открыт; физическая GBA,
оригинальный BIOS и полный DOS-плейтест не проверены.

```powershell
C:/Source/wasdify/.venv/Scripts/python.exe scripts/analyze_door_mirror.py
C:/Source/wasdify/.venv/Scripts/python.exe scripts/verify_door_mirror_original.py
C:/Source/wasdify/.venv/Scripts/python.exe scripts/build_gba_frontend.py --out build/g20b-playtest2 --verify
C:/Source/wasdify/.venv/Scripts/python.exe scripts/verify_ui_playtest.py --build-dir build/g20b-playtest2/hardware/hw_v128_entity_rect --report build/g20b-playtest2/playtest.json
C:/Source/wasdify/.venv/Scripts/python.exe scripts/build_gba_frontend.py --out build/g20b-playtest2-repeat
C:/Source/wasdify/.venv/Scripts/python.exe scripts/prototype_gba_bsp.py --base build/g20b-playtest2/base --out build/g20b-playtest2/unreflected-control --variants hw_v128_entity_rect --unreflected-control
C:/Source/wasdify/.venv/Scripts/python.exe scripts/benchmark_gba_hardware.py --build-dir build/g20b-playtest2/unreflected-control/hw_v128_entity_rect --reference-build build/g20b-fixes/hardware/hw_v128_entity_rect --report build/g20b-playtest2/control396.json --languages 0 1 --details
C:/Source/wasdify/.venv/Scripts/python.exe scripts/benchmark_gba_hardware.py --build-dir build/g20b-playtest2/hardware/hw_v128_entity_rect --reference-build build/g20b-fixes/hardware/hw_v128_entity_rect --report build/g20b-playtest2/render396.json --languages 0 1 --details
C:/Source/wasdify/.venv/Scripts/python.exe scripts/report_ui_playtest.py
```
