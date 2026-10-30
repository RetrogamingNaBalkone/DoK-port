# N20: исходное раскрытие карты

Адреса ниже — Ghidra NE loader. Это не файловые смещения, номера
NE-сегментов или runtime-селекторы DPMI. Анализ не использует код GBA.

## Источники и воспроизведение

Read-only `tools/ghidra/ExportNdsMap.java` экспортирует выбранные функции,
ASM и сегменты из сохранённых `research/ghidra/projects/{ru,en}/Kremlin.gpr`.
Драйвер `ports/nds/tools/export_map_evidence.ps1` сверяет SHA EXE до и после:

| Образец | SHA-256 |
|---|---|
| PKR.EXE | `12f78afc0eba05f76b5dc0bb99da38d7daa20585024c0ba958fe828aa3864d28` |
| PKE.EXE | `cce7bc5993d0734ad87e93db37a70b8b52faedfff14ac48d6e317e5ac996a37b` |

Оба SHA сохранились. Экспорты, журналы и manifests:
`research/evidence/nds/n20/original/{ru,en}/`. Автоматический C — материал
исследования; он не собирается как движок.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File ports/nds/tools/export_map_evidence.ps1
# Полный повтор: host Windows, static Linux cross-link, native/C gate, summary.
py -3.14 -B -X utf8 ports/nds/tools/map/check_discovery.py
# Только native/C gate на уже собранном host executable:
py -3.14 -B -X utf8 ports/nds/tools/map/audit_discovery.py --dump build/nds/n20/discovery/map_discovery_dump.exe
# Проверка сохранённых pins и обновление компактной сводки:
py -3.14 -B -X utf8 ports/nds/tools/map/check_discovery.py --summarize-only
```

Host dump собирается общим Zig0.13.0, strict C11, `-O2 -Wall -Wextra -Werror
-pedantic`, include `ports/nds/include` и `build/nds/n07/generated`:
`ports/nds/tests/map_discovery_dump.c`, `ui/map_discovery.c`,
`render/{sprites,scene,geometry}.c`, `core/{world,sha256}.c`.
Общий `render_trig.inc` содержит уже проверенные722 математических значения;
игровые изображения и геометрия в executable не встраиваются.

## Producer → visibility → visited

| Стадия | PKR | PKE |
|---|---|---|
| World builder | `1098:30C1` | `1030:3571` |
| Sort descending key | `1100:0000` | `1060:0F30` |
| Column visibility | `1100:0301` | `1060:128F` |
| Record flags → visited | `1098:53E9…542A` | `1030:60E3…6123` |

Native record хранит far vtable/указатели; `+4` — WORD индекса клетки,
`+6` — BYTE flags, `+7` — signed DWORD sort key. Native virtual slot+4
возвращает включительные горизонтальные границы как signed WORD. Проверены
`LES`, `RETF`, caller cleanup, mixed WORD/DWORD арифметика и low32 IMUL/SAR/IDIV.
Far pointers не перенесены в NDS-структуры.

Сначала constructor проверяет глубину и границы в исходных320 колонках.
Далее Shell sort с gap=count/2, gap/=2 располагает records по убыванию key.
Visibility проходит список с конца, от ближних к дальним. Все320 колонок
изначально открыты. Record, пересекающая хотя бы одну открытую колонку,
получает bit0. Если исходный byte имеет bit1 (`flags&2`), она дополнительно
закрывает каждую открытую колонку своего включительного диапазона.
Обычный billboard/floor проверяет наличие колонки, но не закрывает её.
Key обнуляется **у каждой** record во время этого прохода, даже у видимой.
Наконец world builder записывает visited[record.cell]=1 только при bit0.
Это coarse column occlusion, а не raycast по сетке, веер или GPU visibility.

Построение records сохраняет исходный порядок внутри клетки: ceiling bands,
стены либо парный floor/ceiling, центральные плоскости, TMap billboard,
MMap billboard. Парный floor/ceiling — **одна** DOS record, а не две.
Builder передаёт горизонтальному constructor `FCMap.flags|8`, что отдельно
от поля object_flags в N07 геометрии. Строгий gap sort сохраняется вместе
с этим порядком: произвольная перестановка равных key недопустима.

Disclosure использует исходный AABB, включая overwritten-p1 максимум:
min берётся по p0/p1/p2/p3, max — по p0/p2/p3, затем clamps. N07 renderer
сознательно использует более широкий исправленный AABB; карта его не наследует.
Все1444 сохранённых native AABB-вектора проверяются отдельно. Ни DS guard
band, ни масштаб/прокрутка нижней карты не влияют на раскрытие.

Vertical constructors сохраняют near clipping двух endpoints; center key
считает average depth−SAR16(width,2), edge — average depth. Horizontal
отрицательная глубина использует SAR20 исходного low32 rotation numerator,
не обычный near clip. Billboard использует принятый N12 source projector.
Проверяется world-space geometry до GPU/door crop, с текущими runtime flags.
Исходные door/gate flags64/8 не имеют occluder bit1: такая дверь сама
не закрывает колонки даже в закрытом состоянии. Заменять это остановкой
луча на двери было бы изменением DOS.

## Отдельные исходные эффекты и исправление NDS

DOS automap во втором проходе при MMap class1 пишет visited=1
(`1098:15EE` / `1030:16D2`) до visited gate. Первый FCMap pass уже выполнен.
Это отдельная мутация функции карты, а не результат render records.
Pure raster воспроизводит порядок чтения локально; runtime после отрисовки
сохраняет эти visited-записи для следующего кадра. Постоянный экран NDS
вызывает карту чаще modal DOS — явная адаптация частоты, с тем же порядком.

Найдено исходное ошибочное раскрытие от глобального projectile pass.
Spawn выставляет `flags|1000h` (`PKR1068:2681`). После клеточного обхода
`PKR1098:51F2…53BA` строит эти billboard отдельно. Код `52B7…531A`
берёт младшие WORD actor X/Z и **сразу clamps их в0…63, без деления
на CellSide480**. Обычные положительные world coordinates поэтому дают
tag4095. Visible record затем раскрывает нижний правый угол карты.
Это не корректный индекс клетки projectile.

NDS сознательно исправляет эту утечку: disclosure принимает только N12
source IDs `80000000h | plane<<24 | cell`. Pool/synthetic IDs (`90000000h`
и другие) не раскрывают клетки. Функция не трактует pool index как cell index.
Native source vector и отрицательные NDS update-векторы зафиксированы отдельно;
это исправление не выдаётся за побитную эквивалентность исходного global pass.

## Реализация и границы доказательства

`ports/nds/ui/map_discovery.c`, `include/dok/map_discovery.h` не хранят копию
мира или меша. Временный workspace —35268 B:2496 compact records, индексный
порядок,320 колонок и счётчик. Он помещается в общий scratch131072 B и не
удерживает borrow между вызовами. Опубликованные scene packets не меняются.
Visited меняется только после успешного построения/валидации всех records.

`audit_discovery.py` исполняет оригинальные PKR/PKE constructors, sort и
column code через Unicorn2.1.4. Подменены только compiler array initialization
и dispatch границ для контролируемых synthetic records. Отдельный combined
тест выполняет fresh original per-cell builder → native constructors →
native sort/columns и сверяет **actual C update целиком**, включая insertion
order, flags, key, bounds, cell index и visited. Он проверяет54 камеры трёх
RU-уровней, в том числе heading112 с исходной AABB-особенностью.

Скалярные geometry constructor vectors покрывают все исходные static records
трёх RU-уровней, четыре положения камеры на record, в обоих EXE.
Для sprites есть отдельные constructor/provenance vectors и ранее принятый
N12 projector. Combined54 не объявляется полной живой DOS-сессией с AI,
анимацией и снарядами. Аппаратные touch/тайминги не проверяются этим oracle.
Точные итоги и source pins — `research/evidence/nds/n20/source-discovery.json`;
компактная сводка — `analysis/nds-n20-discovery-source.json`.

Итоговый полный gate прошёл, с одинаковыми source/executable hashes до и после:

| Проверка | Результат |
|---|---:|
| Native PKR/PKE geometry constructor vectors |121776 PASS|
| Native billboard constructor vectors |280 PASS|
| Native AABB |1444 PASS|
| Synthetic column cases / records |88 /26042 PASS|
| Combined actual update cases / records |54 /6078 PASS|
| Invalid sprite provenance, out unchanged |5 PASS|
| Дополнительный projectile не меняет actual update |54 PASS|

Windows host executable SHA-256:
`8e0f1e585f6b44ef790b2947d8a90bf834cb2b7adb65bbd75c094f7dbdbe0123`.
Отдельный strict Linux x86-64 musl static cross-link завершился с exit0,
SHA `59cb20486a0fd53284355b90522355338bd419c367ac24361c2ad59b165e6850`;
Linux executable не запускался. Проверки исполнены CPython3.14.6,
Unicorn2.1.4, Zig0.13.0. Математические `render_trig.inc` N07 и N09 совпали
по SHA `409f2a9340587f9affc1589d982371fa60daa5817df8bd24a142f255f03438e1`.
