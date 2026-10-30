# N22: игровой цикл и время жизни мира

Работа начата 30 октября 2026 года по поручению N22 → N23. Программная
приёмка ещё выполняется; прохождение и окончательные хеши дополняются после
проверок. Физическая DS/DS Lite и прослушивание остаются в N24.

## Основание восстановления

[Аудит оригиналов](N22-original-flow.md) независимо повторяет оригинальные
x86-функции PKR/PKE из сохранённого Ghidra-экспорта и разбирает LEVEL1–3
непосредственно из исходных RES. Указанные там адреса относятся к NE-loader
Ghidra. Автоматический C не включён в реализацию порта. GBA-код не копировался.

В оригинале уровни независимы: выбор → BINTROn → PLAY → EINTROn → выбор.
Game Over возвращает выбор без EINTRO. ENDTXT относится к выходу. Category1000
сравнивает signed WORD score с category99 total; все шесть исходных карт имеют
total0 и штатный score0, поэтому задача собрать очки не добавляется. Выходы
берутся из FCMap; это не выход по близости к тестовой координате.

## Матрица переходов

| Переход | Мир и игровые часы | Представление и звук |
|---|---|---|
| Выбор → BINTRO | Мир ещё не создан | Полный локализованный текст N21 |
| Последняя BINTRO → PLAY | Новый RU-мир; новый epoch; lives3; исходный inventory | Новый demand/cache, HUD, карта и языковое представление |
| PLAY → пауза → PLAY | Тот же мир, события, deadlines и дробная часть часов | Touch отменяется; held input поглощается до отпускания |
| Смена языка в паузе | Мир, AI, inventory и epoch прежние | Перепривязка только presentation; запись только settings.dat |
| HP0, DEC lives ≠0 | Немедленный retry того же мира | Once death sound; очищается сообщение HUD; число попыток обновляется |
| HP0, DEC lives =0 | Удаление мира; выбор текущего уровня | Звук остановлен; EINTRO не показывается |
| FINISH либо HP<0 | Удаление мира после полного callback dispatch | EINTRO текущего уровня, затем выбор |
| Пауза → главное меню | Удаление мира | Звук и ссылки старого мира очищены |
| Главное меню → ENDTXT → EXIT | Активного мира нет | Полный ENDTXT; экран выключения N21 |
| Перезапуск процесса | Мир не восстанавливается | Читаются только настройки языка |

Отдельных экранов retry/Game Over не добавлено: исходный death helper сразу
восстанавливает героя, а строка startup после его возврата содержит один пробел.
Оригинальные startup debug-флаги принудительного завершения не становятся
производственным игровым вводом NDS.

## Retry: точный объём изменений

`dok_gameflow_check` повторяет signed16 DEC с wrap, в том числе для lives0
и −32768. HP<0 означает FINISH без уменьшения попыток. Конструктор нового
героя задаёт lives3; это не поле RES.

`nds_gameplay_retry` выполняется вне dispatch и collision context. Он очищает
старую клетку MMap, восстанавливает моделируемые члены native30-byte диапазона
hero+26…43 и записывает glyph героя в клетку восстановленной позы. Расчёт клетки
повторяет IDIV → signed LOWWORD → clamp. Admission движения не используется:
оригинал может перезаписать занятую клетку respawn. HP становится max_health.

Восстанавливаются x/y/z, heading, angle_parameter, speed и distance. Native
WORD+36 и DWORD+40 пока не моделируются; им не приписывается восстановление
или влияние на NDS. Flags+1C, turn_step/linear_step, last_turn, movement/turn
accumulators, accel_sign и last_flags находятся вне копируемого диапазона
и сохраняются. Полное копирование `dok_motion_state` не используется.

Сохраняются inventory/charge/keys/weapons/score, все карты и visited, двери,
pickup removals, AI/корпусы, снаряды и их occupancy, scheduler owners/events,
clock fraction/deadlines, last_hero и оставшиеся актёрские поля. Retry не вызывает
level_load, clear, bind, map_load или cache reset. Изменение revision и числа
повторов — диагностика новой реализации, не новое состояние DOS.

Интеграционная проверка выявила отдельный случай: исходный retry безусловно
записывает героя поверх стоящего в spawn врага. Прежний NDS guard чужого
origin затем останавливал настоящий AI callback. Подлинный PKR/PKE callback
с map setter подтвердил последовательность space → собственный glyph врага.
Исходное перекрытие и его последствия восстановлены через узкий допуск:
одна spawn-cell и 32-byte bitmap индексов world seeds для героя и находящихся
там class9 actors. Reference, epoch, cell и lifetime проверяются; остальные
foreign origins отклоняются. Допуск снимается у конкретного actor только при
committed departure, сохраняется при same-cell и отказе движения и не
возвращается при обычном повторном входе.

AI walking/course в таком перекрытии восстанавливают собственный glyph;
dying выполняет исходный corpse transfer. Публичный controlled sight остаётся
read-only и сохраняет карту, внутренний native sight выполняется после
исходной очистки origin. Camera admission не меняет bitmap. Metadata меняется
в exclusive retry boundary без active context; revision публикуется вместе
с завершённой операцией retry. После уничтожения мира bitmap очищается
вместе с gameplay. [Доказательства и focused tests](N22-retry-overlap.md).

FINISH-output sink только устанавливает флаг. После возврата floor-handler
hero callback записывает HP=−1 перед дальнейшими проверками fire/movement.
Фактический переход интерфейса и удаление мира выполняются после завершения
dispatch и предъявления кадра, когда заимствования закончены.

Root QA обнаружила ошибку первой интеграции: после after-render FINISH или
Game Over новый frontend-state публиковался с even sequence ещё до отрисовки
его собственных pixel buffers. Это ошибка нового NDS-кода, а не особенность
DOS. Исправление сначала скрывает 3D, полностью готовит новый OUTRO/LEVELS
и его палитры и ждёт VBlank, затем публикует descriptor. Физический LCD
scanout проверяется отдельно после стабилизации кадра. Промежуточные отчёты
старых ROM не выдаются за приёмку исправленного display-контракта.

## Удаление и новая загрузка

`world_active` отделён от монотонного `world_serial`. Это позволяет удалять
мир на FINISH/Game Over/уходе в главное меню и не вызывать повторно clear
на уже снятых combat/AI tags при завершении текста или новой загрузке.

Перед удалением проверяются отсутствие dispatch/collision context и снятие
scene lease. Audio STOP проходит синхронный ACK ARM7; проверяются отсутствие
playing/preparing, underruns и stale commands. Затем удаляются combat и AI
owners, scheduler сбрасывается с новым epoch. Обнуляются gameplay и sprite list,
borrowed effects links, demand descriptors, frontend visual pointers, pending
HUD message и snapshot карты. Все процессные PCM-банки остаются неизменными;
указатель в mutable world не передаётся ARM7.

Новая загрузка создаёт все состояния заново в заранее выделенных slabs.
Старые tokens/keys/inventory между независимо выбранными мирами не переносятся.
Файлового игрового восстановления, сохранений или autosave нет.

Независимый разбор границ заимствования сохранён в
`research/evidence/nds/n22/root-teardown-review.md`.

## Сборка и наблюдение

`Makefile.n22` наследует принятые N21P GPU fog, frame store, water UV,
быстрый overlay и ITCM-размещение. Production `FRONTEND_FIXTURES=0`;
реальные выходы обрабатываются всегда. Старые N21P targets не перезаписываются.

```powershell
py -3.14 -B -X utf8 ports/nds/tools/flow/audit_original.py
py -3.14 -B -X utf8 ports/nds/tools/flow/check_host.py
py -3.14 -B -X utf8 ports/nds/tools/check_flow.py candidate-partial03-ru
py -3.14 -B -X utf8 ports/nds/tools/check_flow.py candidate-partial03-en
```

Сборщик закрепляет shared BlocksDS/Wonderful packages по toolchain_versions.json,
входы, generated data и ARM7 dependency. Build-manifests находятся в target.
Следующий candidate label создаётся отдельно; существующие immutable targets
не перезаписываются. Проверенные candidate01 и их exact compiled-input snapshots
перечислены в паспорте. Production default включает partial-defrag; старые
frontend targets являются controls с flag0.

Текущий измеренный RAM closure production: startup495616 + managed2293760
+ SDK225280 + reserve655360 =3670016 bytes. SDK minimum196608 соблюдён,
запас над минимумом28672 B; reserve не расходуется. Новая overlap metadata
занимает36 B внутри world slab. Физические high-water stacks и аппаратная
совместимость остаются N24; текущие числа получены в DS-mode эмуляторе.

`g_nds_flow_probe` — только диагностический read-only снимок32 DWORD с seqlock:
magic22504B44/version1/sequence; active/serial/level; HP/lives; x/y/z/heading;
pending FINISH/deaths/retries; callbacks hero/AI/combat; event/object counts;
указатели MMap/TMap/FCMap/visited только активного мира; keys/charge; last flow
result; dispatch/collision-active; scheduler epoch; presentation/frontend frames.
Ни команды, ни инъекции ожидаемого результата в ABI нет.

## Прохождение и ограничения

На candidate-partial01 RU/EN root отдельно прошёл обычными кнопками две смерти
с немедленным retry и третью смерть с Game Over в каждом языке. Снимки
подтвердили точную исходную позу, прежний epoch, живые события до Game Over
и пустые события/ссылки с остановленным звуком после него. HP и поза не
инъецировались. Полные отчёты — `build/nds/n22/root-natural-death-{1,2}-partial01/result.json`;
точные версии driver/импортов закреплены в `sources/` каждого прогона.

Host: 655 929 substantive checks с полным signed16 lives domain и сравнением
полных gameplay/world/scheduler/event-bank на retry. Регрессия production
audio loader: по 9 097 511 checks и 9 011 200 PCM bytes на язык; обе Linux
static cross-сборки созданы, их исполнение не заявляется. Эти проверки не
заменяют ARM7 timing или субъективное прослушивание.

Focused occupied-respawn: 160 checks, 40 настоящих AI callbacks, герой и два
врага в одной клетке, повторный retry, обе corpse-ветви, hero/camera/direct
movement и отрицательные проверки cell/ref/generation/epoch. Регрессия:
шесть host suites без failures, 61 824 original collision queries и 1 152
wrappers на шести картах; 2 060 свежих C callback rows совпали с PKR/PKE.
Старые N10/N17 отчёты сохранены; новые находятся в
`research/evidence/nds/n22/regression/`.

Те же focused checks исполнены отдельной ARM9 fixture: 160 checks и 40
настоящих AI callbacks, два retry и две corpse-ветви, faults0; melonDS DS
1.4.0 в DS mode, результат на 709-м video frame, FAT не изменён. Probe
доступен только для чтения; production не содержит команд этой fixture.
Память collision state на ARM9 — 1 340 bytes, bitmap — 32 bytes.
190 source/archive SHA checks прошли. [Компактный результат](../../../analysis/nds-n22-overlap-arm9.json),
полные доказательства — `research/evidence/nds/n22/overlap-arm9/`.

[Маршруты](N22-routes.md) и их машинные протоколы должны отличать найденный
план от фактического прохождения. Допустимы реальные joypad/touch callbacks
с read-only наблюдением позы. Teleport, выдача ключей/HP и отключение AI/урона
не доказывают обычное прохождение. Host и branch fixtures дополняют его,
но не заменяют. Автоматический маршрут не заменяет человеческий плейтест N24.

Первый принятый обычный проход — LEVEL1 RU на историческом control e7428fc1…:
V18 содержит9 285 presentations,632 достигнутые клетки с детурами/повторами,
девять реальных touch Center, все три ключа и два retry, затем настоящий
FINISH → EINTRO → LEVELS. V17 независимо завершил тот же уровень. Остальные
уровни и локали проверяются. В L2 промежуточного ROM ab7a6040… найден реальный
отказ между submit и presentation; отдельное восстановление точного старого
ROM и повтор его inputs сохраняют этот случай для диагностики.
[Частичное перемещение VRAM](N22-texture-stream.md) устранило подтверждённую
причину этого отказа в actual SDK paired fixture, сохранив pixels и DMA guards.
Полная ordinary матрица candidate01 остаётся отдельным открытым критерием.
