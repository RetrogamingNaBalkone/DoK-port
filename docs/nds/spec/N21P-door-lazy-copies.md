# N21P C10: копирование только изменяемых door quads

Меняется только `render/door_render.c`. C9 source сохранён в
`research/evidence/nds/n21p/pre-door-lazy/` с pins.
Pure crop constructor, его rounding/phase/extents, cache lookup,
`source_door_class` и проверка изменённой merged door побайтно неизменны.
`scene.c` geometry/visibility/merge здесь не меняются.

## Стабильная compaction

Раньше inner loop копировал каждый92-byte NDS quad в local `dest`, затем
обратно в тот же либо более ранний slot — даже при неподвижных закрытых дверях.
Теперь source остаётся const pointer. Пока запись не сдвинулась и её вершины
не изменились, scene bytes не записываются. При compaction копируется только
действительно перемещаемая запись. Cropped vertices пишутся после успешной
прежней validation; in-place crop сохраняет остальные поля без полного copy.

Destination index равен числу оставленных предыдущих записей и никогда
не превышает source index. Разные quad slots не перекрываются; одинаковый
адрес не нуждается в copy. Предыдущая запись поэтому не повреждает будущий
source. Cell compaction и её порядок сохранены. Никакая новая запись
не выполняется раньше failure checks текущего quad.

## Проверки и timer

```powershell
py -3.14 -B -X utf8 ports/nds/tools/map/check_door_lazy.py
```

Actual C9/C10 replay сравнивает13816 camera poses:936 N07 fixtures и12880
наблюдённых runtime frames. Каждая поза проверяет phase0 и циклическую фазу
из0…29. Дополнительно проверены invalid phase/extent/revision/epoch,
NULL phase view, legal ready-open extent resample, class4, mixed late failure
после частичной compaction и отказ изменить merged door в degenerate fixture.
Всего29342 scene instances, включая676 error returns,12416 cropped и661
hidden outputs. Counts/status/out и все нормализованные scene bytes совпали:
7687134528 байт. На ошибке out/counts сохраняются, lease release и промежуточные
scene side effects совпадают. Pure constructor не менялся и отдельно
не выдаётся за новый native replay.

Отчёты — `door-lazy-equivalence.json`, `door-lazy-source-supersession.json`;
exact C10 source — в `c10-door/`. Полные сцены используют исходные LEVEL
world/bindings с runtime camera poses, а не изменяемую live world snapshot.

При `N21P_PROFILE && N21P_GEOMETRY_PROFILE` добавлена одна пара
`cpuGetTiming` сразу после успешного base prepare. Она измеряет весь отдельный
door adapter, включая NULL-phase return либо release на failure, и пишет
`scene->profile_ticks[3]`. В host equivalence profiling выключен. Timer имеет
собственную стоимость; общий geometry remainder не называется чистым door time.

## Phase0: исследование без дополнительной реализации

C10 сохраняет прежний phase0 constructor call и последующий memcmp.
Потенциальный shortcut допустим только после доказательства всех прежних
validation: source kind, image width/height1…32767, extent≥1 и phase=0.
Phase0 возвращается до проверки crop mode/UV, поэтому эти дополнительные
проверки нельзя произвольно вводить. Extent0 обязан по-прежнему завершаться
ошибкой, несмотря на phase0. Положительный extent при phase0 не даёт hidden.
Source cache уже строит исходную door geometry с теми же immutable bindings,
но использование этого postcondition для отдельного shortcut потребует
своего узкого proof. Такой shortcut в C10 не добавлен.

Влияние на совместный CPU/FPS измеряет основной N21P runtime. Эти host checks
не подтверждают прохождение бюджета либо аппаратную DS/DS Lite, отложенную N24.
