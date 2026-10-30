# N22: read-only AI advisory из существующего candidate01

Отдельное исследование для бота обычного прохождения. Production ROM,
события, характеристики, allocator и набор probes не менялись. Observer
читает существующий `storage` pointer и типы из DWARF своего ELF; принятый
SDK fragmentation gate не пересобирался.

## Точный ABI проверенного ELF

Образец: `build/nds/n22/candidate-partial01-ru/dok_nds_gameplay.elf`, SHA-256
`06f2151482206baefac04ebeff855b6c2c7b880db0e9d31cf24c4240d5635943`.
Адреса — ARM9 main RAM. Смещения получены из DWARF самого ELF,
не вычислены через Windows host `sizeof` и не относятся к native DOS layout.

```powershell
py -3.14 -B -X utf8 ports/nds/tools/flow/inspect_ai_layout.py
```

Команда запускает общий `arm-none-eabi-readelf.exe --debug-dump=info`,
разрешает ссылки DWARF типов и сохраняет
`research/evidence/nds/n22/candidate01-ai-layout.json`. Для другого ELF
использовать `--elf <path> --output <json>` и передать новый JSON reader.
По умолчанию reader откажется читать ELF с другим SHA. Полный временный
14-МиБ readelf dump удалён; воспроизводимый ELF и27-КиБ
`research/evidence/nds/n22/ai-readonly/dwarf-excerpts.txt` сохранены.

Для EN candidate01 также извлечён отдельный DWARF layout:
`candidate01-en-ai-layout.json`, ELF SHA
`5fddcabe2ae3f7378ddb8e494b818e9132cd6247fcbd14eabfc9f94ab1db6753`.
Его `storage` находится в`0203FE2C`; структурные смещения ниже совпали с RU.
Конструктор `LiveAiReader` выбирает только единственное точное совпадение
ELF SHA среди RU/EN JSON, не угадывает ABI по имени файла или языка.
Неизвестная сборка явно отвергается. Прежний RU-only reader для уже начатых
frozen запусков сохранён в`ai-readonly/read_live_ai-ru-only.py`.

| Объект | Адрес или смещение |
|---|---|
| `storage`, pointer32 | `0203FE6C` |
| `SpriteWorld.game` | `*storage + 352960` |
| `NdsGameplay.ai` | `game + 4` |
| `dok_ai.enemies` | `ai + 36`, stride72 |
| `dok_ai.count/callbacks` | `ai + 9800 / 9808`, uint32 |
| `dok_ai.world/epoch` | `ai + 0` pointer32 / `ai + 9796` uint32 |
| Enemy x/y/z | signed32 `+4/+8/+12` |
| Enemy heading/angle_parameter | signed16 `+36/+38` |
| Enemy health | signed16 `+56` |
| Enemy glyph/state/corpse | uint8 `+66/+67/+69` |
| Enemy last_motion_ticks | uint32 `+48` |

Reference берётся по glyph через `world.glyph_index[glyph]` и соответствующий
`world.objects[]`. Reader проверяет полный epoch/object_id/generation,
class9, definition index/glyph и совпадение AI-позы с object seed.
Возвращаются также height_parameter, upper_y/lower_y, static definition flags,
burst_started и совпадение позиции с переданной опубликованной MMap.
Отдельный флаг «летит» из этих полей не выдумывается.

## Подключение к observer

```python
from read_live_ai import LiveAiReader

reader = LiveAiReader(target / 'dok_nds_gameplay.elf')
sample = reader.sample(
    lambda address, size: render.ram(observer, address, size),
    mmap=published_mmap,
)
if sample['accepted']:
    by_glyph = {enemy['glyph']: enemy for enemy in sample['enemies']}
```

Это только чтения model RAM. Нет host RAM writes, исполнения target-команд,
инъекций cache flush, перемещения героя или изменения HP. При отказе
`accepted=False`, `reason` объясняет причину; вызывающий код продолжает
обычный ввод, использует прежний снимок с указанием возраста либо показывает
неизвестное состояние. Для HP/state advisory важнее достоверно показать
прогресс попаданий, чем заменить MMap-клетку точной координатой спрайта.

Перед выдачей snapshot проверяются:

- Согласованный live flow seqlock и отсутствие dispatch/collision context.
- Реальные AI busy/fault и scheduler dispatch/in-callback поля равны нулю.
- Epoch и AI callback counter совпадают с опубликованным flow.
- Private committed hero pose совпадает с опубликованной flow pose.
- Границы DS main RAM, count≤128, world count≤256, glyph indices,
  actor references, definitions и совпадение AI/object-seed poses.
- Повторное чтение flow полностью совпадает с исходным.

Это проверки согласованности состояния эмулятора, не доказательство
физической ARM9 D-cache coherence. Production не выполняет полный flush
private AI pool каждый кадр; такие гарантии нельзя вывести из чтения RAM.
Кроме того, опубликованная map snapshot может относиться к предыдущей
перерисовке. `occupancy_match` позволяет отдельно отфильтровать соответствие
AI позы этой карте. Никакие ограничения не снимаются записью в cache state.

## Фактическая проверка

```powershell
py -3.14 -B -X utf8 ports/nds/tools/flow/smoke_live_ai.py
py -3.14 -B -X utf8 ports/nds/tools/flow/smoke_live_ai.py --language 2
```

Fresh process загрузил неизменённую candidate01 ROM, прошёл меню настоящими
libretro кнопками и взял80 samples, включая обычное движение вперёд.
Приняты43 согласованных snapshots, содержавших60 размещённых class9 actors.
Отвергнуты28 samples с ещё не опубликованной hero pose и9 с несовпавшим
AI callback counter. Принятые snapshots покрывают AI callbacks1→6;
в последнем все60 поз совпали с опубликованными MMap cells.
Это boot/read-only smoke, не прохождение уровня.

Оба RU/EN процесса повторили этот результат по отдельности:43/80 принятых
snapshots и те же37 мотивированных отказов. Каждый использовал свой ELF,
свой `storage` pointer и выбранный по SHA JSON. EN не использует RU address
как fallback; полный отчёт EN — `ai-readonly/smoke-en.json`.

Для glyph239 получены HP24, y−250, height_parameter250, reference
`[epoch1, object_id65, generation1]` и начальная клетка9,11. Это исходное
положение smoke-сессии, не координаты позднего пользовательского choke.
Полный отчёт: `research/evidence/nds/n22/ai-readonly/smoke.json`.

В текущем `core/combat.c:contact` class9 попадание определяется MMap клеткой
и `dok_ai_hit`; y и height не проверяются перед нанесением урона.
Height используется затем для позиции impact effect. Поэтому большая
высота bat сама по себе не подтверждает отсутствие попаданий. V damage4
после decay может стать2; HP24 требует6–12 успешных попаданий и, если враг
спит, отдельного первого wake-hit без вычитания HP. Число выстрелов и число
попаданий различаются; readonly health advisory помогает их различить.
