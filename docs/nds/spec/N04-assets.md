# N04: пакет ресурсов DOKNDS1

29 октября 2026 года. Формат и границы приняты до реализации; затем конвертер,
runtime, независимая сверка и три эмуляторных сценария прошли приёмку N04.
Статус — **готово (эмулятор)**; аппаратная проверка остаётся в N24.
[Паспорт N04](../tasks/N04.md), [бюджет N03](N03-architecture.md).

## Архитектурные решения

Один локально подготовленный файл `build/nds/assets/dok-assets.ndspak` содержит
RU/EN и исходные индексируемые блоки обоих RES. В ARM9 ROM игровой банк не
включается. Отдельный JSON происхождения содержит SHA архивов, оригинальный
индекс/имя/файловое смещение/размер/record bytes и SHA каждого блока/варианта.
Пакет, превью и сырьё не входят в Git/поставку без отдельного разрешения.

ID обозначает семантический вариант ресурса, а не адрес или место в кэше.
Совпадающие payload дедуплицируются по SHA-256 **и полным байтам**, но сохраняют
раздельные ID и происхождение. Различающиеся EN LEVEL и POPAL сохраняются;
игровой мир впоследствии выбирает RU независимо от языка интерфейса.

Каталог проверяется потоково, а не копируется целиком в RAM. Сессия хранит
reader и проверенные метаданные; mutable cache/pin/generation существуют
отдельно. Для directory validation допустим caller workspace не более
128 КиБ, включая таблицу уникальных диапазонов и I/O/hash scratch. Resident
подмножество — до 1 МиБ, audio staging — до 256 КиБ, один read_at — до 64 КиБ.
Все пределы относятся к фактическим диапазонам, а не ко всему файлу.

Resident чтение заполняет ещё не опубликованный candidate. Только после
проверки SHA и типовой схемы выдаётся lease. При отказе существующие pinned
данные, session/cache metadata и выходной descriptor не меняются; содержимое
unpublished candidate может измениться и потребителю не передаётся. Caller
задаёт слот/регион и policy замещения; автоматический LRU здесь не вводится.
Замена pinned backing и переполнение generation отвергаются. RAW_SOURCE
служит архивной проверке/происхождению, не готовым native runtime объектом.

Пакет неизменяем в течение жизни сессии. Изменение файла требует закрытия и
повторного открытия. SHA обеспечивает целостность, не аутентификацию автора.
Игровых сохранений и общего write API нет; запись настроек — отдельная N21.

## Числа и ID

Все числа на диске little-endian; чтение по байтам без cast/packing C struct.
Версия 1, flags/reserved равны нулю, codec — без сжатия. Файл и диапазоны
ограничены `uint32`, все сложения проверяются до использования.

```
id = (language << 30) | (source_index << 18) | (representation << 10) | part
language: 1 RU, 2 EN; source_index: 0..4095; representation: 1..255
part: 0..1023; для неиндексированного варианта part=0
```

`source_id` — RAW_SOURCE ID того же language/source_index. Для обычных
вариантов catalog subindex равен `0xFFFF`, хотя part в ID равен нулю;
для FRAME/GPU-frame/PCM_CHUNK subindex — номер с нуля. GPU standalone тоже
использует `0xFFFF`. Это не меняет ID: representations различны.
Записи сортируются строго по uint32 ID; дубликаты запрещены. RAW_SOURCE=1
предшествует производным своего источника. Максимум 2048 записей: текущий
полный профиль помещается; превышение требует новой проверки/версии, не усечения.

## Header, 160 байт

| Offset | Тип/длина | Значение |
|---:|---|---|
| 0 | 8 bytes | ASCII `DOKNDS1\0` |
| 8 | u16 | version=1 |
| 10 | u16 | header_bytes=160 |
| 12 | u16 | entry_bytes=128 |
| 14 | u16 | flags=0 |
| 16 | u32 | entry_count, 1..2048 |
| 20 | u32 | directory_offset=160 |
| 24 | u32 | data_offset=160+128*count |
| 28 | u32 | точный размер файла |
| 32 | 32 bytes | SHA-256 всех catalog bytes |
| 64 | 32 bytes | SHA-256 исходного PKR.RES |
| 96 | 32 bytes | SHA-256 исходного PKE.RES |
| 128 | 32 bytes | SHA-256 header bytes 0..127 |

## Catalog entry, 128 байт

| Offset | Тип/длина | Значение |
|---:|---|---|
| 0 | u32 | ID |
| 4 | u32 | source_id |
| 8 | u32 | абсолютный offset payload, aligned32 |
| 12 | u32 | payload bytes |
| 16 | u16 | representation |
| 18 | u8 | language |
| 19 | u8 | source_kind |
| 20 | u16 | original resource index |
| 22 | u16 | frame/chunk subindex или0xFFFF |
| 24/26 | u16/u16 | logical width/height |
| 28/30 | u16/u16 | storage width/height |
| 32/36 | u32/u32 | param0/param1 по representation |
| 40 | 32 bytes | SHA-256 payload |
| 72 | 32 bytes | SHA-256 исходного RES block |
| 104 | 16 bytes | имя CP866 до NUL, остаток zero |
| 120 | 8 bytes | reserved zero |

Source kinds: IMAGE=1, ANIMATION=2, FONT=3, PALETTE=4, LIGHT=5,
LEVEL=6, TEXT=7, PCM=8. Serialized pointers/cache garbage не используются как
runtime metadata. Исходные bytes остаются только в RAW_SOURCE/provenance.

Уникальные payload размещаются в порядке первого ID с выравниванием32,
padding zero. Alias допускается только при полном совпадении offset/length/SHA;
частичные перекрытия запрещены. Validation может хранить до2048 диапазонов
`offset,length,sha[32]` (81920B) в caller workspace; entry/hash/I/O scratch
вместе с этой таблицей должны укладываться в128КиБ. Сканировать каталог
для lookup допустимо; постоянный индекс и eviction tuning сейчас не нужны.

## Представления v1

| № | Представление | Payload и параметры |
|---:|---|---|
| 1 | RAW_SOURCE | Точные индексируемые bytes исходного блока; geometry/params0, subindexFFFF; source_id=self |
| 2 | IMAGE_INDEX8 | Row-major width*height indices без18-byte header; storage=logical; param0=stored_flags,param1=0 |
| 3 | ANIM_META | Header u16 count,u16 unknown,u32 zero; далее count*16: u32 FRAME_INDEX8 ID,u16 interval_bits,A,B,stored_flags,width,height; params0 |
| 4 | FRAME_INDEX8 | Аналог IMAGE; subindex=frame; param0=stored_flags,param1=8 (исходный load-time режим, не stored field) |
| 5 | GPU_INDEX8_POT | Все77 images+154frames/lang; logical исходные размеры, storage=minPOT>=8, zero padding; params как2/4 |
| 6 | STATIC_INDEX8 | Семь фонов320x200/lang →256x192; storage=logical; param0=PALETTE_RGB555 ID,param1=1 (pixel-centre nearest, аспект4:3) |
| 7 | PALETTE_RGB555 | 256 LEu16, R/G/B из VGA6 через v>>1, bit15=0; geometry0,params0; исходные6bit значения сохранены RAW |
| 8 | PCM_CLIP | Header24: u32 rate,sample_count,chunk_count; u16 C,D; u32 original_unknown28,original_unknown32; затем chunk_count*12: u32 CHUNK_ID,sample_offset,length; param0=rate,param1=sample_count |
| 9 | PCM_CHUNK | SignedPCM8: каждый original unsigned byte XOR0x80; <=65536B; subindex=chunk; param0=sample_offset,param1=rate |
| 10 | TEXT_UTF8 | Полный CP866→UTF8 без EOL/$/whitespace нормализации; geometry0,param0=65001,param1=0 |
| 11 | FONT_BITMAP | Точные256glyph*8/16bytes, MSBfirst8columns; logical/storage8×glyphheight; param0=256,param1=1 |
| 12 | LIGHT_TABLE | Точные16×256bytes; logical/storage256×16; params0 |
| 13 | LEVEL_MAPS | Header20: u16 width,length,depth=1,planes=3; u32 cell_side,cell_height,zero; затем MMap,TMap,FCMap width*length bytes каждая; geometrywidth/length,storage=logical,param0=side,param1=height |
| 14 | LEVEL_CP866 | Полный исходный LEVEL text, без нормализации; geometry0,param0=866,param1=0; N05 владеет grammar/objects/balance |
| 15 | STATIC_GPU_INDEX8 | Вариант6 с storage256×256 и zero padding, logical256×192; params как6 |
| 16 | TEXT_CP866 | Полный исходный текст9resources/lang, включая PANEL_DAT; geometry0,param0=866,param1=0 |

ANIM interval/A/B сохраняются как исходные16бит, включая0x86A0; неизвестные
поля не переименовываются в масштаб/панораму/громкость. PCM rate номинальный,
не объявляется частотой реального вывода DS. Все chunk records имеют собственный
SHA; длинная заставка474361samples обязана читаться частями, не вмещаться целиком
в256КиБ audio staging. Схема и ссылки descriptor проверяются до использования.

Шрифты/LIGHT/CP866/LEVEL canonical payload могут alias RAW bytes, но имеют
раздельные semantic ID и typed validation. Все исходные146records/lang
сохраняются. Производные варианты не заменяют исходный RES.

## Изображения и цвет

Семь фонов: BSPR1–3/ESPR1–3 и `ЗАСТАВКА_СПР`. Для целевого(x,y):
`sx=((2*x+1)*320)//(2*256)`, `sy=((2*y+1)*200)//(2*192)`.
Палитры: BSPRn/ESPRn→GAMEPn, заставка→`ЗАСТАВКА_ПАЛ`.
Текстовые ресурсы не впекаются в изображения; художественные надписи внутри
оригинальной картинки сохраняются как часть рисунка.

GPU представление — индексная8bit texture с POT-границами и явным content rect.
Оно не обещает окончательный VRAM/material выбор renderer. LIGHT применяется
позже; index0 не объявляется прозрачным до LIGHT. Padding не должен попадать
в content UV. Прозрачность, variants LIGHT, mirrored/cropped sampling и
проекция уточняются N06/N07/N13, исходные indices/flags для этого сохранены.

## Проверки и происхождение

Независимый oracle не импортирует новый converter. Он заново разбирает RES,
сверяет все292 indexed source blocks, 77image+154frames/lang, все поля анимаций,
полные PCM/text/font/palette/LIGHT/maps и каждый derivative byte. Два преобразования
с одинаковыми входами/инструментами дают одинаковый пакет. SHA оригиналов
проверяются до/после. Source names не используются как filesystem paths.

Runtime gates: реальные host/ARM9 reads с bounded requests и cache leases;
missing/version/truncatedcatalog/shortread/hash/schema/bounds/overflow отказы;
повреждение с пересчитанным SHA тоже должно отклоняться типовой проверкой.
Проверка не только tiny fixtures: длинный actual title PCM и largestGPU131072B.
Визуально проверить исходные фоны и aspect-correct целевые варианты плюс
контрольную сетку/окружность. Аппаратная приёмка — N24 по
[validation-policy](validation-policy.md).

## Принятый результат

Пакет содержит 1466 записей всех 16 представлений, 292 исходных блока,
738 уникальных payload и 728 aliases. Размер 9 230 016 байт, SHA-256
`2f03ba02c9986b4e0bd321e94ca00df8a355f7dbb1dff5598a950d3366061511`.
Независимый oracle сверил все производные байты и provenance с обоими RES;
два преобразования побайтово совпали. Семь пар фонов и контрольная окружность
просмотрены; соответствующие EN-пары побайтово совпадают с RU.

Host выполнил 4759 проверок, ARM9 в эмуляторе — все 16 типов обоих языков,
длинный PCM (474361 sample, 8 chunks), GPU payload 131072 байта и отрицательные
сценарии. Отдельные загрузки без носителя и без пакета завершились понятными
диагностическими экранами. Все 49152 байта опубликованного изображения
независимо прочитаны из ARM9 RAM и совпали с записью пакета.

Рабочий workspace — 114688 байт в I/O-области 128 КиБ. Probe резервирует
1 МиБ asset pool и 256 КиБ audio staging. `asset_highwater=262144` обозначает
фиксированное разделение candidate/backing по 131072 байта, а не измеренный
пик универсального allocator или всей игры. Чтение ограничено 65536 байтами;
каталог и полный пакет в RAM не размещаются.

Команды, API и ограничения — [runtime](N04-runtime.md), алгоритмы и исходные
SHA — [конвертер](N04-converter.md), полная приёмка — [паспорт](../tasks/N04.md).
Компактный итог — [JSON](../../../analysis/nds-n04-assets.json), первичные
доказательства — `research/evidence/nds/n04/`. Подготовленные данные не означают
готовый renderer, воспроизведение PCM, игровой мир или измеренные DS FPS.
