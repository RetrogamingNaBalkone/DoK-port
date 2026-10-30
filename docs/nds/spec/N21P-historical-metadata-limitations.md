# N21P: два ограниченных пробела исторических метаданных

`research/evidence/nds/n21p/historical-metadata-limitations.json` фиксирует
ровно два старых вложенных reference, чьи исходные байты не найдены в
рассмотренной области N21P snapshots. Root отдельно разрешил оформить эти
ограничения. Старые JSON не переписаны, текущие несовпадающие файлы не
объявляются восстановлением прежних bytes. Ослаблений source integrity,
DMA или FPS здесь нет.

1. `resource-audit/C7-review.json` закрепил тогдашний `host-dirty-map.json`:
   5132 байта, SHA-256 `131cedd5157be2a8810e7ace580056064974f6cff4b34d1a590c6b339e3f1149`.
   Collector прочитал и хэшировал proof, но не скопировал этот JSON в свой
   snapshot. В этой одной части старый review полностью не воспроизводится.
   C7 отклонён из-за map DMA и не является текущей приёмкой. Текущие
   dirty/native/source proofs и полные C21 runtime gates проверяются отдельно.
2. `oracle-review/final-driver17-review.json` сослался на изменяемый
   `candidate17-ru/root-run-process.json`:506 байт, SHA-256
   `de2554f81b8a708130ceda14dd2b7cfec36c6c4544c1f0f626eb9c83105fade4`.
   Это исторические PID/orchestration metadata, а не source, ROM или packet.
   Прежнее содержимое не найдено; его область заменяют frozen C21 driver
   reviews, approved-target pins и новые фактические результаты запусков.

Для обнаруженных35 уникальных непокрытых SHA проверены34 769
локальных text/proof файлов:33 версии найдены по точному SHA,54 references
получили явный saved path в draft artifact inventory. Ошибок обхода0;
применено только ранее рассмотренное точное исключение недоступного
temporary directory. Полнота поиска за пределами этой области не заявляется.

Нельзя утверждать100% проверку всех вложенных исторических pins. Для C21
по-прежнему обязательны точные текущие source/tool/manifests/ROM/ELF,
полное покрытие профиля, функциональные/native/audio/LCD проверки и окна DMA.
Данный artifact не превращает незавершённую финальную матрицу в PASS.
