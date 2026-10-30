# N22: переполнение CPU-кэша в обычном EN LEVEL2

Статус: **причина воспроизведена и подтверждена host-проверкой; исправление и
полное прохождение этой сборки не приняты**. Это отдельная неисправность от
[фрагментации SDK VRAM](N22-sdk-fragmentation.md).

Образец — неизменяемый `candidate-partial01-en`: ROM
`476eda4c91b94822c0f8ab0314bf183798ef12ae0e432d208064a6a8728e0a53`, ELF
`5fddcabe2ae3f7378ddb8e494b818e9132cd6247fcbd14eabfc9f94ab1db6753`.
Адреса ниже — ARM9 runtime, поля извлечены из DWARF этого ELF.

Обычный прогон `route-2-level2-original-en-l2-v1` дошёл до 2089 клеток,
ключей 1+2, 30 убитых противников и двух настоящих смертей с retry.
Перед неисправностью оставались HP47, W102, одна жизнь. Использовались только
joypad/touch и наблюдение памяти; игровые данные, сохранения и телепортация
не подменялись. Полное прохождение отсутствует.

На video frame42654, при подготовке render frame19305, frontend остановился
с общим `error_bits=65536`. `effect_probe[31]=791` указывает на внешний
`sprites_prepare` в сохранённом `gameplay_probe/main.c`; он перезаписывает
внутренний номер строки. До `effects_prepare` и `stream_plan` выполнение
не дошло. Поэтому общий renderer status3 сам по себе не означает ошибку GPU.

Отдельный replay повторил все **7183 перехода кнопок**, все touch-переходы,
все **19304 завершённых строки состояния** и весь `fault_snapshot` без
различий. Неизменны FAT-файлы, ROM, ELF и libretro DLL. Observer сохранил
4MiB RAM до выгрузки эмулятора: frame42640 и frame42654. Никаких cache flush
или записей в ARM9 память он не внедрял.

В момент отказа:

| Данные | Байты |
|---|---:|
| Ёмкость CPU raw cache | 221056 |
| 13 уже запрошенных, закреплённых изображений | 196608 |
| Свободно | 24448 |
| Следующее изображение `0x82441404`, projectile glyph52 | 32768 |
| 14 разных изображений всех 25 видимых poses | 229376 |
| Дополнительный текущий W frame0, `0x82201400` | 8192 |
| Полный текущий набор с оружием | **237568** |
| Дефицит к текущей ёмкости | **16512** |

Первый 32KiB кадр того же projectile, `0x82441405`, уже закреплён.
Одновременно нужны два разных кадра двух существующих projectile instances.
Удалить закреплённое изображение нельзя: его использует текущая сцена.
Все 13 raw payloads (196608 байт) совпали по SHA-256 с хэшами неизменяемых
GPU asset entries. Проверены суммы, диапазоны, уникальность ключей, flags,
разрешение всех image IDs и соответствие адреса list опубликованному probe.

Адрес указателя `storage` — `0x0203FE2C`. От его значения: cache `+318504`,
list `+302100`, frames `+379840`, game `+352960`. Cache record — 24 байта,
records `cache+40`; список вмещает 512 poses. Фактические 25 poses не
приближаются к этому пределу.

Host-драйвер вызывает настоящие `dok_frame_store_require`,
`dok_texture_cache_begin` и `dok_frame_unpack` над сохранёнными metadata,
asset entries и packed frames. **331 проверка прошла**:

- Сохранённый all-pinned prefix возвращает `DOK_STATUS_NO_SPACE=2` на том
  же 32KiB изображении; cache, frame-store и payload при отказе не меняются.
- Последовательность 14 изображений из текущего list при ёмкости221056 и
  229375 останавливается на индексе13.
- При229376 все sprites помещаются; следующий W8192 не помещается.
  При237567 происходит тот же отказ на оружии.
- При237568 все15 запросов проходят. Все15 декодированных payloads
  совпадают с asset SHA-256, суммарно237568 байт.

8192 — также максимум одного кадра текущей W-анимации (остальные кадры
4096/8192). Значение237568 — доказанная потребность **этого** набора,
а не достаточный верхний предел для всей игры. Изменение общей памяти,
распределения asset slab либо стратегии CPU-кэша требует отдельного решения
и проверки других уровней. Снижение AI, удаление видимых объектов или
снятие pin-защиты здесь не применялось.

Полные доказательства:

- `research/evidence/nds/n22/cpu-cache-fault-01/host.json` и
  `captured-case.bin`, frozen sources;
- `research/evidence/nds/n22/candidate01-en-cache-layout.json`;
- `build/nds/n22/candidate-partial01-en/route-2-level2-original-en-l2-v1/`
  — исходный отчёт, команды, input, скриншоты, покадровый профиль;
- `build/nds/n22/candidate-partial01-en/replay-original-en-l2-fault-1/`
  — повтор, `ram-evidence.json`, before/fault RAM.

Воспроизведение без изменений ROM:

```powershell
py -3.14 -B -X utf8 ports/nds/tools/flow/capture_replay_fault.py --target build/nds/n22/candidate-partial01-en --source-report build/nds/n22/candidate-partial01-en/route-2-level2-original-en-l2-v1/report.json --run-id original-en-l2-fault-NEW --language 2 --before-frame 42640 --frames 42750
py -3.14 -B -X utf8 ports/nds/tools/flow/inspect_cache_layout.py --elf build/nds/n22/candidate-partial01-en/dok_nds_gameplay.elf --output research/evidence/nds/n22/candidate01-en-cache-layout.json
py -3.14 -B -X utf8 ports/nds/tools/flow/check_cache_fault.py --memory build/nds/n22/candidate-partial01-en/replay-original-en-l2-fault-1/fault-main-ram.bin --layout research/evidence/nds/n22/candidate01-en-cache-layout.json --ordinary build/nds/n22/candidate-partial01-en/route-2-level2-original-en-l2-v1/report.json --replay build/nds/n22/candidate-partial01-en/replay-original-en-l2-fault-1/report.json
```

Ограничение: observer читает model RAM без принудительной синхронизации
ARM9 D-cache. Проверки metadata, хэшей payload, родственных published probes
и точного replay подтверждают согласованность конкретного снимка; они не
являются общей гарантией аппаратной cache-coherency.

## Исправление: неиспользованный хвост events slab

N22 перераспределяет существующий managed slab, сохраняя все512 event owners
и по3 slots на владельца. `dok_event_bank` состоит только из массива owners:
72×512=36864 B. Прежние65536 B оставляли28672 B, которые N08 заполнял и
проверял как **неиспользованный** guard-tail. Теперь compile-time assertion
требует точного равенства `sizeof(dok_event_bank)==DOK_NDS_EVENTS_BYTES`.

| Регион | Раньше, B | После, B |
|---|---:|---:|
| Events |65536|36864|
| Assets |1048576|1077248|
| Общий managed slab |2293760|2293760|
| SDK minimum |196608|196608|
| Защищённый reserve |655360|655360|

Packed frames, decoder, eviction/pinning, число объектов и видимых спрайтов
не менялись. Для доказанного LEVEL2 EN raw-cache увеличивается221056→249728 B:
237568 B проверенного набора помещаются с12160 B остатка. Это по-прежнему
не доказательство максимального набора всей игры: обычные маршруты и N23
проверяются отдельно. Scratch/scene/assets начинаются на28672 B раньше;
audio/IO и конец slab остаются на прежних адресах.

[Отдельная регрессия](N22-memory-rebalance-regression.md) прошла: host memory,
time/events,655929 flow checks и160 occupied-retry checks; ARM9 проверил все
2293760 B новых семи регионов и настоящую Start-паузу/resume. Старые187
N03/N08 artifacts сохранены. У N08 diagnostic исправлен только zero-tail
цикл и показ актуального размера; production clock/event code не менялся.

Immutable targets `candidate-partial02-{ru,en}` собраны отдельно. Старые01
сохраняют воспроизводимый отказ. Полная N22-приёмка ещё открыта.
