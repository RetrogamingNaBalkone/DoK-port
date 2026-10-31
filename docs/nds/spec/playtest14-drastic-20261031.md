# Playtest14: ARM7 в собственной WRAM

Пользователь получил в playtest13 на RG DS/ROCKNIX/DraStic/ES-DE:
`Stage 58`, `Code 00004000`, `Audio initialization failed`.
ROM playtest13 SHA `7a97e8e3e43588a1bb3de99617b264925bcbd3158ecb020d0dda3015b53b503c`.
Это уже явный отказ: send вернулся, но ожидание ARM7 ACK завершилось без
совпадения serial. При отказе coherent status init сначала показывает59,
поэтому сообщение с58 локализует именно command timeout. Это не доказывает,
что ARM7 не писал ACK в canonical RAM: ARM9 читает uncached mirror.

## Кандидат совместимости

Предыдущий ARM7 linker map показывает `iwram=0x037f8000, length=0x18000`:
BlocksDS по умолчанию включает32КиБ shared WRAM в область ARM7 кода/данных.
SDK допускает `__shared_wram_size=0` или32768 (`sys/crts/ds_arm7.ld`).
В N23 теперь выбран0: код/данные размещаются в собственной64КиБ ARM7 WRAM
от `0x03800000`. Новый ELF LOAD segment имеет vaddr0x03800000,17904 file bytes;
linker не обнаружил переполнения. DSi-only segment отдельно0x03000000,
к обычному DS runtime не относится. Стек и резерв рассчитываются SDK linker.

Причина отсутствия ACK в DraStic пока не доказана. Это адресная гипотеза,
а не установленный факт о реализации DraStic. Transport mailbox, decoder,
звук и игровая семантика playtest13 сохранены.
Параметр ARM7_SHARED_WRAM имеет прежний default32768 для старых задач;
N23 build driver явно передаёт0 и фиксирует `arm7_shared_wram_bytes=0`.
Принятый ELF N18 не пересобирался и не перезаписывался.

В случае нового audio init отказа экран выводит raw ACK/expected serial,
Ready/Seq, адрес uncached mirror, ACK/Ready из canonical RAM и FIFO control.
Диагностика не ждёт согласованного status snapshot. Перед canonical чтением
инвалидируются ровно64байта ARM7-owned status на границе cache line32;
после attach ARM9 эти линии не модифицирует. Dirty ARM9 data не теряются.
Эта диагностика не меняет обычный способ IPC и не объявляет mirror сломанным.

## Команды и проверки

Общие BlocksDS1.24.0, GCC16.2.0, Zig0.13.0, Python3.14. Сборка `-Werror`,
MSYS2 вне песочницы. Первая попытка нашла отсутствие include arm9.h в main;
include исправлен, повтор до manifest успешен. Никакой неуспешный ROM не поставлялся.

```powershell
py -3.14 -B -X utf8 -c "import json,sys; from pathlib import Path; sys.path.insert(0,'ports/nds/tools/performance'); from build import build; f=json.loads(Path('build/nds/n23/playtest13-ru/build-manifest.json').read_text())['feature_selection']; build('playtest14-ru',f)"
$env:ZIG_GLOBAL_CACHE_DIR='C:/Source/DoK-port/build/zig-global-cache'
$env:ZIG_LOCAL_CACHE_DIR='C:/Source/DoK-port/build/zig-local-cache'
& C:/Users/dspev/AppData/Local/Programs/Zig/0.13.0/zig.exe cc -std=c11 -O2 -Wall -Wextra -Werror -Iports/nds/tests/audio_ipc_host ports/nds/tests/test_audio_ipc_wait.c -o build/nds/n23/test_audio_ipc_wait14.exe
& ./build/nds/n23/test_audio_ipc_wait14.exe
py -3.14 -B -X utf8 ports/nds/tools/performance/run_logged.py build/nds/n23/playtest14-ru/smoke.log.gz ports/nds/tools/run_frontend_emulator.py --target build/nds/n23/playtest14-ru --mode world-smoke --no-storage
py -3.14 -B -X utf8 ports/nds/tools/performance/run_logged.py build/nds/n23/playtest14-ru/live.log.gz ports/nds/tools/run_joint_frontend_emulator.py --target build/nds/n23/playtest14-ru --language 1 --mode live --levels 1 2 3 --steady-presentations 60
py -3.14 -B -X utf8 ports/nds/tools/package_rom.py --target build/nds/n23/playtest14-ru
```

Host ACK/status/mailbox PASS. Добавлены проверки raw diagnostics при нечётном
status_sequence (без ожидания), canonical ACK/ready и FIFO, а также static
assert на размер96/offset32 структуры с ARM7-owned cache lines.
melonDS DS1.4.0 smoke без DLDI PASS; all3 результат — в сводке.
ARM7 runtime адреса проверены напрямую в ELF program headers и map.
Текущие runtime sources сверены с build manifest. DraStic/RG DS локально нет,
реальное устранение отказа не подтверждено, физическая DS не проверялась.

ROM `artifacts/nds/dok.nds`,10164736bytes, SHA-256
`fef5cd3e8c391f8379bfa9685c1f23bbd096970b9d897f27fe21cf2c8f4ea59b`.
ARM9 ELF SHA `b6f9fa2272521c6a0f4b3d745a590c51e8fa20d744a12827621d55d420c214b9`.
ARM7 ELF SHA `b2828cfbc80a0a636aa834d7860eaa8eb37899f91bfbe90f00c833031bb04701`.
Compiled evidence — `research/evidence/nds/n23/candidates/playtest14-ru/`;
отчёты — `research/evidence/nds/n23/playtest14-drastic/`;
сводка — `analysis/nds-playtest14-drastic-20261031.json`.

Следующий шаг: холодный запуск нового ROM на том же устройстве. При отказе
снять все строки экрана, особенно ACK, Ready, Mirror и RAM ACK. При canonical
ACK/ready и отсутствии mirror ACK исследовать RAM alias; при нулевых обоих
исследовать запуск ARM7/его FIFO callback. Не отключать звук молча.
Следующий ID N23; N23 не принята, N24 не начата. GBA/SDL2 не менялись.
