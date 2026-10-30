# N21P C17: независимая проверка переноса source cache clean

29 октября 2026 года. Read-only review исходников до freeze; продукт не
изменялся, эмулятор не запускался. Результат: **static PASS**; runtime
DMA window, pixels, memory и полный RU/EN budget этим не принимаются.

`research/evidence/nds/n21p/resource-audit/review_c17.py` воспроизводит
проверку. `C17-review.json` закрепляет SHA семи входов; точные reviewed
product bytes сохранены в `resource-audit/c17-snapshot/`. База —
`research/evidence/nds/n21p/pre-stream-clean/pins.json` с проверенными SHA.

## Порядок и lifetime

`effects_prepare` заканчивает генерацию texels/palette, разрешает конечные
указатели из effect cache и вызывает `stream_plan`. Затем завершаются
diagnostic copies, flat quad adaptation, UI fixture, `nds_map_prepare`
и audio service. Новый `dok_nds_stream_prepare_uploads` вызывается после
этих операций, перед submit/SwapBuffers. Он чистит только valid+pending
texture ranges и pending palette; не меняет pending flags, stream state,
VRAM mappings и не запускает DMA.

Между prepare и `commit_prepared` проходят geometry submit, ожидание
границы и bounded map transfer. В этом пути нет texture/effect cache
require, compaction, palette generation или смены pending identities.
Map pixels находятся в IO storage; payloads — в raw/effect pools,
palette — в effects runtime. Промежуточные GE/diagnostic операции не
мутируют их bytes. Источник не заимствуется из временного map scratch.
Header явно требует сохранить bytes, allocations, palette и pending
identities до commit. Нового persistent prepared flag/cache нет.

Cold/loading сохраняет прежний полный `dok_nds_stream_commit`.
Нормальный frame использует prepare и затем prepared commit. Перенос
CPU cache clean перед SwapBuffers уменьшает работу в узком upload окне,
но сам по себе не доказывает попадание в это окно.

## Независимая проверка точного delta

Проверен whole-file reversal, а не только чтение отдельных новых строк:

- Shared commit kernel после удаления двух `if(clean_sources)` и возврата
  старого имени/signature полностью совпадает с прежним public commit.
  Значит null/planned → start VCOUNT guard → darkness → bank mapping →
  texture DMA → bank restore → palette DMA → end guard/publication порядок
  и error precedence сохранены. Public wrapper передаёт1, prepared —0.
- Prepare совпадает с отдельно зафиксированным коротким exact block:
  единственные действия после planned guard — исходные cache-clean ranges.
- Main после возврата прежнего commit call и точного удаления prepare/timer
  additions полностью совпадает с базой. Map-first scheduling, actual DMA
  line checks, дополнительное bounded ожидание и119-scanline epoch deadline
  не ослаблены.
- Header отличается только явным lease contract и двумя prototypes.
- `row.io_upload_ticks` и profile26 включают preclean CPU плюс actual commit
  CPU. Submit timer начинается после preclean; VBlank wait туда не добавлен.

Материального нового correctness риска в проверенном delta не найдено.
Prepared API намеренно требует корректного call order; его нельзя использовать
после нового plan/reset или модификации payload. Существующий полный public
commit сохраняет контракт для остальных callers. Не следует добавлять общий
cache/invalidation слой для этой узкой перестановки.

Frozen extension выполнен отдельно: `resource-audit/C17-frozen-review.json`
подтверждает reviewed SHA == snapshot == current == candidate17-ru manifest
для всех трёх product sources. Проверены реальные ROM/ELF bytes:
`ce51568ffbaab2b8a4a9ae36449538c0f0633cb5fae2701c20c0d0be104d0b54` и
`9af25f906f34ef24e5406cdff3756a37aebdd9a569d0eacb6d94afbe423ec857`.
Исходный `C17-review.json` оставлен без изменений. Primary host report
`stream-clean-equivalence.json` закреплён по SHA; независимо не перезапускался.
Runtime guards и независимые LCD/DMA проверки остаются обязательными:
этот аудитор не запускал runtime и не присваивает ему acceptance.
