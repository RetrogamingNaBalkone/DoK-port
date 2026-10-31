# Playtest13: команда ARM7 через адрес FIFO

RG DS, ROCKNIX DraStic, запуск через ES-DE: пользователь сообщил **56** на
playtest12 (`3a8db8a1b484d8f9d80f18d2dcb615466ca4b157ba5cffe7190037f704fc7fc1`).
Код выставляется перед `fifoSendDatamsg`; экран асинхронен, поэтому наблюдение
сужает проблему до отправки/непосредственно следующего checkpoint, но само
по себе не доказывает внутреннюю инструкцию зависания эмулятора.

## Изменение транспорта

Старый путь передавал 48 байт команды плюс заголовок — 13 слов FIFO.
Новый ARM9 помещает команду в отдельный 32-byte-aligned mailbox в main RAM,
делает flush и отправляет `fifoSendAddress`. ARM7 принимает адрес callback,
проверяет диапазон/выравнивание и копирует команду до исполнения/ACK.
Через FIFO передаётся одно слово; формат команды, серийный ACK, PCM,
кольцо и механизм воспроизведения сохранены. Использован штатный API SDK,
без вмешательства в аппаратную FIFO очередь системных сообщений.
Описание API — в установленном `libnds/include/nds/fifocommon.h`;
референс реализации — [BlocksDS fifosystem.c](https://github.com/blocksds/libnds/blob/master/source/common/fifosystem.c).
Это кандидат совместимости; неисправность именно datamsg в DraStic ещё не доказана.

Команда допускается только одна одновременно. Mailbox не переиспользуется
после timeout/отказа send: локальная latch блокирует последующие команды,
включая очистку. Даже поздний ACK не снимает latch и не разрешает перезапись.
Это исключает чтение изменённой команды опоздавшим ARM7.

Оба CPU пересобраны. N23 build driver теперь выбирает собственный
`build/nds/n23/<label>/arm7/` через ARM7_OUT/ARM7ELF и фиксирует именно этот
ELF в manifest. Принятый N18 ARM7 не перезаписан: SHA до/после
`825e93dc4af325e1aa0c974dd34f631bf2bccd0b903d4e6c3706c252b710795a`.
Новый ARM7 SHA `799399174a89bd6d00754a1fd3375e5a7337993882b75afa9806b733be9c201b`.
Игровая семантика, данные оригиналов и GBA/SDL2 не менялись.

## Команды и проверки

Общие BlocksDS 1.24.0 / GCC 16.2.0, Zig 0.13.0, Python 3.14;
ARM9 Thumb ARM946E-S и ARM7 Thumb ARM7TDMI, codec ARM. `-Werror`.
MSYS2-сборка выполнена вне песочницы из-за Win32 signal pipe ограничения.

```powershell
py -3.14 -B -X utf8 -c "import json,sys; from pathlib import Path; sys.path.insert(0,'ports/nds/tools/performance'); from build import build; f=json.loads(Path('build/nds/n23/playtest12-ru/build-manifest.json').read_text())['feature_selection']; build('playtest13-ru',f)"
$env:ZIG_GLOBAL_CACHE_DIR='C:/Source/DoK-port/build/zig-global-cache'
$env:ZIG_LOCAL_CACHE_DIR='C:/Source/DoK-port/build/zig-local-cache'
& C:/Users/dspev/AppData/Local/Programs/Zig/0.13.0/zig.exe cc -std=c11 -O2 -Wall -Wextra -Werror -Iports/nds/tests/audio_ipc_host ports/nds/tests/test_audio_ipc_wait.c -o build/nds/n23/test_audio_ipc_wait13.exe
& ./build/nds/n23/test_audio_ipc_wait13.exe
py -3.14 -B -X utf8 ports/nds/tools/performance/run_logged.py build/nds/n23/playtest13-ru/smoke.log.gz ports/nds/tools/run_frontend_emulator.py --target build/nds/n23/playtest13-ru --mode world-smoke --no-storage
py -3.14 -B -X utf8 ports/nds/tools/performance/run_logged.py build/nds/n23/playtest13-ru/live.log.gz ports/nds/tools/run_joint_frontend_emulator.py --target build/nds/n23/playtest13-ru --language 1 --mode live --levels 1 2 3 --steady-presentations 60
py -3.14 -B -X utf8 ports/nds/tools/performance/run_logged.py build/nds/n23/playtest13-ru/frontend-full.log.gz ports/nds/tools/run_frontend_emulator.py --target build/nds/n23/playtest13-ru --mode full
py -3.14 -B -X utf8 ports/nds/tools/package_rom.py --target build/nds/n23/playtest13-ru
```

Host PASS: ACK/status cases предыдущей итерации, flush mailbox перед send,
единственный адрес, запрет перезаписи после timeout и позднего ACK.
melonDS DS 1.4.0 world-smoke без DLDI PASS. Результат all3 — в сводке.
`frontend --mode full` завершился FAIL: требует отключённый в production
display-only POPAL fixture. Это неподходящий режим полной приёмки этого ROM;
ошибка не скрыта повтором. До fixture оба полных проигрывания title завершились,
waveform oracle проверил PCM оригинала (whole correlation >0,99). Эти частичные
звуковые проверки не объявляются PASS всего full-flow.

ROM `artifacts/nds/dok.nds`, 10164736 bytes, SHA-256
`7a97e8e3e43588a1bb3de99617b264925bcbd3158ecb020d0dda3015b53b503c`.
ARM9 ELF SHA `970d0b577853e9b0d4e1356f0516f9da15118943434063df8bfea1b997b7f1c9`.
Compiled inputs/ROM/manifest:
`research/evidence/nds/n23/candidates/playtest13-ru/`.
Отчёты — `research/evidence/nds/n23/playtest13-drastic/`;
сводка — `analysis/nds-playtest13-drastic-20261031.json`.

Следующий шаг: холодный запуск на том же RG DS/ROCKNIX/DraStic и результат
либо код под шкалой/Stage/Code. Локального DraStic/RG DS нет; совместимость
не подтверждена. Следующий ID N23; FPS/полные маршруты открыты, N24 не начата.
