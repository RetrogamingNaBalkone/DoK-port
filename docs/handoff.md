# Передача контекста между чатами

Обновлено 25 октября 2026 года: G00–G05 готовы; следующий этап G06.

## Цель и порядок

Пользователь поручил выполнять этапы последовательно без остановок между ними;
при необходимости разрешены субагенты. Целевой результат — **один GBA-ROM
с переключением RU/EN, общим балансом/геометрией и целевой частотой 30 FPS**.
Различия данных разрешаются по `docs/gba-balance-proposal.md`: стартовый гибрид
принят как руководство, неподтверждённые ветви геометрии остаются гипотезами.
База карт пока RUS, переключение языка не меняет мир/баланс.

Последнее поручение 25 октября — продолжать до конца текущей задачи G05.
G05 завершён; G06 не начинался. Это актуальная граница данного продолжения.

Уточнение UI: [gba-ui.md](gba-ui.md). Сохраняем пропорции исходного игрового
поля; нижний остаток экрана (~15 px) целиком под строку сообщений. Жизнь и
заряд числами, ключи — в углах поля поверх сцены. Учесть viewport уже в G05.

Реестр и зависимости — `docs/gba-plan.md`, паспорта — `docs/tasks/`.
**G05 завершён**: первое изображение настоящего LEVEL1 на GBA проверено.
Root ведёт общие build/CMake/GBA файлы, план и этот handoff.
Следующий этап — G06: время, события и фиксируемое исполнение.
После него обязателен G06A: подвижная камера и ранний бюджет renderer;
затем строго G07–G21 по порядку. Оценка после G05:
`docs/plan-review-g05.md`. Пользователь потребовал линейный план без развилок.
План и паспорта последующих задач приведены к последовательным зависимостям.
G06A ещё не начат; потолок 400 000 cycles — плановый критерий статической
сцены, не измеренный результат. Новые сборки в рамках оценки не запускались.
Владение подзадачами G05 назначается сообщениями; общие Ghidra-проекты
нельзя одновременно изменять двумя агентами одного языка.

Текущие артефакты G04: `docs/rendering.md` и тематические geometry/assets/reference;
`scripts/InspectRendering.java`, `scripts/analyze-rendering.ps1` (34 роли на язык),
`include/dok/projection.h`, `src/projection.c`, `tools/projection_probe.c`.
`scripts/probe_projection.py` прошёл: 31216 native-векторов + Thumb compile,
сохранены wrap32, SAR15 и signed IDIV. В G05 модуль реально исполняется на ARM;
полного renderer/всей игры пока нет.
Итоговый `scripts/verify_rendering.py` прошёл:34 роли каждого образца,
462 изображения,6 LIGHT,9 synthetic,1444 trig values,4 точных door vertex
records. `analysis/rendering-verification.json`. UI — **240×144 + 240×16**:
это сохранение DOS VGA pixel aspect5:6, raw clip оригинала320×160.

## Выполненные результаты

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
  EWRAM89776, renderer workspace54284. Watermark main stack2400 байт,
  untouched5536; IRQ/BIOS выше03007F00 не измерены.
- `analysis/render-benchmark.json`, `analysis/render-host-verification.json`:
  RU→EN→RU сохраняет все33864 байта мира,1344 байта faces и34560 scene pixels.
  Все38400 индексов с независимо составленным HUD и512 palette RAM bytes
  совпали для трёх captures. Guards, capacity1 отказ, poisoned-RAM reset passed.
  В ELF/ROM исполняются16 edge divmod vectors через ARM/Thumb wrappers.
- **30 FPS не достигнуты:**10175200…10187755 cycles за render+HUD+VRAMcopy
  против561792; steady36…37 VBlank≈1,637 FPS,18,13× бюджета. Для будущей
  интеграции требуется существенная оптимизация; этот результат не считать
  приемлемой конечной частотой. G05-критерий первого кадра и измерения выполнен.
  Пол/потолок diagnostic flat, LIGHT row0, Q7-U approximation, heading90 only;
  полный raster, billboard, анимации, движение, игровой цикл и HUD пока впереди.

- **G03:** `scripts/pack_gba_assets.py`, `include/dok/rom_assets.h`,
  `src/rom_assets.c`, `tools/rom_assets_tool.c`, `tests/test_rom_assets.c`,
  `scripts/verify_packing.py`; спецификация `docs/gba-assets.md`.
  Детерминированный пакет2763046 байт,143 resource IDs,286 исходных payload
  вариантов,3 общих уровня×2 языка. Профиль —39 изменений от RUS по proposal;
  прочие85 исходных numeric differences/87 map cells явно отнесены к RUS базе.
  `analysis/packing-report.json` подтверждает все6 views, исходные переводы,
  binding IDs/диапазоны,23 corruptions и побайтный детерминизм пакета/manifest.
- `scripts/build-gba-assets.ps1 -CrossChecks` прошёл целиком:
  **9 Windows-тестов**, Linux/Thumb compile, runtime/packing oracle, ARM ROM.
  `build/gba-assets/dok-diagnostic.gba` —2776104 байта, EWRAM34572;
  пакет в cartridge совпадает с offline assets.bin побайтно.
  Все6 original-derived views реально reset на ARM. Select меняет RU/EN
  textpool; все33864 байта mutable мира сохраняются, poisonedRAM reset проходит.
  `analysis/g03-gba-verification.json`, PNG `build/gba-assets/assets-{ru,en}.png`.
  Это технический asset экран, не игровой renderer/HUD.
- AssetsProbe резервирует **8 KiB IWRAM stacks/BIOS** через LLD override:
  локальные580-байтные views reader могут вкладываться. Default G01/G02 —1KiB.
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
  Диагностический ROM2560 байт; native `.data/.bss`40 байт EWRAM.
  `docs/gba-platform.md`, `analysis/g01-verification.json`.
- **G02:** `include/dok/runtime.h`, `src/runtime.c`, `tools/runtime_tool.c`,
  `tests/test_runtime.c`, `scripts/analyze_runtime.py`.
  Immutable ROM descriptors/typed initial states отдельно от RAM runtime.
  Один объект на glyph, последнее размещение повторов, explicit unknown
  неразмещённых актёров. Все6 исходных миров/780 definitions/69623 known bytes
  и полные masks/все class fields/resources/texts/maps сохранены.
  `analysis/runtime-verification.json`, `docs/gba-memory.md`,
  `analysis/gba-memory-budget.json`.
- ARM runtime **33864 байта**: objects256×36, actors128×64, player60,
  maps4×4096 плюс указатель/счётчики. Max исходных defs158, actors106,
  field records1655, textpool8849. Actor reserve22 — инженерный запас,
  не доказанный лимит активных снарядов/врагов оригинала.
- `gba/memory_probe.c` + `scripts/runtime_memory.py`:
  `build/gba-memory/` ELF/map/layout/ROM7640 байт. С реально размещёнными
  резервами будущих buffers EWRAM178800, свободно83344.
  Резервы144896 — не готовые подсистемы. Верхний1KiB IWRAM пока reserved
  stacks/BIOS; глубина стека не измерена. `analysis/g02-memory-run.json`
  подтверждает ARM synthetic reset/position/release, startup, keys, IRQ,
  panic, poisoned-RAM reset. Это не запуск всех6 игровых миров на ARM.
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
  Автопроверки в отдельном ядре0.11-212-7a12d6d
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
Существенные доказательства этапов2–6: `docs/resources.md`, `animations.md`,
`sound.md`, `levels.md`, `world.md`; различия/параметры — `enemy-parameters.md`,
`enemy-tables.md`, `weapon-parameters.md`, `level-version-comparison.md`.
Материалы `C:/Source/wasdify` только для чтения, происхождение/ограничения
`docs/wasdify-findings.md`. LEVEL и world имеют разный порядок плоскостей.

Физическая GBA, оригинальный BIOS, GUI/GDB-сессия, stack high-water и
производительность полной игры пока не проверены. Достигнутые~20 обновлений
текста диагностики не являются оценкой рендера игры или достижением30FPS.
G05 закрыт; продолжение начинается с G06 по обновлённой линейной очереди.
