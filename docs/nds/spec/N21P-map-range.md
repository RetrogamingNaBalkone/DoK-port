# N21P: однократный видимый диапазон клеток

C19 исправляет измеренную регрессию C18, без кэша или изменения мира. Один
проход не более64 колонок находит `[xbegin,xend)` внешних включительных
прямоугольников, пересекающих0…255. Для каждого x проверяются `right>=0` и
`left<=255`; при положительном scale позиции убывают монотонно, поэтому
допустимые x образуют один непрерывный диапазон. Если видимых колонок нет,
begin=end=width и оба прохода пусты. Делений, persistent state и RAM allocation нет.

Каждая видимая строка начинает индекс с `z*width+xbegin`, сохраняя исходные
линейные индексы для analytical seen/PRE и cross-row ±1 quirks. Внеэкранные
строки по-прежнему пропускаются только при `top>159 || top+scale<0`. First-pass
FCMap glyph/visited/colour filter и вычисление координат после него восстановлены
точно как C17, без новых per-cell bounds checks. Во втором проходе координаты
также вычисляются после прежнего seen filter. Hero footer всегда выполняется,
включая случай пустого диапазона и scale2 arrow за внешней границей клетки.

Предыдущие исходники/доказательства C18 сохранены в `pre-map-range/pins.json`.
Новый `ports/nds/tools/map/check_map_range.py` использует неизменный meaningful
host harness:110332 raster cases,7784 rect cases, все5 scales/4 byte alignments,
edge/index/PRE cases, input immutability и8 hero escape cases. Whole-source
обратное преобразование к C17, неизменность helpers/footer и host comparisons
прошли. Отчёты `map-range-equivalence.json`, `map-range-native.json` и
`map-range-source-supersession.json` относятся только к C19; C18 не переписывается.
Fresh native954 /39 075 840 пикселей прошли без различий; SHA обоих оригиналов
до/после и все source pins совпали.

Root diagnostic1066 подтвердил функциональную эквивалентность, но строгий
бюджет пока не выполнен: один интервал3 VBlank в case16, минимум29,42267 FPS.
В воспроизводимом frame66/ordinal1 raster сократился с4,65448 до4,03640 мс,
а CPU — с32,7366 до32,12074 мс относительно C17. Диапазон сохранён как
измеренное ускорение; сам по себе он недостаточен для приёмки N21P.

Он сохранён в финальной C21. Совместно с поздним diagnostic clean/count
полные normal RU/EN прошли без3VB, minimum29,913049 FPS; итоговая приёмка
и независимые103 reports/11 266 file-pin checks — в паспорте N21P.
