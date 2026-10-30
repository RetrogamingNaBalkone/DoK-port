# N21P: пропуск полностью внеэкранных клеток

**C18 отклонён по быстродействию**, при сохранённой побайтовой корректности.
Focused1066 показал raster4.65448→5.19058 мс и CPU32.7366→33.2749 мс: тот же
один3VB-интервал, budget FAIL. Безусловные координаты перед FCMap visited/colour
filter добавили больше работы, чем сэкономил cull. Full C18 не запускался.
Контроль и причина: `matrix18/rejection.json`. Следующая версия:
[однократный диапазон C19](N21P-map-range.md).

C17 full RU/EN функционально прошли12880 presentations на язык, но ровно один
нормальный интервал (case16/frame634/ordinal1) занял3VB. Minimum секундного
окна29.42267 FPS нарушает обязательный порог, несмотря на средние≈44.8 FPS.
Расширенный diagnostic1066 воспроизвёл тот же пик при CPU32.7366 мс и raster≈4.655 мс.
Порог, нормальные case labels и clocks не меняются.

C18 отбрасывает только клетки, чей включительный внешний прямоугольник целиком
вне viewport0…255×0…159. Обе raster traversal сначала проверяют строку:
`top>159 || top+scale<0`; skip увеличивает linear index ровно на width.
Затем для клетки проверяются `right<0 || left>255`. Равенства0/255/159 остаются
видимыми. Все FCMap fills, стены, двери и interior markers лежат внутри этого
outer rectangle при scale2/3/4/6/8; раньше их `rect` всё равно не делал stores.
Удалены только эти пустые проходы, порядок остальных stores прежний.

`seen` не мутирует snapshot: class1 учитывается аналитически через source index
и `neighbor<=current`. Поэтому пропущенная offscreen class1 сохраняет влияние
на последующих соседей. Cross-row ±1 quirks и PRE FCMap pass не меняются.
Runtime class1 world writes по-прежнему выполняются отдельно каждый кадр.
Hero arrow оставлен после traversal без cell clipping: при scale2 его три
пикселя могут выходить за outer rectangle собственной клетки. Whole-map early
return перед arrow отсутствует. Нового cache, persistent state или RAM нет.

Суммы координат при offset±32768,width/length≤64,scale≤8 находятся в signed32.
Условия используют строгие outside comparisons, сохраняя inclusive edge strokes.
Независимый reviewer проверил index/PRE/edge dataflow до сборки; фактическое
ускорение требует root timing1066 и полного повторного набора.

`py -3.14 -B -X utf8 ports/nds/tools/map/check_map_cull.py` проверяет точное
обратное преобразование к frozen C17, неизменность helpers/hero footer и
attrs-only связь C17 с принятым исходным N20 raster. Host110332 raster cases,
7784 rect cases,92720 edge/index/PRE cases,8 scale2 hero escape cases и все
четыре byte alignments совпали; input immutability также проверена. Отчёт:
`research/evidence/nds/n21p/map-cull-equivalence.json`. Fresh native954 replay
прошёл39 075 840 пикселей без различий: `map-cull-native.json`. SHA обоих
оригиналов до/после и все source pins совпали. Scoped bridge:
`map-cull-source-supersession.json`; прежние N20/C12 отчёты не переписывались.
Ни host, ни static review не являются runtime acceptance.
