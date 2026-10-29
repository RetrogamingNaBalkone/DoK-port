# N07: протокол независимой проверки renderer

29 октября 2026 года. Это протокол приёмки N07, а не отчёт об уже пройденных
runtime-проверках. Итоговый статус задачи определяет [паспорт](../tasks/N07.md).
Аппаратная проверка обычной DS/DS Lite остаётся в N24 по
[политике проверки](validation-policy.md).

## Источники и границы доказательства

Проверяются реальные полигоны DS 3D engine. Bitmap viewer N06 и совпадение двух
новых реализаций не заменяют исполнение renderer. Чистые стартовые DOS-кадры
N06 служат исходными эталонами видимости, масштаба и положения архитектурных
границ. Полного равенства растров не требуется: DS иначе интерполирует текстуры,
квантует вершины и определяет покрытие пикселей. Свет, вода, отражения и
динамическая палитра остаются явно открытыми до N13.

Исходные maps/определения — полные RU N05 canonical JSON и неизменный пакет N04.
Исходные формулы и near cases — [N06-camera](N06-camera.md),
`research/evidence/nds/n06/height-near-fixtures.json`; все адреса в этом отчёте —
Ghidra NE-loader. Source/runtime row orientation различаются, world maps N05 уже
отражены. Координата центра runtime-клетки: X=480*x+240, Z=480*z+240.

Read-only проверка 144 закреплённых путей N02/N03/N04/N06 прошла до реализации:
`research/evidence/nds/n07/dependency-integrity.json`. Сверены SHA и размеры
артефактов, первичных отчётов и source manifests по принятым compact JSON.
Предыдущие сборки, конвертация пакета и тесты N02–N06 не запускались.

## Матрица камер

Начальные реальные RU-позы: LEVEL1 (15120,−300,22800), LEVEL2
(20880,−300,3120), LEVEL3 (17520,−300,28560), heading90. Для обоих языков
используется один RU-мир. Эти три poses сравниваются с clean DOS N06; отдельные
снимки исходного EN-мира не заменяют RU geometry.

Реальные дополнительные области, найденные независимым чтением N05 maps:

| Уровень | Runtime-клетки | Проверяемое свойство |
|---|---|---|
| LEVEL1 | (40,3) → (40,4) | FCMap space/10 → `+`/400, ceiling −951 → −561 |
| LEVEL2 | (41,2) → (41,3) | FCMap `>`/400 → space/10, ceiling −561 → −951 |
| LEVEL3 | стандартная область и стена рядом | Контроль обычной высоты −951, near и отсутствия щели |

В этих переходах TMap обеих клеток — space; это диагностические poses, а не
доказанные маршруты героя DOS. Во всех ненастенных FCMap трёх реальных карт
floor Y=0; LEVEL3 не содержит низких потолков или FCMap-переходов высоты.
Нельзя объявлять выдуманный уступ пола обязательной особенностью LEVEL3.
Синтетический соседний перепад floor допустим только как отдельно подписанный
контроль алгоритма, если реальная карта его не содержит.

Для первичного поиска нагрузки используется независимый консервативный proxy:
в квадрате 19×19 вокруг кандидата суммируются открытые стороны class2, class4/7
и TMap billboard classes 3/9/10/6. Кандидаты на TMap space:

| Уровень | Клетка | Edge faces | Class4/7 | TMap billboards | Сумма |
|---|---|---:|---:|---:|---:|
| LEVEL1 | (16,21) | 165 | 9 | 66 | 240 |
| LEVEL2 | (30,50) | 265 | 44 | 51 | 360 |
| LEVEL3 | (10,45) | 177 | 4 | 48 | 229 |
| LEVEL3 | (34,10) | 104 | 6 | 116 | 226 |

Proxy не учитывает поворот, frustum, occlusion, clipping или MMap actors. Он
выбирает кандидатов, но не доказывает максимальную GPU-нагрузку. Финальные
тяжёлые ракурсы выбираются по фактическим emitted/clipped vertices/polygons,
кэш misses/uploads и CPU времени. Для каждого уровня нужны неподвижные
камеры и детерминированный проход с поворотом/перемещением, пересекающий границы
кэша и near; повторяющийся стартовый кадр не заменяет подвижный сценарий.

## Наблюдение runtime

Наблюдатель читает ARM9 RAM и кадры, не пишет guest RAM, регистры, PC или
результаты. Writer сначала отдельно записывает volatile odd sequence и flush
этого поля, затем меняет payload и flush всего record, после чего отдельно
публикует even sequence и flush. Snapshot принимается
только при одинаковом чётном sequence до и после чтения. ABI v1 определён в
`ports/nds/platform/render_probe/render_probe.h`: основной scalar probe,
128 независимо seqlocked history records и до 32 реальных GE PosTest records.
Нужны идентификаторы case, frame, уровня,
camera pose, submitted frame и VBlank, чтобы не сравнить метрики нового кадра
со старым изображением.

Минимальные группы наблюдения:

- CPU ticks отдельно для подготовки геометрии, texture I/O/upload и submit;
  источник timer, делитель и частота явно закреплены. Wall-clock host не равен
  времени ARM9 или FPS DS.
- VBlank sequence и VCOUNT на границах работы; completed/submitted frame IDs,
  пропущенные интервалы, минимум/максимум и распределение. Для раннего бюджета
  тяжёлый moving проход должен укладываться в два VBlank после warmup.
- `GFX_CONTROL`/DISP3DCNT bits12/13: color-buffer underflow и polygon/vertex
  overflow. Sticky flags накапливаются до отчёта; их нельзя сбрасывать перед
  наблюдением и терять отказ.
- `GFX_STATUS`/GXSTAT, polygon RAM usage 0x04000604, vertex RAM usage
  0x04000606, программные emitted/clipped counts. Чтение RAM counts выполняется
  в известной фазе до swap, иначе значение может относиться к другому буферу.
- RDLINES минимум/максимум и число валидных samples: низкое значение означает
  меньший запас, ноль samples означает отсутствие измерения. Нельзя выдавать
  неизвестное значение за честный нулевой расход.
- Текстурный working set, occupied/capacity/high-water/evictions/misses/uploads,
  байты и длительность загрузок; VRAM bank mapping и palette consumption.
- N03 static+startup envelope (включая alignment/gap), heap clamp, фактическое
  исключение 655360-byte reserve, managed arena capacities/high-water и SDK
  tail минимум196608. VRAM не прибавляется к main RAM.

Названия и адреса регистров сверены с закреплёнными локальными headers
BlocksDS/libnds (`video.h`, `videoGL.h`). FIFO busy/empty не является длительностью
полного rasterizer. GPU-профиль должен называть непосредственно наблюдаемую
фазу: geometry completion, swap, presentation или line underflow. Если renderer
не имеет отдельного GPU-completion таймера, это ограничение нельзя скрывать
CPU-замером. В N07 допустимы эмуляторные интервалы при явном переносе физических
измерений GPU/DS FPS в N24.

## Независимая проверка геометрии и изображения

1. Проверить surface descriptors против исходных maps/definitions: число и
   тип плоскостей, высоты, открытые стороны, соседние FCMap boundaries, UV и
   происхождение texture ID. Любое слияние хранит доказательство эквивалентного
   покрытия и не пересекает различающиеся material/height/UV boundaries.
2. По независимой рациональной формуле N06 спроецировать выбранные вершины,
   principal point, крайние лучи и clipping intersections. Сверить DS fixed-point
   clip output с заранее заданным допуском, выведенным из GPU precision, а не
   увеличенным после получения расхождений.
3. На диагностическом polygon-ID/flat-color режиме проверить interior coverage,
   occlusion и отсутствие фоновых щелей; пограничная маска шириной до доказанного
   raster uncertainty исключается отдельно. Процент похожих цветов без masks
   не доказывает правильную геометрию.
4. Реальные текстурированные старты сравнить с DOS N06: outlines/landmarks и
   порядок перекрытий отдельно от palette, affine/perspective texture sampling,
   LIGHT и объектов, которые ещё не вошли в N07. Нельзя скрывать отсутствие
   обязательной архитектуры общим большой pixel tolerance.
5. Неподвижные poses повторяются после eviction/reload, движущиеся проходят
   несколько различных кадров. Их hashes и counts подтверждают реальное
   движение, а не только смену подписей нижнего экрана.

Порог производительности и геометрические допуски фиксируются до итогового
прогона. Если heavy scenes превышают два VBlank, возникает overflow, теряется
поверхность или незамкнут RAM budget, N07 остаётся открытой; улучшение
архитектуры не заменяется произвольным уменьшением мира или удалением эффектов.

## Реализованный наблюдатель и найденные ограничения

`ports/nds/tools/run_renderer_emulator.py` использует неизменный N02 Frontend
только как libretro transport. В FAT размещаются точный N04 `assets.ndp` и
неизменный `scene.dat`; ARM9 строит геометрию самостоятельно. SHA fixture
проверяется после core load отдельно от последующего runtime/unload: вставка
ROM самим homebrew core при загрузке не приписывается игре. Все 9728 пикселей
начального HUD каждого уровня независимо сверяются с закреплёнными N06 PNG/
RGB555 входами через SHA принятого manifest, без повторного исполнения N06.
В каждом READY все 9728 показанных HUD RGB555-пикселей и полные ARM9 bytes
immutable packet дополнительно сверяются с фактическим выводом и main RAM.

Состояние READY=4 удерживает scene lease и массивы неизменными. Наблюдатель
ждёт восемь стабильных frontend frames, проверяет packet/generation и только
затем сравнивает изображение. Реальная кнопка A переводит к следующему case;
освобождение A поглощается. COMPLETE=2 допустим только после всех cases.
Подвижная последовательность задана immutable packet: i=0…frames−1,
X=x+dx*i, Z=z+dz*i, heading=(heading+dheading*i) mod360, Y неизменен.
Проверяется каждый history pose и наличие разных показанных сцен.

Восемь отрицательных admission scenarios возвращают mask255: signed-v16
overflow, origin вне lattice, отсутствующая texture slot, отрицательный UV,
reserved flags, сверхлимитная геометрия, сверхлимитный preload count и
недопустимый palette bit15. Они выполняются на отдельных копиях descriptors;
изменение leased scene запрещено. Статистика сохраняет sentinel, а последующий
показ/coverage подтверждает сохранение корректного кадра.

GE-only архитектурная проверка прошла: 24 actual PosTest records, шесть пар
одинаковых world endpoints через разные origins с точным равенством XYZW.
Первичная запись — `research/evidence/nds/n07/geometry-gate.json`; это не
приёмка всего renderer. Масштаб 512 world/GPU unit, общий camera anchor по всем
трём осям и origin-linear clip columns устранили предсказанный ±1LSB шов
обычного раздельного projection×modelview. Наблюдатель независимо вычисляет
semantic modelview, фактически загруженную clip matrix и результат GE PosTest.
Nominal near 200/far 49152 после Q12 depth coefficients дают effective
near 200.0068085/far 49850.1818; это измеренное ограничение точности, а не точная
непрерывная far 49152. Дальность original gather проверяется отдельно.

Flat case использует те же vertices, matrices и depth. Цвет непрозрачной
поверхности задаётся независимо проверяемым hash её source_surface_id:
`((id*2654435761u)>>17)&32767`, ноль заменяется единицей. Наблюдатель читает
actual ARM9 frame/cell/quad descriptors (32/20/92 байта), выполняет независимое
homogeneous clipping и строит две маски. Union interior исключает только
двухпиксельный внешний силуэт; внутренние cell boundaries остаются в проверке
чёрных щелей. Точная RGB555-проверка победившего polygon interior исключает
двухпиксельные окрестности **всех** projected edge segments, включая ближайшие
occluders. Увеличения tolerance по результатам изображений нет.

Такой pass нашёл реальную ошибку viewport: SDK bottom-origin `(0,0,255,153)`
давал 38 чёрных строк сверху. Исправление `(0,38,255,191)` проверено отдельно;
исходное доказательство — `viewport-failure.json` и `viewport-before.png`.
После этого все внутренние union pixels сохранили проверку; чёрный фон вне
конечного mesh не считается автоматически доказанным DOS backdrop. Исходные
Floor/Ceiling Color и дальнее заполнение требуют явного продолжения в N13.

Профиль различает загрузку уровня FLAG1, реальный presented render FLAG8 и
интерактивную console/flush diagnostic phase FLAG32 без PRESENTED. Время
последней сохраняется отдельными records/ticks, не исчезает из истории.
После загрузки/READY перед каждым ACTIVE выполняется ровно одна отдельная
SYNC64 без PRESENTED, geometry, upload или UI work. Полное ожидание ближайшего
VBlank сохраняется в её total_ticks. Первый render начинается с counter и
timestamp завершения SYNC; history/publish после SYNC входит в его бюджет.
Оба sampling callsites используют один noinline `wait_presentation_boundary`:
ожидание SWI и чтение timer. Render sample предшествует layer-enable;
его tail входит в следующий interval, а tail последнего кадра — в начало
DIAGNOSTIC от последнего presentation timestamp/counter. Связь counters
проверяется по RAM, связь timestamp — по закреплённому source до history/publish:
отдельного absolute timestamp в ABI нет. Source review сохранён в
`frame-begin-source-review.json`.
Все реальные render frames, включая первый и любые WARMUP, проходят один
строгий предел двух VBlank; case preparation не выдаётся за движущийся кадр.
Thumb baseline не прошёл этот бюджет. Выбор ARM instructions для трёх render
math modules сохранил все 944 семантических history records и SHA всех 19
финальных верхних кадров; запись `arm-thumb-comparison.json`. Исходный Thumb
report не сохранил промежуточные moving pixel hashes, поэтому их побитовое
равенство не заявляется. Перенос console phase сохранил все 936 active
geometry/camera records и SHA всех 19 верхних кадров; запись
`diagnostic-phase-comparison.json`.

## Итоговый runtime gate

Команда из корня репозитория, CPython 3.14.6:

```powershell
C:/Source/wasdify/.venv/Scripts/python.exe -B -X utf8 ports/nds/tools/run_renderer_emulator.py --core C:/Users/dspev/AppData/Local/Programs/melonDS-DS/1.4.0/melondsds_libretro-win32-x86_64-Release/cores/melondsds_libretro.dll
```

Итоговый seqlock/SYNC прогон завершился exit 0, `passed:true`: 19 immutable cases,
2614 frontend frames, 982 сохранённых records. Все 936 actual render frames,
включая 488 moving и первые кадры, уложились в два VBlank; максимум
1120380 ticks при 560190 ticks/VBlank. Отдельно сохранены восемь level-loading
records (максимум 56636685 ticks), 19 diagnostic records (максимум 172339,
сумма 3066860 ticks) и 19 SYNC (максимум 559720, сумма 9870960 ticks).
Эти задержки не выдаются за скорость живой игры. Все 19 связей
SYNC.end→first-render.start проверены по counters; первый render не исключён
из бюджета. Исторические два first-frame превышения после загрузки при почти
неизменном CPU work сохраняются в `experiments/seqlock-phase-failure/`, а не
скрыты в warmup. Полные before/after records — `frame-boundary-comparison.json`.
Итоговое сравнение `sync-presentation-comparison.json` подтвердило неизменность
всех 936 семантических camera/geometry/material records и SHA всех 19 верхних
кадров после исправления presentation protocol. Timing equality и равенство
промежуточных moving pixel hashes не заявляются.

Восемь flat passes проверили 286544 union interior pixels на чёрные щели и
161212 guarded interiors на точное RGB555. RDLINES min=max 46 на 936 samples;
sticky underflow/overflow/matrix errors равны нулю. Максимум submitted quads
282, actual hardware vertices 1122. Все 24 actual GE tests и шесть shared
endpoint pairs прошли; negative mask 255, HUD/ARM9 packet и runtime FAT
unchanged подтверждены независимо. Верхние кадры трёх starts, реальных
потолочных переходов и heavy endpoints дополнительно визуально просмотрены.

Фактический texture working set уровней: 278528/319488/294912 байт при ceiling
393216; palette по 512 байт при ceiling 16384. Main RAM closure:
static 164740, heap-start gap 32; static+startup 172032, managed 2293760,
SDK growth 548864, reserve 655360;
сумма 3670016. Heap-start gap 32 учтён. Это расход текущего renderer probe,
не оценка полной игры.

Полный независимый runtime report, все current case PNG, coverage descriptors,
expected PNG, native log и точные inputs сохранены в
`research/evidence/nds/n07/emulator/`; `archive-manifest.json` закрепляет
85 файлов, 33 source pins и SHA использованного ROM/ELF/map. Заключительный
runtime выполнен после terminal host/original-data/SDK gate с точными current
manifest bytes. До старта сохранены `run-input-scene.dat`,
`run-input-scene-manifest.json`, `run-input-build-report.json`,
`run-input-source-manifest.json`, `run-input-build-manifest.json` и
`run-input-host-check.json`. `run-input-binding.json` подтвердил неизменность
всех этих файлов и 33 source pins до/после исполнения.

Контрольные SHA-256 окончательной цепочки:

- ROM: `6815321607c94e1dcc20116819504de62175c94cf2647cdaf9003327ff8f5f04`.
- ELF: `4753c7b3353c1e488c54cbc55f6ace422e9460b7eb246964a4dbd4c0a0e10283`.
- Input manifest: `d7289dd8c6a7d4b8f954a140e4664f10e470efb4b42fb3d30903adf62c467fd0`.
- Runtime result: `14162c4b7dbbe61877a8fd69642dc5d9a32d5356de628b4058a3f6e4ce2b415f`.
- Archive manifest: `db486e150e1490de6a03017616adad2d0455ada78d7bd276ed39a8441967f6ba`.
- Runtime review: `ae250d09b2cf1a0da83a375139b8b00825c43023fedfd9f502fb2343740bd194`.

`research/evidence/nds/n07/runtime-review.json` — проверка runtime/provenance,
не замена независимого code review.
Аппаратные FPS, картридж/loader и полная игра этим gate не доказаны.
