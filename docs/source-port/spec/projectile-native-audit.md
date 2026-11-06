# SP11: независимая проверка контакта и жизненного цикла снарядов

Дата: 6 ноября 2026. Образцы: исходные OG.EXE/OLD_GOLD.RES, PKR.EXE/PKR.RES,
PKE.EXE/PKE.RES из реестра SP01. Все адреса ниже — **Ghidra MZ/NE**, не файловые
смещения, DPMI-селекторы или адреса вставок WASDify. SHA-256 до/после находятся
в `research/evidence/source-port/sp11-native-complete/report.json` и manifests Ghidra.

## Воспроизведение

Нужны общие Zig 0.13.0, Ghidra 12.1.2/JDK21, Python 3.14 с Unicorn и собранные
`dok_core`, `dok_inspect`. Новый CMake target `dok_projectile_native_probe` собирает
только стенд. При уже собранном core эквивалентная команда:

```powershell
$env:ZIG_GLOBAL_CACHE_DIR='C:/Source/DoK-port/build/sdl2/zig-cache'
& "$env:LOCALAPPDATA/Programs/Zig/0.13.0/zig.exe" c++ -target x86_64-windows-gnu -std=c++17 -O2 -I ports/sdl2/include ports/sdl2/tests/projectile_native_probe.cpp build/sdl2/windows/libdok_core.a -o build/sdl2/windows/dok_projectile_native_probe.exe
py -3.14 -X utf8 -B ports/sdl2/tools/export_projectile_audit.py
py -3.14 -X utf8 -B ports/sdl2/tools/check_projectile_native.py
py -3.14 -X utf8 -B ports/sdl2/tools/check_projectile_reclaim_inventory.py
py -3.14 -X utf8 -B ports/sdl2/tools/trace_projectile_sound.py
```

Экспортёр использует отдельный `ExportSourcePortProjectiles.java`, read-only проекты,
проверяет SHA образцов и сохраняет scriptlog. Стенд исполняет исходные x86 инструкции
через настоящие vtable; неожиданный callee вызывает отказ. Количество строк C++
строго сверяется с числом входных векторов. Проверка двух новых моделей не используется.

## Матрица ветвей

| Прежний пробел | Реально исполняемое исходное поведение | Входы и сравнение |
|---|---|---|
| Contact/class-hit | Flight callbacks PKR `1068:26e8`, PKE `1020:2d8b`, OG `1ae1:22fa`; оригинальные фильтры через vtable, player stat и monster damage/wake | Каждый projectile × каждое определение всех 7 LEVEL × source class 1/9; классы0,1,2,3,4,6,7,8,9,10,12,13; HP/урон signed16 границы, monster state0–4, door flags и state 1/2 |
| Impact height | Те же callbacks и world movement, без подмены результата попадания | Все представленные классы, высота −32768/−1/0/1/200/32767; направления0/44/90/180/270/359, elapsed0/416/832/3328 |
| Effect lifecycle | Настоящий animation-next PKR `1050:0055`, PKE `1018:0f63`, OG `1aa1:0055`, исходные metadata всех реально используемых клипов | Contact→каждый кадр→cancel, каждое допустимое начальное положение effect cursor, дополнительные callbacks после конца |
| Dynamic reclaim/reuse | World free slot PKR `1098:017d`, PKE `1030:01ea`, OG `21b8:01d4`; OG helper `2000:4745`; настоящий class 8 destructor | Saturated pool, NULL-кандидат, flags0400-кандидат каждого неопределённого glyph, полный пул; отдельно настоящий effect completion→0400/сброс1000→allocator→destructor→повторный spawn |
| Static reclaim | Исходные definitions всех 7 LEVEL и actor-module writers flags0400 | Ни одного static definition с 0400. Единственные OR0400 writers: PKR `1068:2849`, PKE `1020:2eec`, OG `1ae1:23f9`, в завершении динамического снаряда; вызываются предыдущей строкой |

Для contact/effect сравниваются alive, x/y/z, signed damage, raw animation cursor,
impact/effect/actor-hit, занятость клетки, target health/state, число и интервал событий,
**хеш действительно показываемого изображения**, а не только индекс похожего кадра.
Allocator сравнивает выбранный slot и число активных снарядов. Реальные классы5 и11
в этих семи исходных уровнях отсутствуют; class11 не имеет создаваемого типа.
Искусственное превращение static definition в0400 не является достижимым состоянием
этих оригинальных карт. Защита статических definition-slots порта остаётся принятой
адаптацией; динамические уничтожение и повторное использование проверены исполнением.

## Обнаруженные и исправленные расхождения

Достоверный baseline OG дал **39 расхождений из 2472** векторов; прежний probe и код
сохранены рядом с `before-og.json`. Исправления в `projectiles.cpp`:

- OG door filter проверяет state 2 (`1ae1:2eea`), а не flags 8, который применим к Kremlin.
- OG contact записывает cursor 1 (`1ae1:2616`), сохраняя показываемую flight image 0;
  следующий исходный animation-next увеличивает cursor до 2. Прежний порт показывал
  effect 1 и удерживал эффект лишний callback. Восстановление уже показанного effect 1
  безопасно продолжает с2. Совместимость публичных SP17 saves rules1→2 описана в SP17.

После исправлений baseline 2472/2472 PASS. Финальный полный прогон: **27 917/27 917 PASS**,
7 LEVEL, EXE/RES SHA-256 до/после совпадают. PKR и PKE: LEVEL1 по 3291, LEVEL2/3
по 4508; OG LEVEL4 — 3303. Групповые счётчики: 6494 all-resource contact, 6456 angle/elapsed,
1614 height, 5250 signed class-hit, 350 door filters,460 contact-through-release,
912 effect tails,6331 allocation cases,50 фактических completion/reclaim/respawn.
Полные входы, результаты и служебные вызовы находятся в `report.json` и LEVEL JSON.
Повторный прежний `dok_projectile_probe` после исправления также PASS 54/54. Новые golden результаты не заменяют native oracle.

## Границы стенда и звук

Входные карты, decoded clips/изображения, проверенная DOS trig table и начальные тела
объектов задаются явно. Clock start/stop/read управляются (`check_movement_oracle.LAYOUT`).
Исходные scheduler API, heap_free/owner_destroy и HUD presentation контролируются;
их реальные ABI/число вызовов и интервалы сохраняются. Это не проверка heap runtime,
диспетчера или звукового устройства; их отдельные проверки относятся к SP09/SP13.
Все адреса controlled boundaries перечислены в `check_projectile_native.BOUNDARIES`;
HUD: PKR `1d0c7/11f95`, PKE `14957/11bf8`, OG `1fd2b/1fd42/1fd68/1fd7f`
(линейные real-mode адреса). ENG `1090:028b` читает heap metadata для счётчика освобождённых
байтов SS:0010; read-only Ghidra `heap-size.asm` доказывает границу accounting.

Sound wrappers здесь записывают requests, исходная проверка isplaying получает idle.
Входные HP100/99/95/7/0 проверяют OG health%5 gate, включая priority_once (AudioAction4);
этот запрос нормализуется SP13 через native get/same-id gate, а не raw once count.
225 typed traces всех 7 LEVEL (`sound-traces.json`) содержат original contact/hit/cancel
requests и реальные портовые AudioCommands с resource/owner/binding/ID. Файл передан
в SP13 для реального native sound wrapper/admission/driver replay; эти audio columns
**не включены** в PASS сравнения 14 gameplay полей и pixel hash.

Программные пробелы contact/class-hit/height/effect/reclaim для исходных семи уровней
закрыты в описанной области. Ручная оценка боевых ощущений, физические тайминги ввода
и визуальная приёмка доставленного пакета остаются в SP18.

## Повтор после изменений SP12/SP13 — 6 ноября 2026

Native матрица повторена после SP12 movement fix: **27 917/27 917 PASS**, SHA оригиналов
совпали. Прогон также включает damage_player helper SP13; последующее уточнение только
звуковой ветви OG health%5 не меняло сравниваемые числовые поля; аудиокоманды исключены
из числового сравнения.
225 typed sound traces затем заново сняты отдельным перелинкованным стендом с окончательным
core SP13 от 6 ноября 00:16:52: 269 once, 50 stop, 6 stop_all, 6 priority_once.
Их actual native backend replay проверяется SP13. Числовая матрица не запускалась ещё раз
из-за изменения исключительно аудиокоманд. Точные scopes и SHA результатов/исходников/
финального probe: `research/evidence/source-port/sp11-native-complete/post-integration-report.json`.
