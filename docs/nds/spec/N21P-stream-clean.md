# N21P: очистка CPU-источников до окна GPU upload

C16 heavy153/153 по2VB не заменил full gate. Полный RU-набор остановился на
frame1791/case27: map DMA занял начало VBlank, а GPU commit216→49 завершился
на две строки позже принятого окна192…262/0…47. Старый hardware guard вернул
ошибку; он не ослаблен. Отказ C16 и partial matrix сохранены root отдельно.

C17 сохраняет map/GPU DMA и все VCOUNT/epoch guards, но переносит CPU cache
clean pending текстур и палитры до SwapBuffers. Новая явная пара
`dok_nds_stream_prepare_uploads` / `dok_nds_stream_commit_prepared` требует
неизменности CPU bytes, allocations, palette и pending identities между ними.
Prepare не делает DMA, remap VRAM, FIFO или публикацию и не меняет pending.
Старый публичный `dok_nds_stream_commit` по-прежнему выполняет полный clean и
сохраняет validation/error/VRAM/DMA порядок. Новый shared kernel различается
только условием у двух прежних DC_FlushRange. Cold preload использует старый API.

В main pair начинается после effects/stream_plan, UI/map preparation и audio
service. Затем submit лишь читает геометрию, boundary wait и map transfer не
меняют GPU payload. Новый persistent cache, validity flag или allocation не
вводится. Независимый reviewer проверил весь lease и обратное преобразование
трёх product-файлов к frozen C16: `resource-audit/C17-review.json`.

`py -3.14 -B -X utf8 ports/nds/tools/check_stream_clean.py` извлекает настоящие
старый commit и новые prepare/kernel/wrappers.4096 public cases сохранили все
байты VRAM/state, результаты и error precedence;4096 prepared pairs проверили
cache-shadow модель flush-before-DMA и неизменность pending/plan в prepare.
2792 success и1304 refusal включают active-window errors, null stats, отсутствие
plan, разные pending slots,256/4096 palette colours и loading. Source reversal
ограничивает diff main новым pair и accounting. Host VCOUNT не доказывает
runtime deadline: это отдельный root transition/full/LCD gate.

`row.io_upload_ticks` и profile p26 (`stream_commit`) теперь суммируют два CPU
span: preclean перед submit и commit после boundary. Между ними находится wait,
который в эту сумму не входит. Это composite CPU time, а не непрерывная
длительность передачи; общий CPU excluding wait по-прежнему включает clean.
profile63 остаётся фактическими commit start/end VCOUNT. Сумма CPU spans как
верхняя граница transfer-duration в независимом checker консервативна.

Перед повторной full matrix обязателен targeted transition-пакет root с реальным
предшествующим resource state, включая26→27,34→35,57→58, и strict DMA/GPU gate.
Бюджет, классификация loading и нормальные интервалы не меняются.
