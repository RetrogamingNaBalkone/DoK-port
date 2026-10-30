# N22: actual SDK fixture для фрагментации VRAM

Проверено 30 октября 2026 года. В обычном маршруте LEVEL2 V14 обнаружен
полный повторный upload 367 360 байт: после DMA карты текстуры копировались
с линии216 до линии133 следующего кадра и нарушали аппаратное окно.
Этот документ описывает отдельный воспроизводимый эксперимент с настоящим
BlocksDS allocator и uploader. Он не заменяет обычное прохождение.

## Вход и публичное построение layout

Источник — `research/evidence/nds/n22/root-v14-layout.json`, последний
успешный кадр11351. Адреса — физические LCD VRAM pointers Nintendo DS,
не DOS/NE/Ghidra-адреса. Корректность исходного read-only декодера SDK
описана в `research/evidence/nds/n22/root-v14-sdk-pointer.md`.

Fixture сначала заполняет все384 КиБ:16 исходных resident spans и пять
dummy fillers занимают четыре отверстия, причём20-КиБ отверстие разбито
на16+4 КиБ. Каждый новый descriptor добавляется отдельным singleton-prefix
вызовом `dok_nds_stream_plan`, затем обычным commit в loading mode.
Так порядок существующих размещений одинаков при обоих compile flags.
Публичные `glGetTexturePointer` и `glGetInt` подтверждают каждый точный
physical pointer и размер. Приватные allocator/stream metadata не записываются.

Следующий plan оставляет16 residents и удаляет fillers. Все16 адресов
совпадают с V14, свободны только:

| Начало | Размер |
|---|---:|
| `06827000` | 20 480 |
| `0683C000` | 16 384 |
| `06868000` | 16 384 |
| `0687C000` | 16 384 |

Финальный набор убирает16-КиБ key5398598658 из`0681F000` и добавляет
32 768,16 384,2048 и256 байт. Между новым свободным интервалом и первой
дырой остаётся используемый16-КиБ hit key5398598656 в`06823000`.
Одна сортировка новых misses не создаёт нужный32-КиБ span.

Идентификаторы, начальные POT размеры, порядок demand и физические интервалы
взяты из сохранённого V14 layout. Texel payload намеренно синтетический:
уникальные воспроизводимые байтовые patterns позволяют независимо проверить
каждый resident. Для четырёх новых payload используются допустимые POT
размеры того же объёма; ориентация нового32-КиБ прямоугольника не принимается
за доказательство исходной игровой картинки.

## Парная проверка

Оба standalone targets используют одну текущую копию `render_backend.c`
и один SDK. Различается только `N22_STREAM_PARTIAL_DEFRAG=0/1`;
`N22_STREAM_LARGEST_MISSES=0`, partial включает descending misses своим
проверенным compile path. Backend и planner побайтово совпадают с frozen
`candidate-partial01-ru`; production main и его Makefile не изменялись.

```powershell
py -3.14 -B -X utf8 ports/nds/tools/flow/build_fragment_arm9.py baseline
py -3.14 -B -X utf8 ports/nds/tools/flow/build_fragment_arm9.py partial
py -3.14 -B -X utf8 ports/nds/tools/flow/run_fragment_arm9.py baseline
py -3.14 -B -X utf8 ports/nds/tools/flow/run_fragment_arm9.py partial
py -3.14 -B -X utf8 ports/nds/tools/flow/summarize_fragment_arm9.py
```

Сборка — общий BlocksDS1.24.0/GCC16.2.0/Binutils2.47 с проверкой всего
`toolchain_versions.json`; Win32 signal pipes MSYS2 требуют запуска build
вне sandbox. ARM7 — закреплённый N18 ELF. Новые toolchain-копии не создаются.

| Измерение | Baseline | Partial |
|---|---:|---:|
| Actual ARM9 checks | 262 | 264 |
| Uploads / hits | 19 / 0 | 5 / 14 |
| Texture+palette upload bytes | 367 360 | 67 840 |
| Из них palette / BG mirror | 8704 | 0 |
| Map DMA, scanline | 192→216 | 192→216 |
| GPU commit, scanline | 216→133 | 216→250 |
| Совместный DMA epoch, bus ticks | 435 294 | 123 829 |
| Порог119×2130=253 470 ticks | Нарушен | Пройден |
| Uploader result | HARDWARE=6 | OK=0 |

Partial переместил только key5398598656,16 384 байта,
`06823000→06827000`. Всего новых texels51 456 + перенесённых16 384 =67 840.
Полной пересборки texture set и повторной загрузки палитры не было.
Числа — измерения модели DS в эмуляторе, не аппаратный FPS.

## Дополнительные доказательства

- CPU plan не изменяет texel payload: все393 216 texture VRAM bytes
  сравниваются до и после. Palette bytes также неизменны.
- Проверены actual primary-bank и palette-bank mappings до/после plan
  и commit. После временного диагностического LCD readback mappings
  восстанавливаются; этот readback не входит в DMA budget.
- В partial новая геометрия19 текстур поставлена в GE до SwapBuffers;
  после границы предъявления сначала выполняется настоящий49-КиБ DMA
  карты, затем prepared texture commit. Окно119 scanlines не расширено.
- Нормальный frame в линии100 с pending palette update получает
  `DOK_NDS_RENDER_HARDWARE`; отказ не меняет texture bytes. Следующий
  допустимый VBlank commit завершает эту отдельную negative-проверку.
- После commit независимый Python oracle проверяет все358 656 bytes
  девятнадцати residents и8192 bytes палитры. Публичные SDK pointers,
  размеры и key/payload correspondence записаны для21/16/19 allocations
  на трёх стадиях. Поиск по payload не выдаётся за чтение приватной таблицы.
- Старый CPU-буфер hit5398598656 после исходного commit заполнен poison
  `E7`, а current descriptor того же key получает новый буфер с правильным
  pattern. После relocation VRAM содержит current pattern, не poison.
  Этим проверяется использование текущего descriptor при перемещении hit.
- Для partial нарисована сетка19 quads через настоящий backend. Проверены
  19 точек фактического LCD изображения; максимальная разница с RGB555
  эталоном4/255 соответствует проверяемой погрешности преобразования цвета.
  Некоторые эталонные цвета чёрные, поэтому точечная pixel-проверка дополняет,
  а не заменяет полный resident byte oracle. Контрольный PNG просмотрен.
- Оба FAT audit завершились без изменения логических файлов. Observer
  читает согласованный seqlock probe и RAM snapshots, не пишет RAM/VRAM/PC.

## Артефакты и ограничения

Финальные targets: `build/nds/n22/fragment-{baseline,partial}-01/`.
Не пересобирать их после приёмки; следующий опыт получает другое имя.

| ROM | SHA-256 |
|---|---|
| Baseline | `97c842df480b4266dcc69a42dec1687fc5abd59649bf34a3302012754b834dcf` |
| Partial | `bfa36b2f2baf147b91b80380a7c49d2b940daa5fb2a63c09f942945a44f54346` |

Постоянные материалы:
`research/evidence/nds/n22/fragment-{baseline,partial}-01/`:
build manifests, snapshots исходников/загруженных observer-модулей,
`emulator.json`, SDK placement records, полный VRAM/palette readback
и контрольные кадры. ELF/map/core SHA и все options находятся в manifests.
Стенд — melonDS DS libretro1.4.0, DS mode, встроенные BIOS/firmware
replacements, software renderer, JIT и threaded renderer выключены.

Компактный результат и integrity pins:
[analysis/nds-n22-sdk-fragmentation.json](../../../analysis/nds-n22-sdk-fragmentation.json).
Fixture не доказывает обычное прохождение LEVEL2/LEVEL3, правильность всех
игровых эффектов, полную UI map rasterization или работу физической DS.
Её вывод — ограниченный частичный перенос решает конкретный подтверждённый
SDK fragmentation case с сохранением исходного DMA deadline и payload.
