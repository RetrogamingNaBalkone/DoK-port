# G01: диагностическая платформа GBA

24 октября 2026 года получен и выполнен в mGBA настоящий cartridge ROM без
ресурсов PKR/PKE. Исходники — `gba/`, сборка — `scripts/build-gba.ps1`.
Это платформенная диагностика; игровой цикл и восстановленные модули движка
пока в ROM не включены.

## Инструменты и воспроизведение

Используется общий Zig 0.13.0:
`%LOCALAPPDATA%/Programs/Zig/0.13.0/zig.exe`, встроенные Clang/LLD 18.1.6.
Отдельный GCC/devkitARM не устанавливался: необходимых C, ARM assembler,
linker script и извлечения binary достаточно в имеющемся Zig. Каталог
`build/zig-cache` содержит только кеш сборки; toolchain туда не копируется.
PATH и постоянные переменные среды не менялись.

Установлены в общий `%LOCALAPPDATA%/Programs/mGBA/`:

- `mGBA-0.10.5-win64/` — официальный portable-релиз, Qt и SDL frontends.
- `libretro-20261024/mgba_libretro.dll` — ядро mGBA
  `0.11-212-7a12d6d` с buildbot Libretro, на котором выполнена автоматическая
  проверка. Это другая версия, чем standalone 0.10.5.

Источники, версии и SHA-256 пакетов/бинарников —
[gba-tools.json](../analysis/gba-tools.json). Адрес nightly меняется;
для точного повторения нужен сохранённый общий DLL с указанным хэшем.
Контрольные суммы вычислены локально, не выданы за подпись издателя.

Python: `C:/Source/wasdify/.venv/Scripts/python.exe`, только стандартная
библиотека, без зависимости runtime порта от WASDify.

```powershell
# Сборка + выполнение проверок в общем ядре mGBA:
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build-gba.ps1 -Verify
# Только ELF/ROM/map, без эмулятора:
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build-gba.ps1
# Проверить уже собранный ROM:
C:/Source/wasdify/.venv/Scripts/python.exe scripts/verify_gba.py
# Чистый отдельный каталог результатов:
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build-gba.ps1 -OutDir build/gba-clean
# Открыть ROM в интерактивном mGBA:
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/run-gba.ps1
```

`-ZigExe`, `-PythonExe`, `-OutDir`, `-CorePath` позволяют переопределить пути.
У `verify_gba.py` есть `--core`, `--build-dir`, `--report`.
В VS Code добавлены **DoK: build GBA diagnostic** и
**DoK: build and verify GBA diagnostic**; прежняя native-задача сохранена.

## ROM, startup и память

`gba/startup.S` начинает в ARM state по `0x08000000`, переходит через
стандартный заголовок размером 192 байта на `_start = 0x080000C0`.
Сначала выключает IME, задаёт IRQ/System stacks, копирует `.data` из ROM
в EWRAM и очищает `.bss`, затем через `BX` входит в Thumb-функцию `gba_main`.
Внешняя libc и compiler runtime для этого ROM не требуются.

`gba/gba.ld` задаёт ROM `0x08000000`, EWRAM `0x02000000` и IWRAM
`0x03000000`. Верхний 1 KiB IWRAM исключён из размещения секций:

| Область | Назначение |
|---|---|
| `0x03007C00–0x03007EFF` | Резерв System stack; SP начинает с `0x03007F00` |
| `0x03007F00–0x03007F9F` | IRQ stack; SP_irq = `0x03007FA0` |
| `0x03007FA0–0x03007FFF` | Резерв BIOS, в том числе flag `0x03007FF8` и callback `0x03007FFC` |

Резерв не является измерением глубины стека и не защищает от его переполнения.
В диагностике статические `.data` + `.bss` занимают **40 байт EWRAM**;
остальной бюджет игры и размещение кода в IWRAM — G02.

`scripts/gba_rom.py` проверяет ELF ARM executable, ARM-вход картриджа,
Thumb-bit C-входа и выравнивание ARM IRQ callback. В binary записывает
стандартное boot logo, `DOK GBA DIAG`, код `DK01`, фиксированный `0x96` и
header complement. Стандартные байты logo сверены с
[gbafix devkitPro](https://github.com/devkitPro/gba-tools/blob/master/src/gbafix.c);
игровые файлы для сборки не читаются.

Особенность Zig 0.13.0 подтверждена ELF mapping symbols: сочетание
`-target arm-freestanding-eabi -mthumb` в этой установке давало `$a` и
чётный адрес C-функции, то есть ARM-код. Для Thumb используется
`-target thumb-freestanding-eabi -mcpu=arm7tdmi`. Исправлены и шесть
соответствующих compile-check команд в `scripts/build-native.ps1`.
`zig cc` не принимает `-Wl,-Map`; прямой вызов общего `zig ld.lld`
создаёт настоящий linker map.

## VBlank, экран и диагностика

Включён только VBlank IRQ: DISPSTAT bit 3, IE bit 0, IME = 1.
ARM-обработчик `gba_irq` вызывается BIOS через `0x03007FFC`, использует
только сохраняемые BIOS регистры r0–r3, подтверждает IF, обновляет BIOS
interrupt-check flag и `g_vblanks`, возвращается через `BX LR`.
Вложенные IRQ не используются. Адреса здесь — адресное пространство GBA;
они не связаны с NE-сегментами или DPMI-селекторами оригиналов.
WAITCNT кодом диагностики не изменяется; при выполненной проверке прочитано
`0x0000`. Снимок DISPCNT/DISPSTAT/IE/IME/WAITCNT сохранён в JSON-отчёте.

Для первого экрана выбран **Mode 4**: 240×160, BG2, индексная палитра,
страницы `0x06000000` и `0x0600A000`. Записи VRAM только 16-битные;
отдельный пиксель меняется через read/modify/write halfword.
Готовая задняя страница показывается во VBlank.

Экран содержит FRAME, VBLANK, KEYS в hex, десять кнопочных индикаторов
и мигающий квадрат. FRAME считает итерации диагностического вывода,
VBLANK — аппаратные IRQ; число может опережать показанный счётчик,
пока следующий экран готовится в задней странице.

`g_diag` доступен по символу ELF; смещение каждого `uint32_t`:

| Смещение | Поле |
|---|---|
| 0 | magic `0x444F4B31` |
| 4 | frames |
| 8 | последний обработанный vblanks |
| 12 | keys, активная единица, биты KEYINPUT 0–9 |
| 16 | seen, все замеченные кнопки |
| 20 | fault, 0 в норме |
| 24 | data_ok, проверка копирования `.data` |
| 28 | bss_ok, проверка обнуления `.bss` |

В текущем ELF `g_diag = 0x02000004`, `g_vblanks = 0x02000024`;
проверка читает символы из `build-report.json`, не полагается на эти константы.
START+SELECT вызывает контролируемую остановку с красным PANIC-экраном
и кодом `0x10`. Код `1` означает ошибку начальной RAM-инициализации.
При зависании перестают меняться FRAME/квадрат; `g_vblanks` позволяет
отличить живой IRQ от остановившегося main loop. Это не общий обработчик
всех CPU exceptions и не автоматический watchdog.

Для интерактивной отладки есть `scripts/run-gba.ps1 -Gdb`: mGBA запускается
с GDB stub `-g`, порт по умолчанию 2345. Клиенту ARM GDB можно передать
`file build/gba/dok-diagnostic.elf` и `target remote localhost:2345`.
Доступность `-g` подтверждена CLI help; подключение GDB-клиента и GUI-сессия
в G01 не проверялись. Для просмотра игры запускать `.gba`; ELF оставлен
для символов и отладки.

## Выполненные проверки и ограничения

Машиночитаемый результат — [g01-verification.json](../analysis/g01-verification.json).
`scripts/verify_gba.py` — минимальный frontend официального Libretro ABI:
запускает инструкции в mGBA, подаёт кнопки через input callbacks,
читает предоставленные ядром memory descriptors и сохраняет framebuffer
в PNG. Он не подменяет GBA CPU или игровые вычисления.

Проверено:

- ELF, ROM, map из нового `build/gba-clean/`; ROM побайтно совпал по SHA-256
  с `build/gba/`: `bfe60398b90f4cfedd711bfeacbf43caecdc85b8973f32bd698471068a3bf86c`.
  Размер **2560 байт**.
- За 120 аппаратных кадров: **120 VBlank IRQ**, **40 итераций вывода**.
  Полная программная перерисовка текста здесь обновляется раз в три кадра,
  примерно 19,9 раза в секунду аппаратного времени GBA. Это не замер
  renderer будущей игры, не wall-clock скорость эмулятора и не обещание FPS.
- Каждая из десяти кнопок: точная маска в RAM, изменение цвета собственного
  индикатора в framebuffer, сброс после отпускания. Отдельно A+B+R+L = `0x303`.
- START+SELECT: fault `0x10`, красный экран, остановка счётчиков.
- Reset после panic; перед первым CPU-кадром выделенная EWRAM заполнена
  `0xA5`. Startup вновь восстановил `.data`, очистил `.bss` и запустил вывод.
- Кадры `idle.png`, `button-a.png`, `panic.png`, `restart.png` в `build/gba/`
  просмотрены визуально; лог запуска — `build/gba/run-log.json`.

Внешний BIOS dump не нужен: использован replacement BIOS mGBA. На физической
GBA и с оригинальным BIOS этот ROM пока не проверен. Игровые ресурсы,
звук, сохранения, движок и соответствие DOS-оригиналу в G01 не проверяются.
Следующий ID по порядку плана — G00; G02 уже доступна по зависимости G01.

Дополнение G02: `scripts/build-gba.ps1 -MemoryProbe -OutDir build/gba-memory -Verify`
создаёт отдельную ARM-пробу со статическим runtime и резервами будущих буферов.
Она выполняет `dok_runtime_reset`, чтение позиции и `release` маленького
синтетического мира, затем обычную диагностику G01. Для freestanding ABI
добавлены минимальные `__aeabi_memcpy*`/`__aeabi_memclr*` helpers в `gba/memory.c`.
Размеры, карты памяти и границы этой проверки — [gba-memory.md](gba-memory.md),
отчёт `analysis/g02-memory-run.json`. Обычная сборка без `-MemoryProbe`
сохраняет исходный диагностический ROM.

Технические основания: [GBATEK](https://mgba-emu.github.io/gbatek/)
(memory map, cartridge header, interrupt handling, bitmap modes),
[релиз mGBA](https://mgba.io/downloads.html),
[Libretro ABI](https://github.com/libretro/libretro-common/blob/master/include/libretro.h),
[адаптер mGBA](https://github.com/mgba-emu/mgba/blob/0.10.5/src/platform/libretro/libretro.c).
