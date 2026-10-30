# N21P — независимая проверка инструментов приёмки

29 октября 2026 года. Проверены root-инструменты `run_joint_emulator.py`,
`joint_hud_oracle.py`, `joint_profile_abi.py`, `run_joint_frontend_emulator.py`,
`run_joint_restore_emulator.py`, `run_joint_combat_emulator.py`,
`check_joint_dma_windows.py`, их принятые N20/N21 основы и итоговый агрегатор
`summarize_ui_performance.py`. Эмулятор аудитором не запускался; runtime и
инструменты приёмки аудитором не изменялись. Это проверка измерителя, а не
приёмка новой сборки.

## Найденные ошибки и исправления

1. **Фаза наблюдателя скрывала штатные интервалы.** В frontend summarizer
   условие `active_phase` исключало записи с фазами `level-loading`, `pause`
   и `pause-return-menu`, хотя product history помечала их только PRESENTED=8.
   В сохранённом C8 live исключались frame2,3,268,488. Все они реально занимали
   два VBlank, поэтому исходный результат не был ложным PASS; сам фильтр мог
   скрыть замедление. Root исправил классификацию: исключения задают только
   product flags LOADING/WARMUP/DIAGNOSTIC/SYNC, паузы разделяют timestamp gaps.
   Старый observer сохранён отдельно, исходный runtime report не переписан.

   Независимый host replay исходного C8 включает теперь487 вместо483 normal
   интервалов и сохраняет budget PASS. Negative control меняет только frame2
   с2 на3 VBlank и сдвигает последующие timestamps на1, сохраняя непрерывность,
   flags8 и фазу `level-loading`. Старый измеритель оставляет PASS, исправленный
   выдаёт FAIL: minimum1s=29.422671289 против порога29.913049144. Доля интервалов
   больше двух остаётся меньше1%, поэтому проверяется именно второй критерий.

2. **Функциональный frontend PASS не гарантировал FPS.** В live frontend
   `passed` и `steady_metrics.budget_passed` имеют разные значения. Старый
   итоговый агрегатор проверял у дополнительных reports только `passed`.
   Исправленный `require_accepted_report` требует frontend budget, ненулевые
   интервалы и окна1s, отсутствие пропущенных profiles; явно отклоняет
   `acceptance_eligible=False`. Восемь независимых host controls проверили
   положительные live/static отчёты и отказы при budget failure, пустой выборке,
   отсутствии окон1s, потерянном profile, diagnostic-only и functional failure.

3. **Пропавший GPU upload marker мог скрыть перенос.** По дополнительному
   вопросу root проверен `main.c`: marker и `PRESENTED.upload_bytes` публикуются
   из одного `stream_stats`, в том числе для cold preload. Старый checker
   пропускал `reserved63=UINTMAX` без проверки actual upload bytes и объединял
   flags разных записей одного frame ID. Root добавил уникальную PRESENTED
   запись на кадр, уникальные profile IDs, равенство двух множеств IDs и
   эквивалентность `marker присутствует ⇔ upload_bytes>0`. Cold-классификация
   теперь берётся только из соответствующей PRESENTED записи.

   Три неизменённых host replays C8 frontend, C8 heavy и C12 heavy прошли.
   Пять negative controls старый checker принимал, исправленный отклоняет:
   missing marker, duplicate/missing profile, duplicate PRESENTED и normal
   GPU upload100→110 с добавленной непредъявленной LOADING записью того же
   frame ID. Исходные reports не менялись. C12 frontend в момент проверки
   ещё отсутствовал и в эти positive replays не входит.

## Сохранившиеся обязательные проверки

- Совместный accumulated observer принимает полный набор только при успехе
  унаследованных runtime/audio/HUD/map gates и точном множестве profile frame IDs
  `1..total_frames`. История читается по собственным seqlock каждого слота,
  независимо от odd map descriptor; сортировка и дедупликация не заменяют
  итоговую проверку отсутствующих и лишних кадров. ABI1/2/3 декодируются явно;
  старые четыре поля не переименовываются задним числом.
- HUD adapter убрал зависимость от дорогого product VRAM-readback counter.
  Проверки исходных сообщений, INDEX8, полной полосы9728 actual LCD RGB pixels,
  world hashes и gameplay presentation descriptor сохранены. Это допустимое
  отделение release от instrumentation; отсутствующий counter не подделывается.
- Карта использует LAST SHOWN snapshot для native x86 pixels и PRE→POST visited,
  отдельный live state — для touch/model/clock checks. Обычная карта всё ещё
  сравнивается и в INDEX8, и по actual LCD RGB. Restore adapter отличается от
  принятого N21 главным образом новым HUD adapter и provenance; требует по
  одной подтверждённой HUD/map паре на каждом из шести level/language points.
- Combat supplement явно является input gate: `--input-only` не доказывает
  карту. Он требует завершения принятого touch-плана, actual combat callbacks
  при drag/после resume и audio PASS. Native map acceptance остаётся за полными
  RU/EN runs. Его отдельный profile report не заменяет exact accumulated capture.
- Короткий cost run явно помечен `acceptance_eligible=False`; итоговая приёмка
  теперь отклоняет такой report даже при искусственно установленном `passed`.
- DMA checker включает cold/loading map transfers, проверяет VCOUNT192..262
  и duration меньше кадра. Для normal GPU commits проверяются упорядоченные
  endpoints единого окна192..262→0..47 и duration; cold GPU preload исключён
  только по LOADING flag той же PRESENTED записи. Полнота profile/history IDs
  и соответствие upload marker actual bytes теперь обязательны. Absolute upload epoch остаётся дополнительным
  product guard, а не независимым доказательством этого checker. Положительное
  число normal GPU samples нужно проверить в окончательном отчёте отдельно.
- Формула бюджета сохраняет оба требования: ≤1% интервалов больше2 VBlank
  и minimum enclosing1s cadence ≥refresh/2. Частота выводится из DS timer;
  integer count29 в точном окне1s не подменяет cadence. 60-цель отдельная.

## Доказательства и границы

Локальные доказательства:
`research/evidence/nds/n21p/oracle-review/phase-negative-control.{py,json}`,
`aggregate-negative-control.{py,json}`, `dma-negative-control.{py,json}` и
`review.json`. Исходные файлы,
архивная версия observer и C8 report привязаны SHA-256. Проверен синтаксис
семи основных инструментов; эмулятор не инициализировался.

После указанных исправлений дополнительных блокирующих ошибок в проверенном
коде не найдено. Это ограниченный статический вывод с12 отрицательными
host controls и положительными replays исходных отчётов. Незапущенные restore/combat/all-level frontend gates
не считаются пройденными. Итоговый runtime matrix, его pin verification,
положительная DMA/GPU coverage и проверка strict budget для final RU/EN
остаются обязательными. Физическая DS и прослушивание остаются N24.
