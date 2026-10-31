# Исправления NDS-плейтеста, 31 октября 2026 года

Эта поставка заменена на [playtest07](playtest07-20261031.md): исправление
зависания, ускорение загрузки, новое расположение HUD и исходный FONT8.
Ниже сохранено описание playtest03 и отвергнутой узкой однострочной формы текста.

Текущий ROM: `artifacts/nds/dok.nds`, 9 554 432 байта, SHA-256
`01c0a23f7b6a74be5f0d609ca331d269e24cbe3384b536706aba58bfd5fe5855`.
Компиляция: `build/nds/n23/playtest03-ru/`, BlocksDS 1.24.0 / GCC 16.2.0.
Один автономный NitroFS ROM с RU/EN, необязательная запись только настроек.
Предыдущая standalone01 заменена. N23 остаётся открытой, N24 не начата.

## Изменения по замечаниям пользователя

- Главное меню, подменю и пауза целиком на верхнем экране с исходным FON.
  Нижний экран пуст. Разделение иллюстрации и текста сохранено у заставок.
- HUD: сообщения сверху, 16 пикселей по высоте, ниже исходная отбивка панели,
  затем сердце/здоровье, магия/заряд, три ключа и два оружия. Полосы,
  числовой индекс оружия, попытки/жизни и score убраны из представления.
  Сами игровые счётчики продолжают работать. Значки оружия показывают
  наличие обоих предметов, независимо от выбранного оружия.
- Каждое игровое сообщение целиком в одной строке. Исходный FONT16
  масштабируется только по ширине 8→6 nearest по центрам; 40 колонок,
  исходный максимум 36 символов. Страница и её номер не показываются.
  Сообщения не сокращаются. Пагинация сюжетных заставок сохранена.
- По отдельному прямому требованию пользователя убрана инерция героя.
  Новая `dok_motion_update_direct` включает полную скорость сразу,
  прекращает движение при отпускании и сразу меняет направление движения
  и поворота. Максимальная скорость, бег, замедление, коллизии и часы
  сохранены. Это намеренная адаптация управления, не эквивалент DOS.
  Исходная `dok_motion_calculate/update` оставлена для x86-сверок.

## Мерцание и окно VRAM

Найдена ошибка в конвейере: передача карты выполнялась перед текстурами,
а remap текстурной VRAM разрешался до следующей строки 47 включительно.
Это путало окно LCD с окном 3D: растеризатор начинает работу уже на строке
214, во время VBlank. [GBATEK, RDLINES_COUNT и Rendering Engine](https://mgba-emu.github.io/gbatek/).
При прокрутке карты предшествующая DMA дополнительно задерживала remap.

Теперь текстуры и palette commit выполняются первыми после границы VBlank;
карта — после них. Текстурные банки вообще не отключаются, если нет pending
текстур. Runtime проверяет начало и окончание commit в строках 192…213,
а также elapsed от той же границы (<22×2130 bus ticks). Это устраняет
обнаруженную причину мерцания; повторный плейтест на устройстве ещё нужен.
Первичная загрузка с выключенным 3D core остаётся отдельной loading-фазой.
Предел строже прежнего: тяжёлые непроверенные маршруты могут обнаружить
превышение бюджета; их полная приёмка остаётся частью N23.

## Исходные материалы и формат

Образцы, SHA и адреса original HUD/formatter перечислены в
[N19-evidence](N19-evidence.md): PKR RU и PKE EN, Ghidra NE-адреса,
не runtime DPMI-селекторы. Новый анализ EXE не выполнялся.
`prepare_playtest_ui.py` проверяет исходные RES до/после чтения и берёт
`ПОРУЖИЕ1/2` непосредственно из них. N19 исходный blob не переписывается.
Банк по-прежнему 8192 байта; первые 768 байт старого бордюра сохранены,
неиспользуемый хвост боковин заменён weapon14×14 по смещениям 7778/7974.
Подготовка — `build/nds/n23/playtest-ui/`, SHA ресурсов и индексы RES
в `sources.json`; скомпилированный include отдельно в target/generated.

## Проверки

Все итоговые проверки PASS:

- Host HUD: 132 669 защищённых растров, все signed16 значения, 1024 глифа,
  все исходные сообщения, cleanup чисел/ключей. Дополнительный playtest gate:
  1316 растров, все сочетания трёх ключей и обоих видов оружия,
  все 546 исходных строк, максимум 36 символов на одной строке.
- Direct motion host: старт, остановка, обратное движение, смена поворота.
- melonDS DS 1.4.0, закреплённый core SHA из `run_renderer_emulator.py`:
  все три уровня, смена RU/EN, пауза/resume, 57 touch-этапов со всеми
  масштабами/drag/Center; actual LCD RGB и INDEX8 карты/меню проверены.
  Короткий набор: 1479 штатных интервалов по 1 VBlank. Это не полный
  маршрут и не принятие общей цели 60 FPS.
- На итоговом ARM9 через реальные joypad inputs проверены вперёд/назад,
  оба стрейфа и оба поворота: после отпускания live pose не меняется
  на протяжении 40 video frames для каждого направления.
- EN world-smoke с пустым записываемым носителем; RU world-smoke без DLDI.
  Отсутствие внешних данных, игровой запуск, pause/resume и RAM PASS.
- Независимый разбор FNT/FAT подтвердил три встроенных файла и их SHA;
  опубликованный ROM побайтно совпадает с проверенной компиляцией.

В ходе настройки тестов исправлены две ошибки измерения: EN settings нельзя
задать при отключённом DLDI; raster snapshot карты обновляется на 12 Hz
и не подходит для проверки отсутствия инерции. Итоговая проверка читает
per-presentation live pose. Эти ошибки не требовали изменения ROM.

Воспроизведение из корня (общий Python 3.14):

```powershell
py -3.14 -B -X utf8 -c "import sys,json; from pathlib import Path; sys.path.insert(0,'ports/nds/tools/performance'); from build import build; f=json.loads(Path('build/nds/n23/standalone01-ru/build-manifest.json').read_text())['feature_selection']; build('playtest03-ru',f)"
py -3.14 -B -X utf8 ports/nds/tools/check_playtest_ui.py
py -3.14 -B -X utf8 ports/nds/tools/check_hud_host.py
py -3.14 -B -X utf8 ports/nds/tools/check_playtest_controls.py build/nds/n23/playtest03-ru
py -3.14 -B -X utf8 ports/nds/tools/run_joint_frontend_emulator.py --target build/nds/n23/playtest03-ru --language 1 --mode live --levels 1 2 3 --steady-presentations 120
py -3.14 -B -X utf8 ports/nds/tools/run_frontend_emulator.py --target build/nds/n23/playtest03-ru --language 2 --mode world-smoke
py -3.14 -B -X utf8 ports/nds/tools/run_frontend_emulator.py --target build/nds/n23/playtest03-ru --mode world-smoke --no-storage
py -3.14 -B -X utf8 ports/nds/tools/package_rom.py --target build/nds/n23/playtest03-ru
```

Label immutable: для новой компиляции выбрать новое имя. MSYS2 требует
запуска вне песочницы из-за Win32 signal pipe. Логи запускались через
`performance/run_logged.py` в `.log.gz` внутри target. Host direct test:
общий Zig 0.13.0 `cc -std=c11 -Wall -Wextra -Werror -O2 -I ports/nds/include
ports/nds/core/motion.c ports/nds/tests/test_direct_motion.c -o
build/nds/n23/direct-motion.exe`, затем запуск EXE; Zig caches в build/.

Доказательства: `research/evidence/nds/n23/playtest03/` и immutable
`research/evidence/nds/n23/candidates/playtest03-ru/` (ROM/ELF/map/исходники).
Компактная сводка — `analysis/nds-playtest-20261031.json`.
Физическая DS/DS Lite, полные маршруты и общий бюджет N23 не проверены.
Следующий ID — N23: повторный плейтест и дальнейшая проверка тяжёлых
маршрутов со строгим texture deadline. GBA/SDL2 не менялись.
