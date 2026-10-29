# N02: сборка и проверка платформенной диагностики

Обновлено 29 октября 2026 года. **SDK установлен, две чистые диагностические
сборки совпали, три эмуляторных сценария пройдены.** Проверено только
диагностическое приложение N02; аппаратная приёмка итоговой игры остаётся
N24. Паспорт приёмки — [N02](../tasks/N02.md).

По [порядку проверки](validation-policy.md), принятому пользователем,
промежуточная приёмка выполняется на эмуляторе. Физическая DS/DS Lite,
флеш-картридж и microSD проверяются на итоговой сборке в N24. Аппаратные
пункты не считаются выполненными по результатам этого стенда.

## Общая установка Windows

SDK хранится вне репозитория. Текущий общий корень MSYS2:
`C:/Users/dspev/.local/msys64`; Wonderful устанавливается под его
`opt/wonderful`, видимым из MSYS2 как `/opt/wonderful`. Установленный BlocksDS:
`/opt/wonderful/thirdparty/blocksds/core`, то есть
`C:/Users/dspev/.local/msys64/opt/wonderful/thirdparty/blocksds/core`.
Сборщик использует
UCRT64 login shell и загружает `/opt/wonderful/bin/wf-env`. Скрипт SDK задаёт
`BLOCKSDS`, `WONDERFUL_TOOLCHAIN` и пути инструментов в дочернем процессе;
родительская PowerShell-сессия и постоянный пользовательский PATH не меняются.
`WONDERFUL_TOOLCHAIN=/opt/wonderful`,
`BLOCKSDS=/opt/wonderful/thirdparty/blocksds/core`.
Значение Makefile по умолчанию `/opt/blocksds/core` относится к другой схеме
размещения; драйвер получает установленный путь из `wf-env`.

Исходные инструкции: [установка Wonderful](https://wonderful.asie.pl/wiki/doku.php?id=getting_started)
и [BlocksDS для Windows](https://blocksds.skylyrac.net/docs/setup/windows/).
Официальная схема после установки Wonderful: включить репозиторий BlocksDS,
синхронизировать пакеты и установить `blocksds-toolchain`; стандартному MSYS2
пакету `make` принадлежит команда сборки. Драйвер проекта пакеты не устанавливает.

```bash
source /opt/wonderful/bin/wf-env
wf-pacman -Syu wf-tools
wf-config repo enable blocksds
wf-pacman -Syu
wf-pacman -S blocksds-toolchain
wf-pacman -Q
```

Это процедура подготовки общего SDK, а не команда обновления перед каждой
сборкой. При воспроизведении N02 нужны закреплённые версии, а не произвольный
новый результат `-Syu`. Установленные версии подтверждены `wf-pacman -Q`,
[toolchain_versions.json](../../../ports/nds/toolchain_versions.json) и
`research/evidence/nds/n02/installed-packages.txt`:

| Компонент | Точная версия пакета |
|---|---|
| `blocksds-toolchain`, `blocksds-docs` | `1.24.0-1` |
| `toolchain-gcc-arm-none-eabi-gcc`, `gcc-libs` | `1~16.2.0.r229124.be93d9d35bf-1` |
| `toolchain-gcc-arm-none-eabi-binutils` | `2.47-2` |
| `toolchain-gcc-arm-none-eabi-picolibc-generic` | `1.8.13.r26367.770d86889-1` |
| `toolchain-gcc-arm-none-eabi-libstdcxx-picolibc` | `16.2.0.r229124.be93d9d35bf-1` |
| `wf-pacman` | `7.1.0-3` |
| `wf-tools` | `0.2.0-3` |

Полный lock содержит 20 пакетов, включая Windows runtime и Lua-инструменты.
Его SHA-256 — `76906c4c1c07d6bd6cfcf3be02277477581fdab8fcabf7f85cfd8cc0dff472ef`.
Успешный `build/nds/n02/build-report.json` подтверждает
`toolchain_lock_verified: true` и этот же SHA. GCC исполняется из
`/opt/wonderful/toolchain/gcc-arm-none-eabi/bin/arm-none-eabi-gcc`, версия GCC —
16.2.0. SDK ndstool сообщает `v1.24.0-dirty`; это строка поставленного
инструмента, а не признак внесённых проектом изменений в SDK.

Полученные установочные архивы:

| Образец | SHA-256 | Основание |
|---|---|---|
| MSYS2 SFX `20260927` | `ad336cccfda47758b5e15cda993fbba421115cb0b126697daef1ee4dfe37209f` | Контроль полученного образца |
| `wf-bootstrap-windows-x86_64.tar.gz` | `551e5b30598715fe9ee92522f4f9bd4f8cc91b120bc7a2305047e4f40a072de6` | Локальный SHA, без утверждения о сверке с upstream checksum |
| `melondsds_libretro-win32-x86_64-Release.zip`, v1.4.0 | `f03af0318fee91a83880f438b369a1368fea6b5c1524a09dde849ca8fa68603f` | Сверен с digest GitHub release asset |

Архив Wonderful получен с
`https://wonderful.asie.pl/bootstrap/wf-bootstrap-windows-x86_64.tar.gz`.
Локальный протокол —
`research/evidence/nds/n02/bootstrap-acquisition.json`.
В текущей сети передачи больше 16 КиБ зависали; временные
`research/evidence/nds/n02/download_range.py` и `fetch_package.py` получают
малые HTTPS ranges с проверкой размера, Content-Range и ETag/If-Match.
Это обход ограничения транспорта, не альтернативный источник SDK.
Временный `XferCommand` менеджера пакетов восстанавливается после каждой
такой команды через `finally`; после установки он удалён из общей
конфигурации. Изменение не является настройкой проекта.

Протокол завершённой установки —
`research/evidence/nds/n02/sdk-installation.json`: проверены версии всех
20 пакетов и SHA-256 19 архивов в кэше относительно загруженной sync database.
`runtime-msys2-shell` пришёл в bootstrap; отдельного архива в кэше нет,
проверена установленная версия. Это не независимая проверка подписей
пакетов; протокол не приписывает checksum базе более сильное происхождение.

## Цель сборки и фиксация версий

Исходники независимой диагностики:
[main.c](../../../ports/nds/platform/diag/main.c),
[diagnostic_state.h](../../../ports/nds/platform/diag/diagnostic_state.h),
[Makefile](../../../ports/nds/Makefile) и
[build_diag.ps1](../../../ports/nds/tools/build_diag.ps1).
Игровой цикл, ресурсы PKR/PKE и GBA startup сюда не включены.

Команды из корня `C:/Source/DoK-port`:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File ports/nds/tools/build_diag.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File ports/nds/tools/build_diag.ps1 -Rebuild
```

Первый вызов собирает изменившиеся входы; `-Rebuild` сначала проверяет SDK,
затем удаляет фиксированные выходы N02 и собирает заново. Для иной общей
установки задаётся `-MsysRoot 'C:/путь/msys64'`. Эквивалент внутри настроенного
Wonderful shell:

```bash
source /opt/wonderful/bin/wf-env
cd /c/Source/DoK-port
make -C ports/nds all
```

Makefile платформенной диагностики использует **GNU C11** (`-std=gnu11`),
`-Og -g3 -Wall -Wextra -Werror`, ARM9 Thumb для
`arm946e-s+nofp`, `ds_arm9.specs`, `libnds9` и SDK ARM7
`sys/arm7/main_core/arm7_maxmod.elf`. Основа команд — официальный
[ARM9-only template](https://raw.githubusercontent.com/blocksds/sdk/master/templates/rom_arm9_only/Makefile).
Установленные libnds headers используют GNU inline `asm`, поэтому strict
`-std=c11` для этой платформенной цели не подходит. Это локальное требование
SDK; независимое host-ядро N03 сохраняет strict C11 и не получает GNU/NDS
зависимость. Стоимость вывода диагностического текста не задаёт стоимость
будущего renderer игры.

Первая успешная сборка записана в
`research/evidence/nds/n02/first-build.log`. Компиляция собственных исходников
прошла с `-Werror`. Linker выдаёт предупреждение об отсутствующей
`.note.GNU-stack` у поставленного SDK `crtn.o`; оно не подавляется и не
выдаётся за предупреждение из собственных исходников.

Перед сборкой драйвер выполняет `wf-pacman -Q`. Если существует
`ports/nds/toolchain_versions.json`, ожидается JSON со схемой
`schema_version: 1` и непустым объектом `packages`, где каждому точному имени
пакета соответствует строка его версии. Проверяется наличие и точное
совпадение каждой перечисленной версии; дополнительные установленные пакеты
допускаются. Отсутствующий lock вызывает предупреждение и
`toolchain_lock_verified: false`, поэтому такой результат не доказывает
воспроизведение закреплённым SDK. Текущий lock создан по фактически
установленным пакетам; текущий build-report успешно проверил все 20 версий.

Успешный драйвер сохраняет в `build/nds/n02/`:

- `dok_nds_diag.nds`, `dok_nds_diag.elf`, `dok_nds_diag.map`;
- `obj/main.o`, `obj/main.d`;
- `build-report.json`: schema 1, все обнаруженные пакеты, признак и SHA lock,
  размеры и SHA-256 трёх итоговых артефактов.

Для доказательства повторяемости используется
[check_diag_rebuild.ps1](../../../ports/nds/tools/check_diag_rebuild.ps1):

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File ports/nds/tools/check_diag_rebuild.ps1
```

Он требует lock, выполняет два последовательных `-Rebuild`, сверяет свежие
отчёты с фактическими ROM/ELF/map и сравнивает размеры, хеши и версии пакетов.
Протокол — `research/evidence/nds/n02/rebuild-check.json`; дубликаты бинарников
не создаются. При ошибке протокол получает `success: false`, старый build-report
не принимается за новый результат. Финальный collector завершился с кодом 0:
`success`, `artifacts_match` и `package_versions_match` равны `true`.
Обе чистые сборки проверили один и тот же lock; размеры и SHA трёх итоговых
файлов совпали между прогонами и с файлами на диске.

| Итоговый файл в `build/nds/n02/` | Байт | SHA-256 |
|---|---:|---|
| `dok_nds_diag.nds` | 146432 | `5f65eaa46114e5821eae3614a746d890f045ea3cd51d6e50c7d79c41c7194577` |
| `dok_nds_diag.elf` | 1013612 | `3c11e8dd25f5e2848c4908a9b819d9d7b4527ea88eb418c69d7788fd505e2b41` |
| `dok_nds_diag.map` | 652491 | `56cab9f995e09252159b5c71a73e852e42d0da931dde228ba89c486c7f821f4a` |

Очистка только диагностических выходов:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File ports/nds/tools/build_diag.ps1 -Clean
```

`-Clean` не требует SDK и не запускает сборку; вместе с `-Rebuild` запрещён.
Удаляются только три конечных файла, `obj/main.o`, `obj/main.d` и отчёт.
Драйвер проверяет абсолютные пути, обычные каталоги/файлы и отсутствие
junction/symlink в своей цепочке выходов. Эмуляторные доказательства и чужие
сборки этой командой не удаляются. Shell-код передаётся bash через stdin,
путь репозитория заключён в POSIX literal; Windows argv не используется
для передачи текста shell-команды.

## Что выполняет ROM

Главный 2D engine выведен сверху через `lcdMainOnTop()`, второй — снизу.
Видимые подписи `TOP`/`BOTTOM` позволяют отдельно проверить порядок экранов.
Верхняя консоль показывает VBlank, время, интервалы кадра, стоимость работы,
FAT/DLDI и звук; нижняя — held/down/up всех 12 кнопок и координаты touch.
Input, часы и observation обновляются каждый VBlank; текстовая панель
обновляется независимо, по очереди шестью группами, не более восьми строк
за проход. Полный обход занимает шесть VBlank (около 10 Гц). Это разделение
частот диагностического ввода и его отображения, не оптимизация игры.

Счётчик SDK использует ARM9 timer 2, частоту `BUS_CLOCK / 64` и 64-битные
ticks. CPU/work измеряется парой timer 0/1 с частотой `BUS_CLOCK` =
33 513 982 Гц. Измеренный участок включает input, UI, I/O и IRQ; ожидание
VBlank и публикация наблюдаемого состояния исключены. Это прошедшее время
участка, а не число исполненных инструкций. Пара аппаратных таймеров для
work имеет 32-битный диапазон — примерно 128 секунд непрерывной работы.
Интервал кадра измеряется отдельно между проходами после VBlank.
API: [timers.h](https://raw.githubusercontent.com/blocksds/libnds/master/include/nds/timers.h),
[system_counter.h](https://raw.githubusercontent.com/blocksds/libnds/master/include/nds/system_counter.h).

`A` включает PSG 440 Гц на 250 мс, `B` останавливает его; `X` повторяет чтение
marker, `Y` сбрасывает min/max интервалов и счётчик пропущенных VBlank.
Эти назначения принадлежат диагностике и не заменяют управление из плана.
PSG использует channel 8 и
[SDK sound API](https://raw.githubusercontent.com/blocksds/libnds/master/include/nds/arm9/sound.h).
В аргумент `freq` передаётся `440 * 8` = 3520: API задаёт частоту шагов
аппаратного PSG, а период выбранной duty-wave содержит восемь шагов.
Передача непосредственно 440 дала в захваченном PCM около 55 Гц и была
исправлена. В итоговом захваченном PCM измерено 440,2219 Гц; тихий участок
до тона и последние 100 мс после его timeout имеют RMS 0.

ROM открывает только для чтения `fat:/dok/nds-diag.txt`. Точное содержимое —
16 байт `DOK_NDS_DIAG_V1\n`, без BOM и с LF; образец находится в
`ports/nds/platform/diag/sdroot/dok/nds-diag.txt`. Отдельно показываются
недоступность FAT/DLDI, отсутствие файла, ошибка открытия/чтения и неверные
байты. Дополнительный байт обнаруживает лишний хвост. Записи игровых
сохранений или иных файлов ROM не выполняет. Основание работы с FAT/DLDI —
[официальное руководство](https://blocksds.skylyrac.net/tutorial/intermediate/filesystems/).

## Эмуляторный стенд и команда

Используется **melonDS DS libretro core 1.4.0** проекта
[JesseTG/melonds-ds](https://github.com/JesseTG/melonds-ds/releases/tag/v1.4.0).
Это отдельный проект core; его номер 1.4.0 не является версией standalone
melonDS. Общая установка DLL:
`C:/Users/dspev/AppData/Local/Programs/melonDS-DS/1.4.0/melondsds_libretro-win32-x86_64-Release/cores/melondsds_libretro.dll`.

Реальный ROM исполняет стандартный CPython frontend
[run_diag_emulator.py](../../../ports/nds/tools/run_diag_emulator.py)
без GUI. Команда из корня репозитория:

```powershell
C:/Source/wasdify/.venv/Scripts/python.exe ports/nds/tools/run_diag_emulator.py --core C:/Users/dspev/AppData/Local/Programs/melonDS-DS/1.4.0/melondsds_libretro-win32-x86_64-Release/cores/melondsds_libretro.dll --rom build/nds/n02/dok_nds_diag.nds --elf build/nds/n02/dok_nds_diag.elf --out build/nds/n02/emulator
```

Python использует стандартную библиотеку; DLL и процесс Python должны иметь
одинаковую x86-64 архитектуру. `--map` допускается вместо `--elf`, но ELF
дополнительно даёт фактический размер символа. Выход ограничен каталогом
`build/` текущего репозитория. Каждый из трёх сценариев работает в отдельном
процессе с собственными system/save-каталогами и пределом 240 секунд.

Настройки: DS mode, direct boot, встроенные replacement BIOS/firmware,
software renderer, отключённые JIT/threaded renderer, два экрана 256×192
вертикально без промежутка, pointer touch, выключенные сеть и microphone
input. Синхронизация образа с host отключена.
`melonds_homebrew_readonly=enabled` ограничивает запись через
эмулируемый DLDI, но не обещает неизменность host-файла при подготовке boot.
Образ расположен в save-каталоге сценария:
`save/melonDS DS/dldi_sd_card.bin`, не в system-каталоге.
Точный набор запрошенных core options записывается в отчёт сценария.
Nintendo BIOS/firmware dumps, savestates и оригинальные игровые данные
для этого стенда не требуются.

В ходе direct boot core добавляет копию ROM в FAT-образ ещё до исполнения
ARM-кода диагностики. Наблюдавшееся изменение каталога/цепочки FAT согласуется
с upstream `CartHomebrew::SetupDirectBoot → FATStorage::InjectFile`:
[CartHomebrew.cpp](https://raw.githubusercontent.com/melonDS-emu/melonDS/master/src/NDSCart/CartHomebrew.cpp)
и [FATStorage.cpp](https://raw.githubusercontent.com/melonDS-emu/melonDS/master/src/FATStorage.cpp).
Это host-подготовка эмулятора; сама диагностика по-прежнему открывает marker
только с `rb`. Указанные master-исходники объясняют механизм, но пока не
выдаются за подтверждённый точный submodule commit бинарника core 1.4.0.

## Наблюдение и покрытие проверок

`g_nds_diag` — специальный read-only observation ABI диагностики, а не
будущий интерфейс движка. ELF даёт адрес и размер; ожидаются magic `DOK2`,
ABI version 1, 62 little-endian `uint32_t` — 248 байт. Нечётная sequence
означает запись; frontend принимает только согласованный чётный снимок.
ARM9 сбрасывает соответствующие строки data cache в RAM. Frontend читает
`RETRO_MEMORY_SYSTEM_RAM`, не пишет в RAM/ROM и не меняет PC. Ввод проходит
через стандартные joypad/pointer callbacks
[libretro API](https://raw.githubusercontent.com/libretro/libretro-common/master/include/libretro.h).

Проверки исполняемого ROM:

- Boot и наблюдаемое состояние; монотонные system ticks/VBlank, продвижение
  цикла и uptime, интервалы 15–19 мс, ненулевой work меньше интервала,
  наблюдаемая частота system counter около `BUS_CLOCK / 64`.
- Настоящие video callbacks 256×384 и два различных непустых экрана. Порядок
  `TOP`/`BOTTOM` дополнительно проверяется визуально по PNG; сам distinct-test
  не доказывает, что экраны не переставлены.
- Каждая кнопка отдельно: press, hold без повторного down, release; суммарные
  маски 0x0FFF. Touch: три координаты с допуском 2 px в одном удержании,
  один down и один up.
- Реальный PCM: звук после A относительно тихого участка, частота 400–480 Гц,
  запрос остановки по timeout и тихие последние 100 мс захваченного PCM.
- `happy`: mount и точный 16-байтный marker, число прочитанных байт и FNV-1a.
  `missing-marker`: носитель есть, marker отсутствует. `missing-media`:
  homebrew SD отключён, ожидается ошибка mount. Синтетический диск — 32 МиБ,
  MBR/FAT16, каталог DOK, короткие имена 8.3 у исходного fixture. Фиксируются
  SHA созданного образа, после `retro_load_game` и после run/unload. Признак
  `runtime_unchanged` требует совпадения SHA после load, после run и после
  unload; boot-инъекция может изменить исходный SHA. Безоговорочная
  неизменность образа от генерации до завершения не заявляется.

Перед чтением host-файла после load и run frontend вызывает
`fflush(NULL)` из `msvcrt.dll`: проверенный PE import table DLL core
указывает именно этот C runtime для `fopen`/`fwrite`/`fflush`. Это делает
отложенные host stdio writes видимыми до снятия baseline, не меняя
эмулируемые память, CPU, ввод и DLDI. Без flush boot-записи могли бы попасть
на диск позднее и ошибочно выглядеть как запись во время исполнения ROM.
В обоих сценариях с образом baseline после load отличается от исходного,
но совпадает с SHA после run и unload; `runtime_unchanged: true`.

Выходы в `build/nds/n02/emulator/`: общий `result.json`, журналы сценариев,
в подкаталогах — `result.json`, `observations.json`, PNG обоих экранов и
WAV. Отчёты содержат SHA ROM/core/ELF либо map/инструмента, core name/version,
настройки, выполненные assertions и численные результаты. При сбое
сохраняются доступные failure PNG/WAV.

## Проверенный эмуляторный результат

На финальном SHA ROM из таблицы выше все три процесса завершились с кодом 0,
общий `build/nds/n02/emulator/result.json` содержит `passed: true`.
Использован CPython 3.14.6, библиотека сообщает `melonDS DS` / `1.4.0`;
SHA-256 DLL — `c7e6b58c667075a2a9868f2c3ba7a98e24200410a6da3051a7c555f1f0eef8ef`.
SHA-256 исполненного frontend —
`960a656a76e82663a19f56df4da727728207877928a5d13b9cc47b79ca26e5c7`;
он совпадает с текущим `run_diag_emulator.py`.

| Сценарий | Assertions PASS | Кадры frontend | Storage result |
|---|---:|---:|---|
| `happy` | 67 | 592 | 6 — marker проверен |
| `missing-marker` | 18 | 132 | 2 — marker отсутствует |
| `missing-media` | 17 | 132 | 1 — FAT/DLDI недоступен |

В `happy` все 12 кнопок прошли press/hold/release, touch дал точные
координаты (32, 24), (128, 96), (224, 168) и один down/up за удержание.
PSG подтвердился реальным PCM: 440,2219 Гц, тихий baseline и хвост RMS 0.
В 120 итоговых steady-state кадрах интервал составил 16 713–16 715 мкс,
CPU/work — 2632–3503 мкс, медиана 3119 мкс. Частота кадров, сообщаемая core,
59,82609828808083 Гц; аудио sample rate — 32 728,498046875 Гц.
Эти значения относятся к виртуальной машине эмулятора и диагностике.

Визуально просмотрены `happy/final-top.png`, `happy/final-bottom.png` и
верхние итоговые кадры обоих сценариев ошибок: экраны расположены правильно,
счётчики 12 кнопок/touch и объяснения ошибок читаемы. Такая проверка
дополняет автоматические dimension/distinct assertions.

Постоянные raw reports и отобранные PNG/WAV сохраняются в
`research/evidence/nds/n02/emulator/`; компактный итог —
`analysis/nds-n02-platform.json`. Локальные FAT-образы и повторяющиеся кадры
не служат поставляемыми ресурсами порта. Бинарный хеш core фиксирует
исполненный образец; точный submodule commit его upstream пока отдельно
не доказан и не требуется для утверждения о наблюдавшихся PASS.

## Границы доказательства и следующие результаты

Virtual timers подтверждают согласованность функционального эмуляторного
стенда. Они и скорость host не доказывают аппаратные FPS, I/O latency,
совместимость loader/DLDI, физические кнопки, стилус и аудиовыход DS.
Диагностика также не проверяет игру, оригинальные ресурсы, renderer,
бюджет памяти N03 или исходный звуковой тракт N18.

Два чистых build-report с совпавшими хешами, результаты трёх сценариев,
визуальная проверка экранов, установленные версии и lock зафиксированы.
Постоянное хранилище первичных доказательств —
`research/evidence/nds/n02/`; компактный итог ведётся согласно
`analysis/report-policy.json`. В Git не включаются SDK, установочные
архивы, большие runtime-выходы и локальные исследования. Аппаратная приёмка
итоговой игры остаётся N24 по [validation-policy](validation-policy.md).
