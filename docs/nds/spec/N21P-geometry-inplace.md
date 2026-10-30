# N21P C11: один transient geometry cell для scene

Перед изменением C10 geometry/scene/door, headers и отчёты сохранены в
`research/evidence/nds/n21p/pre-inplace/` с pins. Новый код касается только
shared kernel в `render/geometry.c`, private `render/geometry_private.h`
и выбора caller в `scene.c`. Constructor math/order, validations, cache,
visibility, merge, fine clocks и door cropping не меняются.

## Public и private contracts

Прежний public builder по-прежнему отвергает `candidate==out`, сохраняет out
при ошибке и оставляет прежние полные candidate bytes на каждом error path.
Shared kernel проверяет те же world/level/cell/neighbors в прежнем порядке.
В public режиме он обнуляет1108-byte candidate ровно на прежнем этапе,
до constructor веток; wrapper копирует весь candidate в out только при OK.
Unused quads и reserved сохраняют прежние нули.

Private scene-only caller использует тот же kernel с одним cell.
На том же этапе обнуляются только count/reserved. `append` заполняет все
12 metadata bytes каждого active quad; vertical/horizontal constructors —
все80 vertex bytes. Compile-time assertions закрепляют vertex20 B/quad92 B
без padding. Поэтому каждый active quad полностью определён независимо
от прежнего содержимого памяти. Inactive tail не читается и не публикуется.
При constructor failure scene немедленно возвращается, не читая private
partial cell. Это не изменение public error contract.

Private путь выбран только при `GAMEPLAY_GEOMETRY_CACHE=0`. При cache1
сохранены прежние два cells и public cached вызов. Compiler rename
`-Ddok_geometry_build_cell=dok_geometry_build_cell_uncached` продолжает
работать; private symbol не участвует в этом rename. Постоянная память
и world state не добавлены, retained cache или mesh не создаётся.

## Проверки

```powershell
py -3.14 -B -X utf8 ports/nds/tools/map/check_geometry_inplace.py
```

Public C10/C11:528414 случаев, включая NULL/alias/bounds guards, все клетки
трёх уровней, три initial sentinels и binding material/width/height mutations.
Status, весь candidate и весь out совпали, включая165690 error returns.

Private:589824 случаев с восемью poison patterns. Active quad bytes,
count/reserved и status совпали с исходным public constructor;396689740
inactive tail bytes сохранили poison. Ошибочные partial cells не используются.

Отдельно собран и исполнен `_uncached` compiler path с cache1:
36864 cached calls совпали по full output, получено21596 cache hits.
Preprocessed cache1 scene проверен: он вызывает public cached entry,
а не private bypass.

Полный scene C10/C11 совпал на13816 кадрах. Door adapter со всеми фазами,
ошибками/compaction/merged refusal совпал на29342 scene instances.
Сравнено11306705472 нормализованных scene bytes, включая pointer offsets,
counts, vertices, merge provenance и failure side effects.
Использованы936 исходных fixtures и12880 фактически наблюдённых runtime
camera poses на initial LEVEL1–3 worlds; это не replay изменяемого live world.

`geometry-inplace-equivalence.json` фиксирует kernel/source assertions,
inputs, команды, bytes, errors и cache checks. Узкий
`geometry-inplace-source-supersession.json` связывает C10 archived geometry/scene
с новым public-equivalent kernel и private caller. Прежние reports не переписаны.
Exact C11 sources сохранены в `c11-inplace/`.

Host byte equivalence не подтверждает FPS-бюджет. Новый normal fine0 runtime,
звук, touch, SDK reserve и фактическое ускорение проверяет основной этап N21P;
физическая DS/DS Lite остаётся N24. Следующие оптимизации этим шагом не начаты.
