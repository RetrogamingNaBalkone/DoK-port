# C8: независимая проверка конкретной реализации

29 октября 2026 года. Прочитан конкретный код C8 перед эмуляторными результатами.
Продукт аудитор не изменял, runtime не запускал. **Статический review PASS;
это не performance/pixel приёмка.**

Результат и 17 входов с SHA, включая девять сохранённых source/proof inputs,
два frozen build manifests и шесть ROM/ELF/map artifacts:
`research/evidence/nds/n21p/resource-audit/C8-review.json`, копии в `c8-snapshot/`.
Предыдущие C7 source snapshots и его подтверждённый DMA counterexample сохранены.

Реализация устраняет обе найденные причины риска:

- `nds_map_transfer()` переносит только подготовленный bitmap, сразу после
  admission в начале VBlank. Состояние prepared меняется 1→2; повторный
  transfer ничего не делает. Seqlock остаётся odd.
- GPU stream upload следует сразу после bounded transfer. Полный map VRAM
  memcmp и публикация выполняются позже, после stream/HUD; дорогой audit
  больше не задерживает pending texture upload на целую развёртку.

Map admission разрешает начало 192…224. При позднем входе выполняется одно
новое ожидание и повторная проверка с отказом при нарушении. Реальные start/end
и продолжительность transfer проверяются дополнительно. Дополнительное ожидание
публикуется в word62, включено в wait/CPU exclusion; совместный timestamp и
sampled VBlank counter относятся к новой границе. Это не скрытый loading кадр.

Map CPU складывает только prepare, bounded transfer и финальный commit;
между ними не учитываются wait, GPU upload и HUD. Дорогая audit-проверка
остаётся CPU-работой. Counter history теперь использует sampled presentation
VBlank, а не более поздний counter после audit, поэтому timestamp и counter
не расходятся при длинной диагностике.

GPU upload сохраняет прежние start/end VCOUNT guards. Дополнительно actual
upload_bytes проверяются относительно **того же** sampled boundary: completion
раньше 119×2130 ticks. Это предотвращает маскировку пропущенного deadline через
VCOUNT modulo263. Учитываются также palette-only uploads, а не только textures.

В word63 packed GPU scanlines cold-кадра относятся к preload до submit, когда
3D находится в loading mode. Независимый checker должен связывать профиль
с history LOADING и отдельно классифицировать этот путь. Такое исключение
не распространяется на cold map DMA: для него blank-only gate обязателен.

Источники `host-dirty-map.json` и `scene-visibility-equivalence.json` сверены
по их source pins. Host map: 16 948 549 checks на каждый из двух режимов,
включая split/duplicate transfer/odd snapshot и отсутствие verification до
финального commit. Scene proof: 604 425 quads полного signed32 domain,
13 816 полных scenes. Точное преобразование frustum использует оставшиеся
plane bits и camera dot, без нового clipping или ослабления исходного домена.

Рекомендовано перейти к frozen build и root runtime. Приёмка требует всех
actual map DMA, GPU deadline/texture checks, full RU/EN cadence и тяжёлых
cases 14/15/39, independent LCD/native visited/world и audio/touch/restore gates.
Физическая проверка VBlank/DMA/texture fetch остаётся N24.

После сборки дополнительно сверены reviewed product SHA с frozen manifests
`candidate8-ru` и `candidate8-frontend-ru` и actual ROM/ELF/map SHA обоих targets.
Статический review относится именно к этим байтам, не только к рабочему дереву.
