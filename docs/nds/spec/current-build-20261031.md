# Автономная NDS-сборка от 31 октября 2026 года

31 октября 2026: пользователь подтвердил запуск loading20 в DraStic/RG DS.
Открыт дефект аудио: фрагменты title PCM в уровнях и пропуски выстрелов.
Поставлен кандидат [audio21](drastic-audio21-20261031.md): title ring очищается,
resident once целиком готовится и играет SOUND_ONE_SHOT, hardware completion
освобождает busy независимо от задержки IRQ. Boot/input, full title waveform
и короткий LEVEL1 audio transition PASS; выстрелы не проверены (на старте нет оружия).
ROM `artifacts/nds/dok-audio21.nds` / `dok.nds`, SHA
`7df6f228302c80b4dacfeb7a3011b34b4e4d08896eb63b7e5cd87a5ee60e9c18`.
Корневая причина DraStic пока не доказана; повтор на устройстве открыт.
Маршруты/набор уровней не прогонялись. Следующий ID N23, N24 не начата.
Ниже — история предыдущих сборок.

Текущий кандидат — [loading20](drastic-loading20-20261031.md):
`artifacts/nds/dok.nds` и `dok-loading20.nds`, SHA
`332627c7a265fad6578da7c301e1ed8cc344c94f019f7addf95c414ebf243b8e`.
Исправлен найденный путь периодической FIFO-отправки ввода; на RG DS пока
не подтверждено. Проверялась только загрузка, не уровни. Ниже — история.

Для текущего анализа — отдельный [boot diagnostic19](drastic-loading-analysis19-20261031.md):
`artifacts/nds/dok-bootdiag19.nds`, SHA `ba8e7d7e6c2a3b291799a1b899104776f437a5c19198e85c1bc331bf0e1cf338`.
Это диагностика IRQ-контекста, не заявленное исправление. `dok.nds` остаётся18b,
его boot в DraStic по фото пользователя неисправен. Прогоны уровней прекращены
до локализации; новый контекст сопоставлять с ELF diagnostic19. Ниже — история.

Текущая поставка — [playtest18b: RAM sound mailbox](playtest18-drastic-20261031.md).
`artifacts/nds/dok.nds`, SHA `dc2aa5c242e2a0d6c54e0016b66a8c6f47cd0250f07d832917ee0e5e7608b087`.
Повтор RG DS/ROCKNIX DraStic открыт. Ниже — история.

Текущая поставка — [playtest17: обход ARM7 BIOS idle wait](playtest17-drastic-20261031.md).
`artifacts/nds/dok.nds`, SHA `0619e840622cddd34f8cd1b45cd53c429c2219a14d76303e8306b08e5d8f65a4`.
Повтор RG DS/ROCKNIX DraStic открыт. Ниже — история.

Текущая поставка — [playtest16: ARM7 idle Halt](playtest16-drastic-20261031.md).
`artifacts/nds/dok.nds`, SHA `9426cdb8ace55956968672ebc875c8cfb4f05c1f810e79ac64acb088c500159f`.
Повтор RG DS/ROCKNIX DraStic открыт. Ниже — история.

Текущая поставка — [playtest15: ARM7 boot/polling FIFO](playtest15-drastic-20261031.md).
`artifacts/nds/dok.nds`, SHA `7f295ef35aa9e77076c1f9135a9c9a984dcde04f3fb2befff541dd7da6da393d`.
Повтор RG DS/ROCKNIX DraStic открыт. Ниже — история.

Текущая поставка — [playtest14: ARM7 private WRAM](playtest14-drastic-20261031.md).
`artifacts/nds/dok.nds`, SHA `fef5cd3e8c391f8379bfa9685c1f23bbd096970b9d897f27fe21cf2c8f4ea59b`.
Повтор RG DS/ROCKNIX DraStic после Stage58/4000 открыт. Ниже — история.

Текущая поставка — [playtest13: звуковой IPC mailbox](playtest13-drastic-20261031.md).
`artifacts/nds/dok.nds`, SHA `7a97e8e3e43588a1bb3de99617b264925bcbd3158ecb020d0dda3015b53b503c`.
Повтор RG DS/ROCKNIX DraStic после кода56 открыт. Ниже — история.

Текущая поставка — [playtest12: Loading/5 в ROCKNIX DraStic](playtest12-drastic-20261031.md).
`artifacts/nds/dok.nds`, SHA `3a8db8a1b484d8f9d80f18d2dcb615466ca4b157ba5cffe7190037f704fc7fc1`.
На RG DS требуется повторный запуск с фиксацией номера под шкалой. Ниже — история.

Текущая поставка — [playtest11: диагностика DraStic/RG DS](playtest11-drastic-20261031.md).
`artifacts/nds/dok.nds`, SHA `4a664ef914ec1a13366f79e95f653379990c7bd9b5bf093b61d0b6bc21698290`.
Совместимость DraStic на RG DS пока не подтверждена. Записи ниже — история.

Текущая поставка — [playtest10: дверь и сообщения по DOS](playtest10-20261031.md).
`artifacts/nds/dok.nds`, SHA `7ad0033563651ffbccef3dfcdfaa6fda8a320dc030adc46cbae1b0645e67efa5`.
Следующие записи playtest09/07/03 — история предыдущих кандидатов.

Актуальный кандидат — [playtest09: удержание кадра и новый HUD](playtest09-20261031.md).

Актуальный кандидат заменён на [playtest07: зависание, загрузка и FONT8](playtest07-20261031.md).
Исторические хеши и команды ниже не описывают новый ROM.

Текущая поставка заменена на [playtest03: исправления плейтеста](playtest-fixes-20261031.md).
Ниже сохранена история standalone01 и её хеши.

Готовый файл: **`artifacts/nds/dok.nds`**. Один ROM содержит движок, графику,
звук и тексты RU/EN. Язык меняется в главном меню и меню паузы. Отдельных
`assets.ndp`, `ui.dat`, `frontend.dat` для запуска больше не требуется.
Старая выдача из `build/` с внешними файлами заменена этим ROM.

ROM: 9554432 bytes, SHA-256
`bbab7275fcdc32e025e8e249de68dee20ec00086865c1be2c06b6258aae48b25`.
ELF: `5fe30dcf4094d59d1f8b86edd1f316369b67fd7ac54da63d1959a13a7dc76bab`.
Промежуточная компиляция: `build/nds/n23/standalone01-ru/`; начальный язык RU,
оба языка доступны в одном ROM. Готовые файлы выдаются только из `artifacts/`.

## Ресурсы и реальная DS

У обычной DS/DS Lite нет microSD-слота: homebrew обычно запускается через
flashcart в слоте DS-картриджа. Встроенные ресурсы открываются через NitroFS,
который поддерживает чтение картриджа и чтение ROM через загрузчик flashcart.
Используется `nitroFSInit(NULL)` из установленного BlocksDS 1.24.0;
[официальное описание NitroFS](https://blocksds.skylyrac.net/docs/guides/filesystem/).
Физическая DS/DS Lite в этой задаче не проверялась; совместимость конкретного
flashcart/loader остаётся в N24.

Внутренний пакет N04 и алгоритм чтения по рабочему набору сохранены. Банк
не включается в загружаемую секцию ARM9 и не копируется целиком в RAM.
Настройки — необязательный `fat:/dok/settings.dat`, каталог создаётся при
изменении настроек. Отсутствие записываемого хранилища не мешает игре;
выбранный язык тогда действует только до выключения. Игровых сохранений нет.
Исторические SCN/контроли сохраняют внешний FAT-путь (`external` в драйвере).

## Сборка и выполненные проверки

Общий SDK: BlocksDS 1.24.0, GCC 16.2.0, Python 3.14; полный package lock проверен
драйвером. Флаги игры — C22, добавлен NitroFS. Бинарная упаковка изменена,
игровая семантика не менялась. Ошибка `warn_unused_result` при первой компиляции
исправлена; итоговая сборка прошла с `-Werror`. MSYS2 запускается вне песочницы
из-за ограничения Win32 signal pipe в песочнице.

```powershell
py -3.14 -B -X utf8 -c "import sys,json; from pathlib import Path; sys.path.insert(0,'ports/nds/tools/performance'); from build import build; f=json.loads(Path('build/nds/n23/candidate22-ru/build-manifest.json').read_text())['feature_selection']; build('standalone01-ru',f)"
py -3.14 -B -X utf8 ports/nds/tools/run_frontend_emulator.py --target build/nds/n23/standalone01-ru --mode world-smoke
py -3.14 -B -X utf8 ports/nds/tools/run_frontend_emulator.py --target build/nds/n23/standalone01-ru --mode world-smoke --no-storage
py -3.14 -B -X utf8 ports/nds/tools/run_frontend_emulator.py --target build/nds/n23/standalone01-ru --mode settings
py -3.14 -B -X utf8 ports/nds/tools/run_frontend_emulator.py --target build/nds/n23/standalone01-ru --mode restart --reuse-image "build/nds/n23/standalone01-ru/frontend-settings/save/melonDS DS/dldi_sd_card.bin"
py -3.14 -B -X utf8 ports/nds/tools/run_frontend_emulator.py --target build/nds/n23/standalone01-ru --mode readonly --no-storage
py -3.14 -B -X utf8 ports/nds/tools/run_joint_frontend_emulator.py --target build/nds/n23/standalone01-ru --language 1 --mode live --levels 1 2 3 --steady-presentations 60
py -3.14 -B -X utf8 ports/nds/tools/package_rom.py --target build/nds/n23/standalone01-ru
```

Имена компиляций immutable; для повторной сборки выбрать новый label. Прогоны
выполнялись через `performance/run_logged.py <log> <script> <args>`; во время
работы параллельно обновлён логгер на `.gz`, формат конкретного лога указан
в evidence manifest. Дополнительно собран `standalone-external-control-ru`
с теми же флагами плюс `external`, выполнен `world-smoke` со старым FAT-путём.

Все перечисленные проверки PASS на закреплённом melonDS DS 1.4.0. Начальный
FAT автономных проверок не содержит внешних игровых ресурсов; smoke создаётся
без каталога DOK. При отключённом DLDI игра стартует и проходит паузу/resume;
смена языка не требует успешной записи настроек. На доступном носителе меняется
только SETTINGS.DAT, выбор EN восстанавливается после перезапуска того же ROM.
Live-проверка охватила три уровня, touch-карту и RU→EN→RU→EN с сохранением
игрового состояния. Это ограниченные проверки загрузки/экрана, не полное
прохождение и не закрытие FPS-критериев N23.

RAM: static startup507904, managed2293760, SDK headroom212992, reserve655360;
closure3670016 bytes. Пакет ROM независимо разобран по FNT/FAT; все три файла
совпали с закреплёнными build inputs по размеру и SHA. `artifacts/nds/manifest.json`
содержит SHA опубликованного ROM и ресурсов.

Постоянные compiled inputs/ROM/ELF/map:
`research/evidence/nds/n23/candidates/standalone01-ru/`. Отчёты и используемые
наблюдатели: `research/evidence/nds/n23/standalone01/`. Полный пакет ресурсов
и бинарные файлы остаются вне Git. Готовый файл — `artifacts/nds/dok.nds`.
По указанию пользователя дополнительные прогоны прекращены.

N23 остаётся открытой; следующий ID — N23. N24 не начата. Полная приёмка FPS
и физическая DS/DS Lite этой сменой упаковки не закрываются.
