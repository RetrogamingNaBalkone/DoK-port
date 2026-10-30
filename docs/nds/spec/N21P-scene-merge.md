# N21P C9: точные диапазоны merge и отдельный профиль geometry

C8 `scene.c`, его отчёты, инструменты и описание сохранены в
`research/evidence/nds/n21p/pre-merge/` с pins. C9 меняет только return type
private `component` и типы промежуточных массивов `dok_render_merge`.
Общий результат, validations, provenance и порядок merge сохранены.
UV collinearity products по-прежнему вычисляются с явным int64 cast.

## Доказательство диапазонов

До любых разностей `vertex_range` проверяет все восемь вершин:
XYZ−4096…4095, UV−134217728…134213632. Поэтому каждый XYZ delta имеет
модуль≤8191, UV delta≤268431360; оба помещаются в signed32.
Выбранный width axis всегда XYZ, а не UV. Его произведение aw×bw имеет
модуль≤67092481. Каждый XYZ cross component — разность двух произведений,
поэтому его модуль≤134184962, также внутри signed32.
UV delta×XYZ width axis может достигать2198721269760: эти проверки
сохраняют signed64. Никакого modulo wrap, насыщения или приближения нет.

Сравнение выполняет actual C8 и C9 merge API.1140000 вызовов совпали:
60000 приняты и1080000 отвергнуты.20000 корректных affine pairs покрывают
три плоскости, положительные/отрицательные ширины и высоты, предельные UV.
240000 mutations изменяют атрибуты, shared edges, UV continuation и границы
диапазона;100000 дополнительных random pairs сохраняют общую грань и
проверяют произвольные XYZ/UV внутри полного допустимого диапазона.
Каждый вариант проверяет отдельный out, out=a и out=b. При reject out
сохраняется побайтно. После изменения весь scene prepare совпал с C8
на13816 кадрах и3619570944 нормализованных байтах.

```powershell
py -3.14 -B -X utf8 ports/nds/tools/map/check_scene_merge.py
```

Отчёты `scene-merge-equivalence.json` и `scene-merge-source-supersession.json`
содержат function-body hashes и цепочку к неизменённому C8 frustum proof.
Exact C9 source до последующей instrumentation сохранён в `c9-merge/`.
C8 отчёты не переписаны. Trig, bounds и visible в C9 не менялись.

## Instrumentation отдельно от алгоритма

В следующей сборке N21P_PROFILE добавляет только диагностические замеры
ctor/cull/merge и16-byte conditional tail существующего scene slab.
`source_visible`/`merged` сохраняют результат прежнего вызова в local int.
Геометрические helper bodies и их function hashes неизменны.
После удаления PROFILE blocks и нормализации этих двух locals prepare
побайтно совпадает с exact C9; header при PROFILE0 совпадает с прежним.

```powershell
py -3.14 -B -X utf8 ports/nds/tools/map/check_scene_profile.py
```

Этот отдельный gate сравнивает все13816 fullscene outputs при PROFILE0
с C9 snapshot и пишет `scene-profile-equivalence.json`, сохраняя C8/C9 proofs.
Таймеры при PROFILE1 имеют собственную стоимость; данный gate не объявляет
её нулевой. Поле «other» основного runtime — geometry total минус три
замеренных вызова; оно включает door/remainder/overhead и не называется
чистым временем дверей. Совместный FPS и аппаратная проверка остаются
в приёмке основного N21P и N24 соответственно.
