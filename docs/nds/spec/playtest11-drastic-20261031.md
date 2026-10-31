# Playtest11: запуск в DraStic на RG DS

Пользователь сообщил: при запуске ROM Loading остаётся на двух делениях.
Версия DraStic и длительность ожидания не указаны; устройство локально недоступно.
Контрольный предыдущий ROM playtest10:
`7ad0033563651ffbccef3dfcdfaa6fda8a320dc030adc46cbae1b0645e67efa5`.

## Локализация и ограничения

В `platform/gameplay_probe/main.c` два деления выставляются после успешного
`nitroFSInit(NULL)`, открытия `assets.ndp` и проверки каталога, перед
`nds_audio_runtime_init`. Следующая отметка раньше была сразу 6.
Следовательно, наблюдение указывает на загрузку звуков/проверку title chunks/
ARM7 ATTACH либо отказ этого участка. Это не доказательство ошибки NitroFS
при инициализации и не доказательство конкретного сбоя IPC.

Проверенный дефект диагностики: `goto done` при отказе оставлял прежний Loading,
ошибка публиковалась только в RAM. Теперь перед очисткой выводится экран
`Stage` / `Code`. Для загрузки звука добавлены промежуточные деления:

- 2: загрузка и проверка sounds.dat;
- 3: packed sounds загружены, проверяется title descriptor;
- 4: проверка восьми title chunks;
- 5: обмен ATTACH с ARM7;
- 6: звуковая инициализация завершена.

В ARM9 IPC сохраняется быстрый путь ожидания 200000 итераций; при отсутствии
ACK добавлено до восьми VBlank. Это даёт ARM7 время при крупном кванте
планирования эмулятора. Гипотеза о причине именно в таком планировании
DraStic **не подтверждена**. Отсутствие ACK по-прежнему возвращает ошибку;
звук не отключается молча. Игровая семантика и ARM7 не менялись.

## Сборка и проверки

Общий BlocksDS 1.24.0 / GCC 16.2.0, Zig 0.13.0, Python 3.14.
Сборка выполнена с флагами playtest10, новый immutable label `playtest11-ru`.
Первый запуск MSYS2 в песочнице завершился Win32 signal pipe error 5;
повтор вне песочницы успешен, `-Werror` сохранён.

```powershell
py -3.14 -B -X utf8 -c "import json,sys; from pathlib import Path; sys.path.insert(0,'ports/nds/tools/performance'); from build import build; f=json.loads(Path('build/nds/n23/playtest10-ru/build-manifest.json').read_text())['feature_selection']; build('playtest11-ru',f)"
$env:ZIG_GLOBAL_CACHE_DIR='C:/Source/DoK-port/build/zig-global-cache'
$env:ZIG_LOCAL_CACHE_DIR='C:/Source/DoK-port/build/zig-local-cache'
& C:/Users/dspev/AppData/Local/Programs/Zig/0.13.0/zig.exe cc -std=c11 -O2 -Wall -Wextra -Werror -Iports/nds/tests/audio_ipc_host ports/nds/tests/test_audio_ipc_wait.c -o build/nds/n23/test_audio_ipc_wait.exe
& ./build/nds/n23/test_audio_ipc_wait.exe
py -3.14 -B -X utf8 ports/nds/tools/run_frontend_emulator.py --target build/nds/n23/playtest11-ru --mode world-smoke --no-storage
py -3.14 -B -X utf8 ports/nds/tools/performance/check_packed_loading.py build/nds/n23/playtest11-ru
py -3.14 -B -X utf8 ports/nds/tools/performance/run_logged.py build/nds/n23/playtest11-ru/live.log.gz ports/nds/tools/run_joint_frontend_emulator.py --target build/nds/n23/playtest11-ru --language 1 --mode live --levels 1 2 3 --steady-presentations 60
py -3.14 -B -X utf8 ports/nds/tools/package_rom.py --target build/nds/n23/playtest11-ru
```

Host-тест исполняет настоящий `platform/audio/arm9.c` с управляемым ACK:
немедленный ответ, ответ на первом/восьмом VBlank, отсутствие ответа,
устаревший serial и отказ FIFO — PASS. ARM7-планировщик не эмулируется этим тестом.
Первый compile теста выявил отключённые Zig assertions; тест явно отменяет
NDEBUG и повторно проходит с активными проверками.
melonDS DS 1.4.0: world-smoke без DLDI PASS, 1277952 index8 и столько же RGB checks.
Packed loading PASS: 169 кадров, 22 PCM clips, сверка оригинальных PCM и
catalog cache. Результат live трёх уровней — в компактной сводке.

ROM `artifacts/nds/dok.nds`, 10163712 bytes, SHA-256
`4a664ef914ec1a13366f79e95f653379990c7bd9b5bf093b61d0b6bc21698290`.
ELF SHA `8760091a2450b23942081af3ee6dbeb1d49e702df2de55b78e6ceb3c9fa612fe`.
Исходники/бинарные входы и manifest сохранены в
`research/evidence/nds/n23/candidates/playtest11-ru/`.
Сводка — `analysis/nds-playtest11-drastic-20261031.json`.

Проверка DraStic/RG DS и экран ошибки локально не выполнены. Для продолжения
нужен холодный запуск нового ROM на RG DS: результат запуска либо новые
числа Stage/Code (или количество делений при отсутствии ошибки).
N23 остаётся открытой; следующий ID N23. N24 не начинать; статусы GBA/SDL2 не менялись.
