# N21P C21: независимая проверка семи frozen variants и focus

30 октября 2026 года. **Frozen identity/layout PASS, focus report review PASS.**
Полная RU/EN matrix ещё исполнялась при этой проверке; full acceptance не
присваивается. Аудитор не запускал эмулятор, DMA checker или прежние host/native
proofs, не редактировал продукт и постоянные инструменты.

`research/evidence/nds/n21p/resource-audit/C21-variants-focus-review.json`
содержит pinned входы и результаты; `review_c21_variants_focus.py` воспроизводит
read-only hash/ELF/report проверки без запуска guest.

## Frozen identity и layout

Независимо проверены RU, EN, auditRU, frontendRU/EN и fixtureRU/EN:

- 7manifest hashes,14actual ROM/ELF и7link maps совпали с approved targets;
- 452уникальных current source pins совпали между всеми manifest;
- все3164frozen source snapshots совпали с соответствующими source pins;
- 84claims для artifacts/generated inputs/build drivers/ARM7 dependency
  проверены по actual bytes;
- manifest audit/frontend/fixture/fine-profile flags соответствуют variant;
- из actual ELF section tables прочитаны `.text`,`.bss`,`.dtcm`,`.itcm`.
  На всех ITCM29160/32768байт, headroom3608; DTCM data0байт.

Это подтверждает идентичность проверенных сборок и layout; runtime memory,
правильность всех сценариев и performance matrix остаются отдельными gates.
Source/PRE count proof и единственный relocated ARM loop рассмотрены ранее
в неизменённых `C21-review.json`,`C21-frozen-review.json`,`C21-ARM-review.json`.

## Focus — diagnostic evidence, не полная приёмка

C20/C21 focus использует один immutable packet SHA
73a4db8f8383bdf3d49187ef3f481601bb2c8b9cf7b5cdf6749f6a00de22dee2.
Прочитаны existing runtime, cost, profile и DMA reports, hashes их входов
сверены. Capture полный:1066rows,missing[]. `profile.passed=false` и
`acceptance_eligible=false` ожидаемы для cost-only wrapper; не скрываются.
Cost diagnostic complete, underlying runtime/audio PASS, независимые
map/HUD errors отсутствуют.

C21 normal focus:691intervals,437×1VB+254×2VB, ни одного3VB,
minimum cadence29,913049FPS, focus budget PASS. Existing strict DMA gate
относится к тому же profile/runtime hash:49actual map transfers и187ordered
GPU upload frames; checker самостоятельно не перезапускался. Это не полный
HUD/touch/всех камер/обоих языков acceptance.

На frame66/case16/ordinal1:

| Измерение | C20, мс | C21, мс |
|---|---:|---:|
| CPU excluding wait | 31,95798 | 31,91334 |
| Map publish, composite CPU | 0,709316 | 0,709763 |
| VBlank wait | 16,42052 | 0,361103 |

CPU уменьшился всего на0,04464мс; map publication work сохранилась.
Существенный результат — прежний3VB interval стал2VB: подготовка успела
пересечь нужный presentation deadline. Уменьшение wait нельзя описывать как
устранение16мс CPU работы. Перенесённый count после boundary учитывается
в следующем presentation interval; проверка всех последовательных интервалов
и скользящих окон необходима именно из-за этого tail.

Geometry+native disclosure+submit на этом C21 focus frame всё ещё около
20,45мс, при16,716мс на59,826Гц, до map и остальных стадий. Ранее full C17
heavy показывал20,54мс той же суммы. Поэтому focus budget30 и среднее57FPS
на другом live route не означают универсальные60FPS на тяжёлых камерах.
Это measured workload текущего конвейера, не доказанный аппаратный предел DS.

До готовности full RU/EN matrix нельзя объявлять N21P принятой. Если полный
строгий30budget пройдёт, добавлять новые caches/микрооптимизации в эту приёмку
без нового измеренного основания не требуется.
