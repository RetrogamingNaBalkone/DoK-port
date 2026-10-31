# Playtest15: наблюдаемый запуск ARM7 и polling FIFO

Фото пользователя для playtest14 на RG DS/ROCKNIX/DraStic/ES-DE:

```text
Stage 58, Code 00004000
ACK 00000000 / 00000001
Ready 0 Seq 0
Mirror 02C52340
RAM ACK 00000000 Ready 0
FIFO 8400
```

ROM playtest14 SHA `fef5cd3e8c391f8379bfa9685c1f23bbd096970b9d897f27fe21cf2c8f4ea59b`.
ACK не наблюдается ни через alias, ни через canonical RAM; это не подтверждает
гипотезу stale uncached mirror. Перемещение кода ARM7 само по себе отказ не
устранило. FIFO8400 записан как наблюдение, не объявлен доказательством причины.
Нельзя из этих полей определить, стартовал ли main ARM7 и вызывался ли callback.

## Изменение

ARM7 main loop вызывает `fifoCheckAddress(FIFO_USER_01)` перед producer/wait.
Штатный SDK API самостоятельно дренирует RX FIFO и вызывает установленные
callbacks, поэтому этот путь не требует доставки receive IRQ для диспетчеризации
уже полученной команды. ARM9 fallback ожидания ACK также обслуживает входящие
сообщения штатным polling API. IRQ handlers сохранены; к аппаратной очереди
напрямую не обращаемся. Reference: установленный fifocommon.h и
[BlocksDS fifosystem.c](https://github.com/blocksds/libnds/blob/master/source/common/fifosystem.c),
`fifoCheckAddress` / `fifoReadRxFifoAndProcessBuffer`.
Это обход возможного отсутствия IRQ; причина в DraStic всё ещё не доказана.

Добавлена `g_audio_arm7_boot[16]`,64байта, aligned32, в `.crt0` ARM7 — его
зарезервированном main-RAM load region, вне ARM9 arena. Фиксированного адреса
нет: builder сначала собирает ARM7, читает ELF symbol, проверяет размер/range/
alignment и генерирует `audio_boot.inc` для ARM9. В этом ROM адрес0x02380220.
Затем собирается ARM9/ROM; manifest фиксирует header и соответствующий ARM7 ELF.
Буфер не лежит в private WRAM, которая ARM9 недоступна. ARM9 только инвалидирует
его выделенные cache lines и читает; ARM7 пишет без кеша.

На ошибке к прежним данным добавлены A7 boot/Loop/RX/step/Addr/FIFO/IME/IE/Init.
Важные boot stages:1 перед enableSound,2 перед readUserSettings,3 перед
ledBlink/touchInit,4 перед irqInit,5 перед fifoInit,6 перед installSystemFIFO,
7 перед camera/handler,8 перед timer/RTC/VBlank setup,9 — основной loop.
Boot0 означает, что маркер входа main ещё не наблюдался, а не доказательство
конкретного отказа CRT0. Magic37424F41 подтверждает, что прочитан нужный буфер.
RX счётчик увеличивается при входе callback; step1 вход,2 неверный адрес,
3 неверный magic,4 команда распознана,5 отказ pointer validation ATTACH,
6 ACK записан. Loop — счётчик обходов, Init — результат fifoInit.
Диагностика локальная; устройство/DraStic не доступны для воспроизведения здесь.

## Проверки и команды

Общие BlocksDS1.24.0/GCC16.2.0, Zig0.13.0, Python3.14, `-Werror`.
Сборка через общий MSYS2 вне песочницы. ARM7 private WRAM и mailbox transport
playtest14 сохранены; PCM/декодер/игровая логика не менялись.

```powershell
py -3.14 -B -X utf8 -c "import json,sys; from pathlib import Path; sys.path.insert(0,'ports/nds/tools/performance'); from build import build; f=json.loads(Path('build/nds/n23/playtest14-ru/build-manifest.json').read_text())['feature_selection']; build('playtest15-ru',f)"
$env:ZIG_GLOBAL_CACHE_DIR='C:/Source/DoK-port/build/zig-global-cache'
$env:ZIG_LOCAL_CACHE_DIR='C:/Source/DoK-port/build/zig-local-cache'
& C:/Users/dspev/AppData/Local/Programs/Zig/0.13.0/zig.exe cc -std=c11 -O2 -Wall -Wextra -Werror -Iports/nds/tests/audio_ipc_host ports/nds/tests/test_audio_ipc_wait.c -o build/nds/n23/test_audio_ipc_wait15.exe
& ./build/nds/n23/test_audio_ipc_wait15.exe
py -3.14 -B -X utf8 ports/nds/tools/performance/run_logged.py build/nds/n23/playtest15-ru/smoke.log.gz ports/nds/tools/run_frontend_emulator.py --target build/nds/n23/playtest15-ru --mode world-smoke --no-storage
py -3.14 -B -X utf8 ports/nds/tools/performance/run_logged.py build/nds/n23/playtest15-ru/boot.log.gz ports/nds/tools/performance/check_audio_boot_steps.py build/nds/n23/playtest15-ru
py -3.14 -B -X utf8 ports/nds/tools/performance/run_logged.py build/nds/n23/playtest15-ru/live.log.gz ports/nds/tools/run_joint_frontend_emulator.py --target build/nds/n23/playtest15-ru --language 1 --mode live --levels 1 2 3 --steady-presentations 60
py -3.14 -B -X utf8 ports/nds/tools/package_rom.py --target build/nds/n23/playtest15-ru
```

Host ACK/status/mailbox/raw diagnostics PASS; добавлен сценарий, в котором ACK
появляется только от вызова polling API. Это проверка ARM9 fallback, не
эмуляция отказа IRQ в настоящем ARM7. melonDS DS1.4.0 smoke без DLDI PASS.
Read-only boot observer проверил51…59,6, magic и boot9,Loop151,RX2,step6,
Init1,IME1: ARM7 дошёл до main loop и ACK. Результат all3 — в сводке.
Систематическое подавление IRQ не моделировалось, исправление на DraStic не доказано.

ROM `artifacts/nds/dok.nds`,10165760bytes, SHA-256
`7f295ef35aa9e77076c1f9135a9c9a984dcde04f3fb2befff541dd7da6da393d`.
ARM9 ELF SHA `6bd75bfac38991ead62365ac5694365079451ba5acee3cc5f310a817afc5ed8c`.
Compiled evidence — `research/evidence/nds/n23/candidates/playtest15-ru/`;
отчёты — `research/evidence/nds/n23/playtest15-drastic/`;
сводка — `analysis/nds-playtest15-drastic-20261031.json`.

Следующий шаг: холодный запуск в том же ROCKNIX/DraStic. При ошибке получить
весь экран с A7 boot/Loop/RX/step; сначала исследовать обнаруженный последний
этап, не менять снова карту памяти без доказательств. Следующий ID N23,
N23 не принята, N24 не начата. GBA/SDL2 и оригиналы не менялись.
