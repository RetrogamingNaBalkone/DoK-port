# Независимая проверка C7: split-map и lifetime

29 октября 2026 года. Продолжение отдельного аудита по поручению root.
Проверка read-only; продуктовые исходники и сборки не изменялись, эмулятор
аудитор не запускал. Первоначальные `N21P-resource-audit.md`, его результат
и снимки исходников не переписаны.

**Статических блокирующих ошибок в рассмотренных изменениях не найдено.**
Это не приёмка C7: cadence и отсутствие map DMA при активной развёртке
остаются отдельными runtime-обязательствами.

Машинная проверка: `research/evidence/nds/n21p/resource-audit/C7-review.json`.
Сверены 10 продуктовых файлов с обоими frozen build manifests:
`candidate7-ru` и `candidate7-frontend-ru`; отдельно сверены их ROM/ELF/map.
Всего 25 входных SHA, включая существующие host proofs и object files.
Снимки C7 хранятся отдельно в `c7-source-snapshot/`.

## Проверенное поведение

- `main.c` вызывает map prepare после effects/stream planning, до GPU submit
  и SwapBuffers/VBlank wait. Между prepare и commit нет scheduler service
  или другого подтверждённого writer мировых maps. Audio service читает
  собственные buffers; HUD не изменяет map pixels/planes/visited snapshots.
- На redraw prepare закрывает seqlock **до** записи borrowed bitmap, planes,
  glyphs, PRE visited и POST visited. Descriptor сначала хранится отдельно
  в pending_probe. VRAM в prepare не меняется. Commit синхронно переносит
  bitmap, обновляет actual upload counters и только затем публикует descriptor
  и чётный sequence. При no-draw display snapshot остаётся прежним.
- Class1 side effects продолжаются каждый world frame, включая no-draw.
  PRE снимается до class1, POST после. Dirty compare использует PRE visited;
  следующий FCMap redraw debt не потерян.
- На no-draw ветке sequence также остаётся odd на время submit/wait, поскольку
  подготовлен новый live descriptor. Это консервативное окно для наблюдателя;
  оно не означает raster/DMA и не является сокращением CPU-бюджета.
- CPU profile map складывает prepare и commit и не включает промежуточное
  ожидание. Две scanline-метки публикуются отдельно в profile words 60/61.
  Renderer `prepare_ticks` заканчивается раньше map prepare; полный map CPU
  следует брать из отдельной стадии или общего CPU, не из этого старого поля.
- Frontend static payload flush выполняется при draw/mutation. POPAL raw
  payload flush выполняется при изменении asset/address/span или при cache
  insertion/eviction. Raw cache меняется до определения этих условий;
  borrowed demand pointers уже разрешены после возможного compaction.
  Возврат из меню сбрасывает flushed identity, поэтому совпавший адрес
  нового cache после reload не ошибочно считается уже опубликованным.
- В обычном режиме frontend `top_verified/bottom_verified` остаются 0;
  profile word 38 честно сообщает audit mode. Независимый LCD oracle должен
  проверять действительный растр и не подменять отсутствующий C guard.
- Удалён только повторный append memset. Прямая byte-проверка старого и нового
  geometry.c подтверждает единственное удаление. Существующий host proof
  сравнивает все 12 288 клеток, 36 864 sentinel cases, 491 520 изменений bindings
  и 165 690 error returns, включая байты candidate/out и no-mutation-on-error.

## Обязательный runtime gate DMA

`nds_map_commit()` записывает REG_VCOUNT, но не содержит собственного
blank-only guard. Перед ним выполняются stream commit и HUD. Старый stream
guard разрешает строки 0–47 и поэтому сам по себе не доказывает корректное
окно для нижнего bitmap DMA.

Для каждого actual map DMA нужен gate:

```text
192 <= start_line <= end_line <= 262
DMA duration < one complete frame
```

На no-draw обе метки должны оставаться UINT32_MAX. Проверка порядка start/end
обязательна: начало в blank и конец после перехода 262→0 затрагивают активные
строки, даже если прежний stream guard считает этот момент допустимым.
Без полного actual-DMA gate утверждение «DMA не происходит при active LCD»
не доказано только расположением вызова после wait.

Также остаются полные RU/EN pixel/visited/touch/pause/language/restore gates,
проверки POPAL bytes после insertion/eviction и прежний строгий cadence gate.
Положительный host split-тест не подтверждает аппаратное время передачи.

## Дополнительный аудит тяжёлой geometry

Исследованы frozen C7 ARM objects через ELF relocations и Capstone. Тексты:
`C7-scene-disassembly.txt`, `C7-geometry-disassembly.txt`,
`C7-door_render-disassembly.txt` в локальном каталоге аудита.

В scene.o нет вызова software 64-bit division; в bounds присутствует обычный
32-bit idiv. Geometry.o также использует 32-bit idiv. Поэтому объяснять всю
16-мс geometry-стадию массовым int64 divide неправильно. В door intersection
64-bit division действительно есть: пять компонентов фактически обрезаемой
вершины, два статических signed branch call sites на компонент. Для phase 0
этот путь не выполняется. Частоту crop в тяжёлой сцене надо измерить отдельно.

Переданы исполнителю следующие точные кандидаты, без изменения renderer:

1. `visible()` после первого vertex часто имеет только один common_outside bit,
   но продолжает считать все пять planes. Снятый bit никогда не возвращается:
   последующим вершинам достаточно проверять только оставшиеся bits. Можно
   пропускать lateral или dy, если связанные planes уже исключены. Это точная
   алгебра AND, применимая ко всему существующему входному домену.
2. Предвычислить camera dot products и записать rotation как vertex dot minus
   camera dot. Все множители int32; 32×32→64 использует ARM SMULL вместо
   потенциально более дорогого умножения 33-bit delta. При sin/cos ≤32768
   максимальная абсолютная сумма четырёх products <2^48, переполнения int64 нет.
   В текущем frustum нет промежуточного округления, поэтому преобразование точно.
3. `dok_render_merge()` занимает 480 ARM instructions в рассматриваемом object.
   До арифметики vertex_range проверяет xyz в [-4096,4095] и uv в
   [-134217728,134213632]. Все component deltas fit int32: xyz ±8191,
   uv ±268431360. Spatial axis product не превышает 67 092 481, spatial cross
   не превышает 134 184 962. Поэтому aw/bw/component/height/cross могут быть
   int32; только UV×spatial-axis collinearity products требуют int64.
   Это доказательство для всего уже проверенного API domain, без нового guard
   и без ослабления точности.

Ни число instructions, ни число helper call sites не являются измеренным
вкладом в мс. Перед выбором следующей правки нужен раздельный build/cull/merge/
door профиль тяжёлых cases 14/15/39 и byte-exact регрессии результата.
Настоящий FPS старого candidate5 тяжёлых сцен не закрывается результатом
более лёгкого frontend LEVEL1. Физические timing/IRQ/cache остаются N24.
