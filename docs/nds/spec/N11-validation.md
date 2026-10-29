# N11: проверка взаимодействий и дверей

Статус приёмки, текущие хеши и следующий ID определяет
[паспорт N11](../tasks/N11.md). Архитектура —
[N11-interactions](N11-interactions.md), исходные доказательства —
[N11-evidence](N11-evidence.md), изображение — [N11-render](N11-render.md),
интеграция ARM9 — [N11-runtime](N11-runtime.md).

## Принятые зависимости и расширение N10

До начала работы проверены 439 принятых входов N01–N10. Для нового прямого
перемещения conveyor потребовалось расширить публичный collision API:
исходный wrapper `PKR1068:10A4` / `PKE1020:15AE` вызывает resolver один раз,
тогда как N09 идёт тремя подшагами. При штатном C2=4 исходный conveyor
перемещает героя на 350 единиц. Допуск обычного подшага меньше 240 здесь
неприменим; деление одного вызова на несколько изменило бы occupancy и результат.

`dok_collision_displace` добавлена в конец `collision.c`, прототип — перед
завершающим include guard. Прежние тела функций и объявления сохранены байтами.
Их точные копии находятся в
`research/evidence/nds/n11/prior-source/ports/nds/{core/collision.c,include/dok/collision.h}`.
`source-supersession.json` фиксирует исходные и новые SHA, архивные пути и причину.
N10 ROM, reports и manifests не переписываются.

Frozen `initial-dependency-integrity.json` N11 содержит 439 pins: 437 прежних
текущих входов и две архивные версии расширенных файлов. Checker проверяет
также вложенный supersession manifest, текущие версии двух файлов и точное
байтовое сохранение прежнего кода. Новые источники входят в собственный
N11 source manifest. Эта проверка заменяет требование совпадения текущего
collision API со старым хешем; она не скрывает изменение исходников.

Для следующей задачи базой служат этот snapshot и принятый итог N11, включая
его новые source/build manifests. Нельзя заново собирать прежнюю цепочку только
старым `check_time.preserved_inputs()`: N10 source manifest намеренно описывает
его прежнюю версию collision, а текущая версия расширена в N11.

## Независимые доказательства

- Свежие read-only Ghidra exports и исходные x86-инструкции проверяют
  дверные обработчики, Use projection, C2, floor/конвейеры, crop и отражения.
  EXE SHA проверяются до и после. Перехваченные сервисы, адресная система
  и шесть искусственных позиций внутри закрытой двери перечислены в evidence.
- Строгие host suites исполняют ядро и renderer независимо от SDK.
  Реальный N08 scheduler проверяет OPEN/CLOSE/AUTO, отказы заполненной очереди,
  signed occupancy, повторные Use и двойную задержку AUTO.
- Driver загружает исходные три RU-уровня и реальные RU/EN presentation bindings.
  При одной input/service timeline сравнивает состояние, карты, counters и
  события с 0/30/60 наблюдениями. Язык меняет представление, мир остаётся RU.
- Регрессия N10 повторяет его 107 unit и 9909 route/driver checks с новым
  collision API, сохраняя прежние выходы в `build/nds/n10/`.
- Для renderer сравниваются все 65192 исходных camera poses: фаза0 даёт
  прежние packet bytes/cell spans/counts. Исходные crop векторы проверяются
  отдельно от NDS-коррекции: полностью открытая панель скрывается без полосы.
- ARM9 исполняет те же unit/router/crop suites. Эмулятор дополнительно
  принимает физические X/B/Start/A через frontend, наблюдатель читает seqlock
  trace/snapshots. RAM, CPU, часы и результаты не инъецируются.

Linux x86_64-musl проверяется компиляцией и статической линковкой, без исполнения
в Windows. Исследовательские x86-прогоны не являются полным DOS/DPMI-сеансом.
Совпадение host и ARM9 само по себе не доказывает соответствие оригиналу.

## Команды

Из `C:/Source/DoK-port`, PowerShell:

```powershell
C:/Source/wasdify/.venv/Scripts/python.exe -B -X utf8 ports/nds/tools/check_interactions.py --pins-only
C:/Source/wasdify/.venv/Scripts/python.exe -B -X utf8 ports/nds/tools/check_interactions.py
C:/Source/wasdify/.venv/Scripts/python.exe -B -X utf8 ports/nds/tools/run_interactions_emulator.py --core C:/Users/dspev/AppData/Local/Programs/melonDS-DS/1.4.0/melondsds_libretro-win32-x86_64-Release/cores/melondsds_libretro.dll
```

Gate копирует точные N04 package и N07 trig/scene bytes в собственный N11 output,
выполняет host/native проверки и чистую ARM9 Rebuild. Общий Zig0.13.0,
BlocksDS1.24.0-1/GCC16.2.0 и 20 SDK packages закреплены; копии toolchain в проекте нет.
Полные команды, stdout/stderr, sources и artifacts записываются в
`research/evidence/nds/n11/host-check.json`. Runtime хранит сырые trace,
snapshots и кадры также при отказе последующего offline verifier.

Сохранённый trace можно независимо перепроверить без повторного запуска guest:

```powershell
C:/Source/wasdify/.venv/Scripts/python.exe -B -X utf8 ports/nds/tools/verify_interactions_trace.py research/evidence/nds/n11/emulator/result.json research/evidence/nds/n11/emulator/snapshot-worlds.bin
```

Для каждой из четырёх дверных сцен harness требует closed/start, промежуточный
crop между третью и двумя третями extent, полностью открытое и снова закрытое
состояние. Кадр привязан к конкретной RENDER-записи и последнему предшествующему
world snapshot; phase/extent сохраняются рядом. Терминальные кадры ждут
стабильной публикации. PNG проверяются визуально отдельно от native collision.

Final review выявил допустимое исходное состояние `phase > extent`: повторное
Use ресэмплит extent по текущему C2, не уменьшая phase. Native vector444 уже
содержит такой случай. Shared ARM9 renderer suite и интеграционный host wrapper
проверяют его отдельно; renderer сохраняет полностью открытую панель скрытой.
Это исправление области допустимых состояний, а не новая модель door machine.

Физические DS/DS Lite, flashcart/loader, реальные FPS, latency, IRQ и stack peak
остаются итоговой N24 по [политике проверки](validation-policy.md).
Диагностические keys/score/relocation/occupancy fixtures явно помечены и не
являются подбором предметов или игровой кампанией. Игровых сохранений нет.
