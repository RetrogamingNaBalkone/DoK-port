# Playtest18b: звуковые команды через RAM control line

Фото playtest17 RG DS/ROCKNIX/DraStic/ES-DE: Stage58/4000,ACK0/1,Ready0,
RAM ACK0; boot9,Loop65,RX0,At1,VB168,Handler2,A7 FIFO C400,IME1,Init1.
Предыдущий ROM SHA `0619e840622cddd34f8cd1b45cd53c429c2219a14d76303e8306b08e5d8f65a4`.
Обход сна позволил main loop двигаться, но теперь остановка отмечена внутри
fifoCheckAddress перед custom callback. Это не доказывает конкретную ошибку
реализации SDK/эмулятора. Повторное изменение RAM map не обосновано фото.

## Платформенное решение

В production embedded ROM custom sound commands больше не идут через SDK FIFO.
ARM9 сохраняет прежнюю48-byte команду в aligned mailbox и flush-ит её; затем
публикует адрес/serial в отдельной32-byte control line в main RAM. ARM7 main
loop проверяет эту линию непосредственно и вызывает прежний командный обработчик.
Shared status/serial ACK/PCM ring/producer/decoder и значимые звуковые операции
сохранены. Системный FIFO SDK для input/storage и прочих служб остаётся.
Custom address handler в RAM-режиме не устанавливается, fifoCheckAddress для
custom commands из main loop удалён. ARM9 RAM timeout path тоже не вызывает FIFO.

Boot buffer расширен64→96байт: первые64 принадлежат ARM7 (trace), последние32
принадлежат ARM9 (NdsAudioControl: magic43424D41,address,serial,reserved[5]).
Адрес по-прежнему получается из ARM7 ELF, не задан константой:0x02380220 в
этой сборке; control0x02380260. Builder проверяет96bytes/range/alignment и
фиксирует `arm7_ram_mailbox=true`. Nonembedded controls сохраняют старый FIFO
transport; ARM7_RAM_MAILBOX default0, N23 embedded явно передаёт1.

Порядок публикации: mailbox flush; address/magic control flush со старым serial;
новый serial и второй control flush. Старый serial уже обработан и не вызывает
повтор команды. ARM7 без main-RAM cache принимает каждый serial один раз.
Команда и control line неизменяемы до ACK; timeout latch запрещает reuse даже
после позднего ACK. ARM9 и ARM7 не пишут в одну cache line. ARM7 исполняет
команду в короткой critical section относительно audio IRQ, не выполняя внутри
её декодирование PCM. Публичные командные форматы/игровая семантика не менялись.

Первый candidate `playtest18-ru` собран, но не поставлялся. При review добавлена
critical section и удалён неиспользуемый custom FIFO handler; окончательный
immutable target — **playtest18b-ru**. Полные промежуточные evidence не переписаны.
At1 теперь означает RAM mailbox polling, а не fifoCheckAddress; At2…5,VB/Handler
сохранены. Busy idle playtest17 остаётся: повышенная idle-нагрузка и отсутствие
аппаратного power-профиля — известные ограничения, не закрытые N23/N24.

## Воспроизводимость и проверки

Общие BlocksDS1.24.0/GCC16.2.0/Python3.14/Zig0.13.0; `-Werror`.
Сборка общего MSYS2 вне песочницы. N18 ELF не перезаписывается.

```powershell
py -3.14 -B -X utf8 -c "import json,sys; from pathlib import Path; sys.path.insert(0,'ports/nds/tools/performance'); from build import build; f=json.loads(Path('build/nds/n23/playtest17-ru/build-manifest.json').read_text())['feature_selection']; build('playtest18b-ru',f)"
$env:ZIG_GLOBAL_CACHE_DIR='C:/Source/DoK-port/build/zig-global-cache'
$env:ZIG_LOCAL_CACHE_DIR='C:/Source/DoK-port/build/zig-local-cache'
& C:/Users/dspev/AppData/Local/Programs/Zig/0.13.0/zig.exe cc -std=c11 -O2 -Wall -Wextra -Werror -Iports/nds/tests/audio_ipc_host ports/nds/tests/test_audio_ram_mailbox.c -o build/nds/n23/test_audio_ram_mailbox.exe
& ./build/nds/n23/test_audio_ram_mailbox.exe
& C:/Users/dspev/AppData/Local/Programs/Zig/0.13.0/zig.exe cc -std=c11 -O2 -Wall -Wextra -Werror -Iports/nds/tests/audio_ipc_host ports/nds/tests/test_audio_ipc_wait.c -o build/nds/n23/test_audio_ipc_wait18.exe
& ./build/nds/n23/test_audio_ipc_wait18.exe
py -3.14 -B -X utf8 ports/nds/tools/performance/run_logged.py build/nds/n23/playtest18b-ru/boot-control.log.gz ports/nds/tools/performance/check_audio_boot_steps.py build/nds/n23/playtest18b-ru
py -3.14 -B -X utf8 ports/nds/tools/performance/run_logged.py build/nds/n23/playtest18b-ru/title-audio.log.gz ports/nds/tools/performance/check_title_audio.py build/nds/n23/playtest18b-ru
py -3.14 -B -X utf8 ports/nds/tools/performance/run_logged.py build/nds/n23/playtest18b-ru/live.log.gz ports/nds/tools/run_joint_frontend_emulator.py --target build/nds/n23/playtest18b-ru --language 1 --mode live --levels 1 2 3 --steady-presentations 60
py -3.14 -B -X utf8 ports/nds/tools/package_rom.py --target build/nds/n23/playtest18b-ru
```

Actual ARM9 RAM transport host PASS: порядок cache publication, последовательные
команды, немедленный/1VB/8VB ACK, missing/stale ACK, запрет reuse после timeout
и позднего ACK. Любой вызов custom FIFO в этом тесте — assert failure.
Старый nonembedded FIFO/status/raw host regression PASS.
melonDS DS1.4.0: boot observer PASS, прочитаны реальные96байт boot/control,
проверены magic/address/serial, RX/ACK, рост Loop/VB. Первичный boot test до
добавления control assertions также прошёл; расширение проверено отдельным
boot-control запуском, а не выдано за прежний результат.
Production title waveform PASS: все474361 source samples, whole coefficient
0,9924338014; проверка не зависит от отключённых display-only frontend fixtures.
Результат all3 — в сводке. DraStic/RG DS локально нет: устранение сбоя на
устройстве не подтверждено; реальная DS, power и full N23 FPS/маршруты открыты.

ROM `artifacts/nds/dok.nds`,10165760bytes, SHA
`dc2aa5c242e2a0d6c54e0016b66a8c6f47cd0250f07d832917ee0e5e7608b087`.
ARM9 ELF SHA `0aaaa07235322c7a90cfe410b215e9507c250fe5c91c65ec59eecb87742ae443`.
Compiled evidence — `research/evidence/nds/n23/candidates/playtest18b-ru/`;
отчёты — `research/evidence/nds/n23/playtest18-drastic/`;
сводка — `analysis/nds-playtest18-drastic-20261031.json`.

Следующий шаг: холодный запуск на RG DS. При успехе проверить звук и вход на
уровень; затем отдельно оптимизировать idle. При отказе получить полный экран:
At1/RX теперь относятся непосредственно к RAM mailbox, версия bootstrap marker
не менялась. Следующий ID N23; N23 не принята, N24 не начата. GBA/SDL2 не менялись.
