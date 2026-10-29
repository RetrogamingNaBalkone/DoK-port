# N08: план независимой проверки времени

Статус: проверки host, оригинальных инструкций и ARM9 в эмуляторе прошли
29 октября 2026 года. Приёмку задачи фиксирует [паспорт N08](../tasks/N08.md).
Изменяется только линия NDS.
Основания: [паспорт N08](../tasks/N08.md), [контракты N03](N03-contracts.md),
[исходная cadence](../../originals/original-cadence.md) и сохранённые Ghidra ASM
PKR/PKE. Код и тесты GBA не являются эталоном новой реализации.

## Разделение доказательств

1. Неизменность зависимостей: SHA-256 принятых данных, source manifests и артефактов
   N01–N07 проверяются до и после N08. Они не пересобираются этой задачей.
2. Исходная семантика: независимое выполнение адресных x86-фрагментов RU/EN
   проверяет безопасные векторы schedule/dispatch. Отдельно указываются stubbed
   platform services, calling convention и границы изолированного исполнения.
3. Новый strict C11 core: host-тесты проверяют восстановленные безопасные случаи
   и явно принятые исправления опасных случаев. Согласие двух новых моделей
   само по себе не является доказательством эквивалентности DOS.
4. ARM9: тот же strict C core собирается SDK, а ROM исполняет наблюдаемые
   сценарии и платформенный источник времени. Внешний наблюдатель читает RAM,
   не меняет её, CPU state, часы или результаты тестов.
5. Реальная DS/DS Lite и точность физического кварца отложены до N24 согласно
   [политике проверки](validation-policy.md).

## Матрица проверок

| Область | Обязательные наблюдения | Тип доказательства |
|---|---|---|
| Часы оригинала | Два отдельных unsigned DIV на 119; младшее произведение IRQ×period; phase16; reset count>10000 | ASM и независимые RU/EN векторы |
| Ёмкость | capacity-1, граничный отказ без изменения очереди | Native safe векторы и C |
| Owners | Порядок кольца, одинаковые deadlines, удаление опустевшего owner и уменьшенный bound | Native safe векторы и C |
| Повтор | deadline=now_after_callback+interval; отсутствие catchup; отрицательные интервалы | Native safe векторы и C |
| Мутации | Append в текущий owner, cancel keep, source/read против compacted/write, set_interval(slot0) | Native safe векторы и C |
| Опасные мутации | Destroy текущего/следующего owner, clear и повторный вход, активация пустого owner | Отдельный контракт безопасных NDS-исправлений и C; native UAF не исполняется |
| Wrap | uint32 исходные deadlines по обе стороны wrap; unsigned сравнение; независимость uint64 platform time | Native safe векторы, C и ARM9 |
| Рациональный перевод | Точные границы частоты, остаток без накопительного дрейфа, большие counts, произведение свыше 64 бит, uint32 ticks wrap, отказ при регрессии platform counter | Независимая целочисленная модель и C/ARM9 |
| Пауза | Мир стоит, меню-время исключено, resume без burst, повторные pause/resume | C/ARM9 и фактическая кнопка через frontend |
| Вывод | Одни dispatch samples при 15/30/60 графических наблюдениях дают одинаковые события; render reads не вызывают callback | C/ARM9 сценарии |
| DS источник | Неубывающий uint64, фактическая частота descriptor, counter carry, согласование с VBlank | SDK источник/ASM и измерение ROM на эмуляторе |

Одинаковый результат при разных FPS относится к одинаковой последовательности
временных выборок dispatch. Оригинальный polling действительно допускает
запаздывание при редком dispatch; произвольное прореживание dispatch нельзя
выдавать за эквивалентную временную последовательность.

## Первичная проверка зависимостей

29 октября 2026 года read-only обход компактных итогов N01–N07 и их рекурсивных
source manifests проверил 240 уникальных файлов с SHA-256/size, несовпадений 0.
Артефакты не пересобирались. Итоговая машина проверки сохранит список pins и
воспроизводимую команду; эта предварительная проверка не закрывает N08.

## Реализация проверки

[check_time.py](../../../ports/nds/tools/check_time.py) выполняет единый gate:
проверка 246 уникальных pins предыдущих задач до/после; изолированный x86 oracle;
SDK source/object audit; строгая native- и Linux x86_64-musl-сборка трёх программ;
standalone header compile; сравнение численных и оригинальных векторов;
SDK Rebuild и strict ARM9 compilation всех тестовых единиц. Linux binaries
собраны и слинкованы, но в Windows не исполнялись. Итог не объявляет runtime
готовым без отдельного эмуляторного прогона.

Общие host/ARM9 cases:17974 checks, 3 clock cases, 7 event groups, failures 0.
Typed bank содержит 512 owners и занимает 36864 байта внутри events slab 65536.
Вне игрового кода [time_vectors.c](../../../ports/nds/tests/time_vectors.c)
передаёт результаты C-clock независимой модели Python arbitrary-precision
`divmod`:48 рациональных частот,720 шагов,5760 scalar checks. Проверены
`33513982/64`, `1193182/119`, крайние частоты `1/UINT32_MAX` и
`UINT32_MAX/1`,41 seeded random frequency, большие delta доUINT64_MAX,
wrap native ticks, paused reads, idempotent pause/resume и регрессия с
сохранением output canary/state. Ошибки переполнения произведения 64 бит
не маскируются: проверяется точный результат и остаток для fallback path.

[events_vectors.c](../../../ports/nds/tests/events_vectors.c) получает
контролируемые операции и callback scripts из оригинального oracle, вызывает
фактический C API и печатает все прочитанные значения injected clock,
callbacks и итоговое активное состояние. Все 16 RU и 16 EN cases прошли:
242 clock-read values и 48 callbacks, включая nested schedule из callback.
Сравниваются deadlines, intervals, parameters, порядок и число активных owners.
Память уничтоженного native owner не сравнивается после завершения lifetime;
устаревший cursor пустого кольца нормализуется до 0. Эти исключения не скрывают
состояние живых owners. Native dispatch с ненулевым busy-wait duration не
входит в NDS API; проверяется исходный dispatch(0).

## ARM9 и внешнее наблюдение

[ABI](../../../ports/nds/platform/time_probe/time_probe.h) содержит probe 228 B
и 512 trace slots по 116 B. Перед payload writer публикует/flush odd sequence;
после payload flush публикует/flush even. Observer читает
sequence/payload/sequence, проверяет magic/version/size/ordinal и не пишет
эмулируемые RAM, часы, CPU state, pass flags или savestate.

[run_time_emulator.py](../../../ports/nds/tools/run_time_emulator.py) использует
неизменённый frontend N02 только как transport. Core melonDS DS 1.4.0,
SHA-256 `c7e6b58c667075a2a9868f2c3ba7a98e24200410a6da3051a7c555f1f0eef8ef`;
обычный DS mode, 4194304 B exposed RAM, built-in BIOS, software renderer,
RTC 2004-01-01. ROM не использует FAT, игровые данные или Nintendo BIOS dumps.
Полный набор core options хранится в runtime report. До/после запуска
проверяются все source fingerprints и ROM/ELF/map matching host gate.

Synthetic scenarios 1/2/3 используют native samples 0…8000 с шагом 5,
owners 1/2/3 вставлены в таком порядке с intervals 416/1040/1664.
Внутри sample сначала dispatch, затем необязательное render-clock read.
Render observation выполняется при смене
`floor(now*fps*119/1193182)`, включая sample 0. Независимая Python-модель
проверила полные callbacks, source deadlines и native times:
по 30 callbacks в каждой последовательности; render reads 12/24/48 при
15/30/60. Это тесты наблюдения, а не фактический графический FPS игры.

Runtime завершён за 398 frontend frames. Собраны все 556 trace records,
включая перезапись 512-slot ring без потери наблюдений. Live scheduler:
62 callbacks, 30 до паузы и 32 после; paused callbacks 0. Для 378 переходов
проверены точные native ticks и fractional remainder по рациональному
коэффициенту `38181824/1994081929`. Source counter не убывал; его большой
интервал 314 VBlank дал 2748368 counts, согласованный с DS-model
`560190/64 counts per VBlank` с допуском 2% на положение публикации внутри кадра.
Это проверка divider/configuration в модели, не точности физического кварца.

Start приходит через настоящий libretro joypad callback. Harness нажимает
его после 90 LIVE observations, держит в паузе 120 frontend frames, отпускает
на 5 кадров и нажимает заново. Мир оставался на 15343 native ticks в течение
126 VBlank; fraction и pending deadline сохранялись. Первый callback после
resume использовал прежний deadline 15425 и исполнился при tick 15511;
burst/catchup и callbacks в паузе отсутствовали. Обе кнопочные границы и все
clock snapshots сохранены, сравнение не основано на probe pass flag.

RAM closure: 118784 static/startup + 2293760 managed slabs + 602112 SDK headroom
+ 655360 reserve = 3670016. Events bank 36864 B, tail canary до 65536 B сохранён.
SDK package lock 20 packages проверен. `.su` показывает per-function maximum
1648 B для suite, 160 B main, 152 B callback, 112 B dispatch при stack 12288 B;
это не полное доказательство суммарного SDK/IRQ stack usage.

## Команды, артефакты и ограничения

Из `C:/Source/DoK-port`, PowerShell:

```powershell
C:/Source/wasdify/.venv/Scripts/python.exe -B -X utf8 ports/nds/tools/check_time.py
C:/Source/wasdify/.venv/Scripts/python.exe -B -X utf8 ports/nds/tools/run_time_emulator.py --core C:/Users/dspev/AppData/Local/Programs/melonDS-DS/1.4.0/melondsds_libretro-win32-x86_64-Release/cores/melondsds_libretro.dll
```

CPython 3.14.6, Zig 0.13.0, BlocksDS 1.24.0-1, GCC 16.2.0. Host, SDK и runtime
terminal exit 0. Known SDK `crtn.o` warning об отсутствующей `.note.GNU-stack`
не является C warning нового кода; все единицы собраны с `-Werror`.

| Артефакт | SHA-256 |
|---|---|
| `build/nds/n08/dok_nds_time.nds` 109056 B | `41eba541a417f99bb39d48f0c241199b6b596a1feca4b8040034547e4e7adcf8` |
| `research/evidence/nds/n08/host-check.json` | `2f7bd554b268e042c9b7716a7a31cae8b5f055295aa40d871deebf2eda6d7b34` |
| `research/evidence/nds/n08/source-manifest.json` | `2ea803dd9b8607b9e9c2a7e7ee9e847d87534608b7be9c32892dfde1e2b33a08` |
| `research/evidence/nds/n08/build-manifest.json` | `abe68e9d2af0fb197e7ac0cc2ee20164e31289a3646864c9e51d33af9fd8d5bb` |
| `research/evidence/nds/n08/emulator/result.json` | `fe1ee346d15a10f9d562746ed8b3e4bceec1b2f76f14fe0692823cf5bfa2fe3a` |
| `research/evidence/nds/n08/emulator/archive-manifest.json` | `880047f2f2dade244c07d98fa2b6fd45c932ed145a0d8d871b3aca6126fe8a98` |

Архив содержит 13 файлов: точные runtime report/log, 6 PNG, host/source/build
manifests, SDK build report и pre-runtime manifest. Host/source/build manifests
скопированы до запуска эмулятора; их SHA после выполнения не изменились.
ROM/ELF/map не дублируются, текущая сборка сохранена. Полные trace/observations
остаются в ignored research, компактный итог — в `analysis/`.

Оба итоговых screenshot просмотрены: PAUSED показывает state 3/paused 1,
COMPLETE — state 5, failures 0 и paused callbacks 0. Paused PNG снимается после
трёх устойчивых PAUSED frontend frames; главный источник доказательства —
trace boundary и 126 VBlank неизменного world time.

Первый успешный этап сохранён отдельно в
`research/evidence/nds/n08/experiments/initial-gate/`. В нём 17964 checks и
более поздняя перерисовка текста PAUSED. После независимого ASM-review
добавлен regression mid-dispatch rollover, а UI стал рисовать переход сразу.
Окончательный gate и matching runtime повторены для этих изменений.

Важная адаптация: даже native dispatch(0) может начать дополнительные sweeps,
если часы завернулись внутри callback до исходного end. Новый dispatch API
выполняет ровно один sweep; regression проверяет удаление последнего события
и bounded return в этом случае. Это намеренное устранение busy-loop, отдельно
от сохранённого unsigned deadline compare и native wrap quirks.

Исходные DOS PIT jitter, CLI/STI и rebase race не воспроизводятся платформенным
DS-clock: принята монотонная рациональная NDS-адаптация, описанная в
[N08-time](N08-time.md). Физическая DS/DS Lite и итоговый игровой цикл остаются
вне этой проверки.
