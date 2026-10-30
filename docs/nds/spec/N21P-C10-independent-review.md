# C10: независимый review ленивого копирования дверей

29 октября 2026 года. Read-only проверка frozen `candidate10-fine-ru`.
Продукт аудитор не менял, runtime не запускал. **Статический review PASS;
измеренный выигрыш и окончательный бюджет пока не подтверждены.**

`research/evidence/nds/n21p/resource-audit/C10-review.json` содержит SHA
проверенных источников, frozen manifest/ROM/ELF/map и host proof.
Отдельные копии source/proof/manifest сохранены в `c10-snapshot/`.

## Корректность изменения

В исходном post-base обходе каждая грань копировалась в локальный 92-байтовый
объект и обратно, даже если дверь не изменилась и compaction не требовался.
Новый код использует source/destination pointers и копирует только при сдвиге
индекса или после успешной проверки crop.

При stable compaction destination index никогда не превышает source index.
Разные элементы массива не перекрываются. Когда адреса совпадают, unchanged
record уже находится на окончательном месте. При hidden door destination
не увеличивается; последующие чтения не затираются записью предыдущей грани.
Локальная копия cell metadata сохраняет исходные pointers/counts при compaction
массива cells. Scene frame.cell_count меняется лишь после завершения обхода.

Для crop сначала проверяется независимый source-cache/cropped geometry record;
после успеха при необходимости копируется исходный quad и изменяются вершины.
Флаг copied не даёт затереть этот результат завершающей копией. При ошибке
раньше прежнего порядка ничего не копируется. Phase0 validation, merged-door
refusal, native geometry helpers, counts и порядок отказов не сокращены.

Host proof сравнивает целые normalized scenes и адресные offsets, а не только
видимые грани: 13 816 поз, 29 342 scene cases, 408 focused cases, 676 error returns,
12 416 cropped и 661 hidden outputs. Покрыты 30 фаз, invalid phase/extent,
epoch/revision, NULL phases, mixed late failure, class4 и degenerate refusal.
Proof выполняется с profiling=0 и относится к immutable initial geometry;
он не объявляется новым mutable-world playthrough.

## Профиль и пределы вывода

Fine ABI3 публикует build/cull/merge/**прямой post-base door timer**.
Последнее поле не является прежним ABI2 residual `geometry_other`. Оно не
включает base scene preparation и не обещает покрыть pre-base validation
или initialization wrapper. Старые ABI1/2 расшифровываются отдельно.

`N21P_GEOMETRY_PROFILE` по умолчанию 0. В обычной сборке нет дополнительных
per-quad clock reads и 16-байтового массива profile_ticks, публикуется ABI1.
У C9 fine clocks добавляли заметную нагрузку; финальную cadence проверяют
отдельной обычной сборкой. Число удалённых копирований не равно доказанным
миллисекундам, а более быстрый fine route не заменяет весь штатный RU/EN gate.

## Оценка будущего in-place cell builder

Направление безопасно при узком контракте: один shared private build-into(local
cell) сохраняет нынешние validation, полный clear и исходную арифметику.
Публичная wrapper сохраняет candidate!=out и копирует out только после OK.
Scene читает cell только после OK; при ошибке выходит до lease/generation/
frame-pointer publication. Поэтому частично построенный локальный cell на
ошибке не наблюдаем внешним потребителем.

Это убирает один 1108-байтовый copy на gathered cell и один такой stack object,
не требует нового RAM cache или системы invalidation. Не следует одновременно
менять unused-zero/error contracts или дублировать native constructors.

При реализации отдельно проверить geometry-cache=1 path, Makefile rename
публичного символа в `_uncached` и dual-object differential aliases. Нельзя
молча обойти действующий cache flag. Переполнение/частичный alias pointer
не разрешать публичному API ради внутреннего удобства.

ROI оценивать после C10 cost. C9 build6,755 мс — верхняя граница всей
профилированной build-стадии с clock overhead, а не обещание экономии от copy.
Внедрение in-place до парного замера C10 аудитором не предлагалось.
