# N21P C18: независимый аудит offscreen map cull

29 октября 2026 года. Продукт не редактировался, эмуляторы этим аудитором
не запускались. Итог: **source static, frozen equality и host/native proof
review PASS**. Runtime/budget acceptance этим аудитором не заявлен.

Source proof: `research/evidence/nds/n21p/resource-audit/C18-source-review.json`,
воспроизводится `review_c18_source.py`; reviewed bytes сохранены в
`resource-audit/c18-snapshot/`. Точное удаление двух row/cell culls и возврат
координат в прежние места восстанавливают **весь** исходник `map_raster.c`
из `pre-map-cull/pins.json`, SHA которого также проверен.

`C18-frozen-host-review.json` подтверждает equality reviewed raster с
snapshot/current/C18RU manifest и baseline с C17 manifest; actual ROM
`dc1754c7688d6867cdd9e6714de0536b740bd91ac4fd611af020a356317874a1`, ELF
`f945f24373d50729b06ab9b7e26e6a5dc92d2abef69d6f6e4ea268e1d8bea58a`.
Host proof и реальный test source прочитаны:110332rasters,7784rectangles,
4alignments,124guard,92720edge,8hero-escape,220664input-unchanged checks;
сравниваются buffers вместе с guard bytes и return values. Существующий
baseline компилируется отдельно, current включён из настоящего source.

Отдельный `C18-native-review.json` независимо проверяет свежий producer
report:954PKR/PKE comparisons,39075840pixels, different_pixels=0. Actual
hashes оригиналов совпали с before/after, native source SHA — с frozen C18.
Execution checker copy побайтово восстанавливается из исходного checker
ровно тремя filesystem replacements; case/assertion logic не менялась.
Аудитор прочитал и сверил доказательства, но не перезапускал эти прогоны.

## Почему пропуск безопасен

Растр использует inclusive endpoints. Skip проверяет только строгое
непересечение outer cell rectangle [left,right]×[top,top+scale] с
[0,255]×[0,159]. Клетки с right=0, left=255, top=159 или top+scale=0
сохраняются. Во всех cell branches масштаб≥2 гарантирует, что fill,
interior, class2 edges и class4/7 lines не выходят за этот outer rectangle.
Уже существующий `rect` всё ещё окончательно clip'ит каждый примитив.

Hero arrow рисуется отдельно неизменённым footer. Это существенно:
его фиксированные offsets1..3 при scale2 могут выходить за outer cell;
применять к нему новый cell reject было бы ошибкой.

При row skip index увеличивается ровно на width; при cell skip increment
остаётся в for header. Сохраняется index=z×width+x в обоих проходах.
Пределы width/length≤64, scale≤8 и offsets[-32768,32767] оставляют все
новые координатные выражения внутри int32; unsigned width-x не underflow
при x<width. Input validation/return order перед traversal неизменны.

Первый FC pass остаётся перед native/class pass. `seen` не изменён:
class1 определяется условием index≤current и исходной TMap, а не фактом
предыдущего фактического выполнения loop body. Поэтому class1 в пропущенной
offscreen клетке по-прежнему виден соседу, как если бы traversal прошёл её.
Cross-row ±1 neighbour semantics не «исправлялись». PRE visited не получает
POST class1 writes в первом проходе. World/snapshot остаются const; новых
visited mutations, cache, retained pointers или allocations нет.

## Что действительно измерено до C18

Полные C17 RU/EN: functional runtime и полный захват12880profiles PASS,
но normal cadence **FAIL**. На6637 normal intervals RU histogram1/2/3VB
=4407/2229/1, EN=4398/2238/1; минимум окна29,422671FPS на обоих.
Единственный3VB — frame634/case16/ordinal1. Matrix17 завершил10commands,
auditRU прерван root STOP после cadence rejection; completed не означает
budget PASS. Pins и расчёт: `resource-audit/C17-case16-costs.json`,
`collect_c17_case16.py`; root `matrix17/rejection.json` также закреплён.

На frame634 (RU) CPU32,739мс: geometry8,426; native disclosure6,488;
submit5,623; map8,897. Disclosure уже входит в sprites8,052 — эти столбцы
не складываются повторно. Geometry+native+submit **20,537мс** без raster
уже превышают16,716мс на59,826Гц. EN даёт тот же вывод:20,543мс.
Это предел текущей работы данного кадра, не аппаратный потолок DS.

В case16 всего21actual raster на120frames. На этих21 RU map в среднем
9,213мс: raster4,968; snapshot1,204; DMA1,549; publish0,832; buttons0,416;
postvisited0,217. При idle raster map около1,034мс. Раскрытие native работает
каждый frame независимо от dirty raster: первые43ordinals в среднем5,299мс;
geometry7,916 и submit4,938. Общий warm average18,535мс скрывает тяжёлые
положения камеры; оптимизировать только среднее было бы неверно.

Отдельный actual frontend live route C17 даёт57,337RU/57,188ENFPS, но это
не обещание60 на heavy: другое распределение камер, а минимальные live окна
49,855/48,858FPS. Presentation cadence измеряется между границами; CPU timer
одного frame и post-VBlank tail не совпадают с этой границей. Из CPU32,739мс
нельзя вывести, что следующий presentation обязательно уложится в2VB.

Profile `upload_bytes` здесь — cumulative **map** bytes (публикуется из
`map_ui->upload_bytes`), не per-frame GPU payload. `stream_commit` в C17 —
сумма preclean и actual commit CPU, не один непрерывный post-VBlank span.
Эти accounting distinctions закреплены в machine report.

## Разумный предел дальнейших работ

C18 проверяет простой пропуск работы, чей вывод всё равно полностью
clip'ится, и не требует persistent cache. Пока он не измерен, обещать
выигрыш или закрытие30FPS нельзя. Если C18 закроет строгий30budget,
дальнейшие product micro-optimizations в эту приёмку добавлять не стоит.
Статические caches native/geometry потребовали бы новой invalidation
семантики и proof; предыдущая гипотеза долгих field54 scans для heavy
опровергнута реальными counts. Нынешний hidden-overlay стоит лишь около
0,03мс и уже не является существенной целью.

DS-specific ITCM путь уже использован в C17:28872/32768байт, headroom3896;
старые данные C13 о31632свободных больше не описывают текущую сборку.
Exact MTX_TRANS остаётся отдельным исследованием с аппаратным oracle,
fallback и timing, не готовым оправданием новых правок ради60.

## Отдельная проверка matrix18 test driver

`resource-audit/matrix18-driver-review.json` закрепляет source SHA;
`review_matrix18.py` парсит их без импорта/запуска jobs. Full steady wrapper
**побайтово равен matrix17** и только наблюдает120дополнительных presentations
после первого PLAY каждого level/language; emulated RAM writes нет.

В `run_matrix.py` просмотрен небольшой delta: labels17→18, cancelcheck перед
Popen, finally terminate/wait для owned process, blocked restart+final JSON
при settings failure вместо assert. Hash gates ROM/ELF, byteexact clones,
защита существующего final output и acceptance_eligible=false сохранены.
План:11main jobs плюс отдельный root RU,32early,3settings+conditional restart.
`commands_completed_successfully` не является performance acceptance.
Approved targets отсутствовали при этой static проверке; их будущие SHA
и runtime результаты этим review не принимаются.
