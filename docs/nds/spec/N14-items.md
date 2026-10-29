# N14: предметы, характеристики и инвентарь

29 октября 2026. Независимый C11-модуль `ports/nds/core/items.c`, публичный
контракт `ports/nds/include/dok/items.h`. Готово (эмулятор).

## Исходная модель

Образцы PKR.EXE SHA-256
`12f78afc0eba05f76b5dc0bb99da38d7daa20585024c0ba958fe828aa3864d28` и PKE.EXE
`cce7bc5993d0734ad87e93db37a70b8b52faedfff14ac48d6e317e5ac996a37b`.
Новый read-only Ghidra export: `research/evidence/nds/n14/original/{ru,en}/`;
скрипт `tools/ghidra/ExportNdsGameplay.java`, manifest и ghidra.log рядом.
EXE SHA проверен до/после; SHA программы Ghidra также проверяется скриптом.
Автоматический C хранится только как доказательство, в сборку не входит.

Все следующие адреса — Ghidra NE loader, не файловые смещения или DPMI:
stats RU1068:1CC2 / EN1020:2276; weapon registration RU1068:1EEC /
EN1020:249C; pickup→floor→motion RU1068:2127 / EN1020:26D7;
item callback RU1068:2F8A / EN1020:362D. ABI: cdecl16far, this offset:segment,
category/value WORD, RETF, caller cleanup, DS=SS. В новом коде far pointers
заменены полными N03 object references (epoch, ID, generation).

Положительное пополнение при current==maximum отклоняется. В остальных случаях
ADD16 выполняется до signed clamp: отрицательное →0, превышение →maximum.
Изменения максимума ADD16 заполняют current без дополнительного clamp.
Ключи — OR; score — ADD16; map_available становится1 независимо от value.
Инициализация читает все исходные поля героя +5E…72 и +76…7A; maxima
не выводятся из current. +D8 score и world+0C map начинаются с0.

Fallback поддерживает только известные поля категорий4–9/12–14. Неизвестные
категории, включая11, безопасно отклоняются. Нативное произвольное смещение
указателя не воспроизводится; в исходных LEVEL таких предметов нет.
100/101/102/1000 остаются N11. Реально размещённые предметы: health±10/15,
charge+50, keys1/2/4, map200, weapon slot1/2 с max_charge+50.

## Владение и порядок

`dok_items_pickup` вызывается для текущей позиции перед N11 floor и движением.
Он допускается только вне collision context и render lease (последний обязан
гарантировать вызывающий shell). Class3 требует accepted stats. Class10
продолжает исходную регистрацию независимо от stat return. После notification
очищается одна клетка TMap, если category!=1000. Shared definition, flags,
animation и другие клетки того же glyph сохраняются. Полный ресурс оставляет
предмет, отрицательное здоровье расходует его даже при health0.

Повтор оружия снова пополняет максимум, сохраняет прежнюю ссылку слота и задаёт
pending. Первое оружие выбирается сразу. `dok_items_next_weapon` ограниченно
проверяет9 слотов, пустой и единственный инвентарь не меняются; N15 завершает
hide/show. Это безопасный цикл NDS, не восстановление небезопасного чтения DOS.

Синхронные STAT/PICKUP/WEAPON/MAP outputs содержат скаляры и object reference;
sink не может повторно входить в модуль. UI разрешает локализованные +58 text
и sound через N05 presentation. Переключение языка не переписывает stats/maps.
Предмет карты не заполняет visited: он только разрешает будущие правила N20.
Ни один новый модуль не пишет на диск. Связь с N11 проверена реальными key
pickups → event Use → door event registration, ключи не расходуются.

## Проверка и команды

Общий Python3.14.6:
`C:/Users/dspev/AppData/Local/Python/pythoncore-3.14-64/python.exe`.
Ниже `PY` означает этот исполняемый файл.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File ports/nds/tools/export_gameplay_evidence.ps1
& $PY -B -X utf8 ports/nds/tools/check_items.py
& $PY -B -X utf8 ports/nds/tools/build_gameplay.py n14
& $PY -B -X utf8 ports/nds/tools/run_items_emulator.py
& $PY tools/reports/summarize_reports.py
```

Zig0.13.0 строгий C11: 1 302 672 checks,626 placements,1284 notifications,
failures0. Linux x86_64 musl static скомпилирован, не исполнялся.
Unicorn2.1.4 исполнил4637 stats vectors и8 item callbacks **на язык**;
арифметика и persistence сверены с NDS C. Перехвачены только HUD/text/sound
far calls. Это изолированные исходные функции, не полная DOS-сессия.

BlocksDS1.24.0-1/GCC16.2.0: реальная ARM9 ROM выполнила ту же полную suite;
проверены совпавшие checks/placements/notifications, DS mode и неизменность
FAT до/после исполнения и unload. Observer читает RAM, не изменяет её.
melonDS DS libretro1.4.0, core SHA
`c7e6b58c667075a2a9868f2c3ba7a98e24200410a6da3051a7c555f1f0eef8ef`;
настройки и наблюдения в `research/evidence/nds/n14/emulator.json`.
Тестовая ROM без сцены проверяет логику, не игровой FPS или исчезновение на GPU.
Production-модель вывода N12 читает изменённую TMap; совместный живой вывод
проверяется в N15–N17P. Физическая DS/DS Lite остаётся N24.

Полные команды, source pins, ROM/ELF/map SHA — host-check.json и
`build/nds/n14/arm9/build-manifest.json`; компактный итог
`analysis/nds-n14-items.json`. Предыдущие исходники/артефакты N13P не изменялись.
Новая items state занимает176 B на ARM9; world/collision заимствуются.
Тестовый workspace — отдельное диагностическое размещение, не бюджет полной игры.
