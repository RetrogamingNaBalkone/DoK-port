# Playtest12: остановка на пятом этапе в ROCKNIX DraStic

Пользователь проверил playtest11 на RG DS: Loading остаётся на пяти делениях,
экран ошибки не появился. Эмулятор входит в ROCKNIX, запуск через ES-DE.
Точные версия DraStic и дата ROCKNIX не установлены. Локальный DraStic/RG DS
недоступен. Предыдущий ROM SHA
`4a664ef914ec1a13366f79e95f653379990c7bd9b5bf093b61d0b6bc21698290`.

Это сужает участок до `nds_audio_init` либо следующего `publish`, который
читает ARM7 status. Packed sounds и восемь title chunks уже проверены.
Предыдущее предположение о слишком коротком ожидании ACK не подтвердилось
как достаточное исправление. Конкретная причина на устройстве пока неизвестна.

## Изменения

В `platform/audio/arm9.c` найден неограниченный seqlock reader: при постоянно
нечётном status_sequence или непрерывных изменениях он никогда не возвращался.
Теперь быстрый путь ограничен 4096 попытками, затем допускается до восьми
VBlank с повтором. Если согласованный статус не получен, возвращается явный
непригодный статус: ready=0, stale_commands=UINT32_MAX. Это локальная ошибка
снимка, а не число реально принятых ARM7 неверных команд. ABI ARM7 не менялся.
Успешные чтения не ждут VBlank. Инициализация проверяет согласованный snapshot.

На пятом этапе временные коды показаны двумя цифрами под шкалой:

| Код | Следующая операция |
| --- | --- |
| 51 | Обнуление/flush shared storage |
| 52 | Получение uncached mirror |
| 53 | Обнуление PCM ring |
| 54 | Flush PCM ring |
| 55 | Подготовка ATTACH |
| 56 | fifoSendDatamsg |
| 57 | Ожидание ACK |
| 58 | Чтение согласованного статуса после ACK, если ACK получен |
| 59 | Инициализация вернулась, далее runtime publish |

Loading без цифр/5 означает участок до первого callback. Stage/Code на экране
ошибки также сохранены. Callback сбрасывается после init и не рисует загрузку
при игровых командах. Отрисовка boot checkpoints сама даёт дополнительные
VBlank: успешный старт этого ROM на DraStic ещё не докажет причину предыдущего
зависания. Звук и игровые правила не отключались и не упрощались.

## Сборка и проверки

Общие BlocksDS 1.24.0 / GCC 16.2.0, Zig 0.13.0, Python 3.14.
Сборка с теми же feature_selection, label `playtest12-ru`, выполнена через
общий MSYS2 вне песочницы, с `-Werror`. ARM7 остался прежним N18 dependency.

```powershell
py -3.14 -B -X utf8 -c "import json,sys; from pathlib import Path; sys.path.insert(0,'ports/nds/tools/performance'); from build import build; f=json.loads(Path('build/nds/n23/playtest11-ru/build-manifest.json').read_text())['feature_selection']; build('playtest12-ru',f)"
$env:ZIG_GLOBAL_CACHE_DIR='C:/Source/DoK-port/build/zig-global-cache'
$env:ZIG_LOCAL_CACHE_DIR='C:/Source/DoK-port/build/zig-local-cache'
& C:/Users/dspev/AppData/Local/Programs/Zig/0.13.0/zig.exe cc -std=c11 -O2 -Wall -Wextra -Werror -Iports/nds/tests/audio_ipc_host ports/nds/tests/test_audio_ipc_wait.c -o build/nds/n23/test_audio_ipc_wait12.exe
& ./build/nds/n23/test_audio_ipc_wait12.exe
py -3.14 -B -X utf8 ports/nds/tools/performance/run_logged.py build/nds/n23/playtest12-ru/smoke.log.gz ports/nds/tools/run_frontend_emulator.py --target build/nds/n23/playtest12-ru --mode world-smoke --no-storage
py -3.14 -B -X utf8 ports/nds/tools/performance/check_packed_loading.py build/nds/n23/playtest12-ru
py -3.14 -B -X utf8 ports/nds/tools/performance/run_logged.py build/nds/n23/playtest12-ru/boot-steps.log.gz ports/nds/tools/performance/check_audio_boot_steps.py build/nds/n23/playtest12-ru
py -3.14 -B -X utf8 ports/nds/tools/package_rom.py --target build/nds/n23/playtest12-ru
```

Host PASS: шесть ACK cases и четыре status cases, включая постоянно нечётный
status_sequence, завершение записи на первом/восьмом VBlank и быстрый статус.
Тест включает настоящий ARM9 source; fake peer не заменяет эмулятор.
melonDS DS 1.4.0 smoke без DLDI и packed loading PASS. Отдельный read-only
observer подтвердил все этапы 51…59 и последующий 6. PNG визуально проверен:
пять делений и читаемые цифры под ними (образец LCD показывает 55; имя файла
`audio-step56` не является утверждением о точном displayed checkpoint).
Полные маршруты и повтор all3 на playtest12 не запускались: игровой код не менялся.

ROM `artifacts/nds/dok.nds`, 10164736 bytes, SHA-256
`3a8db8a1b484d8f9d80f18d2dcb615466ca4b157ba5cffe7190037f704fc7fc1`.
ELF SHA `ab489209d6bece2a6fb610e4683cc55cd3728b504126bbffba8b4f88469804ad`.
Compiled inputs/manifest — `research/evidence/nds/n23/candidates/playtest12-ru/`;
отчёты/PNG — `research/evidence/nds/n23/playtest12-drastic/`;
сводка — `analysis/nds-playtest12-drastic-20261031.json`.

Следующий шаг: холодный запуск playtest12 в том же ROCKNIX/DraStic через ES-DE,
сообщить номер под шкалой либо Stage/Code. Проверка на устройстве открыта.
Следующий ID N23; N23 не принята, N24 не начата. GBA/SDL2 не менялись.
