# N21P C19: независимая проверка однократного visible xrange

29 октября 2026 года. Продукт/постоянные инструменты не редактировались,
эмуляторы и host/native raster tests этим аудитором не запускались.
Source, frozen RU equality и host/native proof review: **PASS**.
Runtime/performance acceptance этим аудитором не заявлен.

`research/evidence/nds/n21p/resource-audit/C19-review.json` закрепляет23входа;
`review_c19.py` воспроизводит source/hash проверки. Reviewed bytes и snapshot
host report сохранены отдельно в `resource-audit/c19-snapshot/`.
Actual frozen ROM:30786bb55125b2199b74b5bb036916c21ad4029fe30a995c17e0d5bca7fd3651;
ELF:d51e5a448f9bdc498582c355f653be47a44941fcdf2655432ee930f42385f37f.

## Проверенный алгоритм

При допустимом положительном scale обе функции
right(x)=offset_x+(width−x)×scale и left(x)=right(x)−scale строго убывают.
Условия right≥0 и left≤255 задают пересечение двух integer half-intervals.
Поэтому пересекающие viewport outer rectangles образуют один contiguous range.
Scan по width≤64 сохраняет первый matching x и последний matching x+1;
конец exclusive. Если совпадений нет, остаётся xbegin=xend=width.

Границы inclusive: right=0 и left=255 включены, row top=159 и top+scale=0
также не отбрасываются. Input guards прежние. Координаты остаются в
[-32768,33279], linear index не превышает4096;4096 возможен только как
empty/one-past, и тело loop тогда не выполняется. В каждой row начальный
index=z×width+xbegin, каждый next x увеличивает index на1.

Внутри cell bodies нет нового reject branch. First pass сначала читает
glyph marker и выполняет прежний visited/background/excluded filter, затем
считает right. Second pass считает координаты после прежнего seen/glyph
выбора. Это устраняет C18 unconditional coordinate work для unseen cells.
Final rect clipping остаётся полным.

`seen` остаётся аналитическим: visited[index] ||(index≤current &&class1).
Класс1 в offscreen skipped клетке логически учитывается последующими соседями;
нет зависимости от факта исполнения предыдущего loop body. Cross-row ±1
поведение и порядок FC PRE pass перед class pass неизменны. Hero footer
отдельный: при scale2 его фиксированные offsets могут быть видимыми даже
при полностью offscreen cell и пустом xrange, поэтому footer не отбрасывается.

Удаление ровно initial scan, двух row checks и возврат прежних loop/index
headers восстанавливают **весь** C17 raster source из `pre-map-cull`.
Его SHA совпал с frozen C17 manifest. Все остальные helpers, cell bodies,
hero footer, validation и return/error behavior совпадают побайтово.
Reviewed current source совпал с C19manifest/snapshot, ROM/ELF проверены целиком.
Persistent cache, retained data и новые world/visited writes не добавлены.

## Что доказывают тестовые материалы

Host `map-range-equivalence.json` и его test source прочитаны и закреплены:
110332rasters,7784rectangles,4alignments,124guards,92720edge cases,
8hero-escape,220664input-unchanged checks. C18 harness сохранён **byteexact**
в `pre-map-range`; в C19 он сравнивает actual current source с отдельно
скомпилированным C17 baseline. Whole-source reversal независимо проверен
этим аудитором; range correctness обоснована приведённой выше монотонностью
и имеющимися boundary/empty/offset/PRE/hero случаями.

Новые числовые vector suites не добавлялись. Совпадение source/proof hashes
не заменяет runtime timing; выигрыш ещё должен подтвердить root на том же
focus, затем полном RU/EN наборе.

Свежий native proof дополнительно проверен в `C19-native-review.json`
(12pins, воспроизводится `review_c19_native.py`):954PKR/PKE comparisons,
39075840pixels, different_pixels=0. Original EXE actual hashes совпали
с before/after, actual native source hashes — с C19 frozen manifest.
Execution checker копия восстановлена побайтово из original checker
ровно тремя filesystem substitutions; тестовые cases/assertions неизменны.
Этот аудитор прочитал и сверил материалы, но не перезапускал native proof.

## Почему C18 не принят по скорости

Изначальные C18 correctness reviews сохранены неизменными. Отдельный
`resource-audit/C18-speed-rejection-review.json` закрепляет завершённый
1066-frame focus: frame66/case16/ordinal1 всё ещё3VB, raster5,19058мс,
CPU33,27492мс. Это performance rejection, совместимый с correctness PASS.
C19 меняет организацию traversal; его speed PASS нельзя выводить из того,
что корректность C18 и C19 доказана.

Ранее измеренные geometry+native+submit20,54мс на heavy C17 frame634
остаются выше16,72мс для60FPS без raster. Даже успешный C19 budget30
не будет обещанием60 на всех тяжёлых сценах. Если30budget закрыт,
не следует добавлять новые generic caches или micro-optimizations в эту
приёмку без отдельного измеренного основания.
