# N21: frontend Nintendo DS

Реализация принята программно на эмуляторе29 октября 2026 года. Источники и
различия DOS/NDS описаны в [N21-evidence.md](N21-evidence.md), portable
контроллер и формат настроек — в [N21-state-settings.md](N21-state-settings.md).

## Компоновка и исходные ресурсы

Верхний экран256×192 показывает полную исходную картинку с принятой N06
коррекцией DOS320×200 к4:3. Встроенные титульные надписи/копирайт не обрезаны.
Для TITLE и README выбран исходный титул; BINTRO/EINTRO используют BSPRn/ESPRn
и GAMEPn. Главное меню, пауза и ENDTXT используют исходный FON46×46 с повторением.
README сверху с титулом и ENDTXT с FON — явная NDS-компоновка, а не поведение
исходного DOS framebuffer.

Нижний экран — отдельная непрозрачная текстовая панель. В меню и управлении
используется неизменённый FONT8, в полном тексте — FONT16. Текстовое окно
x8/y24,30 колонок×9 строк соответствует принятому N06. Заголовок FONT8
x8/y8, навигация x8/y176. Страницы сохраняют пробелы и полный порядок исходных
CP866 байтов; CR не рисуется, LF переводит строку, NUL/$ завершают текст.
Короткие слова переносятся целиком, длинные делятся только по границе строки.

| Текст | RU страниц | EN страниц |
|---|---:|---:|
| README | 3 | 3 |
| BINTRO1 / EINTRO1 | 2 / 1 | 3 / 1 |
| BINTRO2 / EINTRO2 | 2 / 1 | 2 / 1 |
| BINTRO3 / EINTRO3 | 2 / 2 | 3 / 2 |
| ENDTXT | 3 | 3 |
| Всего | 16 | 18 |

Меню: заголовок x8/y8; фокусный прямоугольник x8/y28+24n,240×20,
маркер `>` x16/y32+24n и текст x32/y32+24n. Пять пунктов главного меню,
четыре паузы и три именованных уровня не перекрываются. Все кнопки имеют
управление крестовиной/A/B; touch используется картой и учитывается при
поглощении переходов.

Чёрный фон имеет исходный menu index0; весь нижний текст — исходный яркий
index2, фокус — index2 с чёрными буквами. Исходная палитра не изменяется.
Изначально проверенный index86 дал контраст1.852:1 и отвергнут; index2 даёт
15.304:1. Цвет меню является адаптацией NDS, не изменением игровых палитр.

## Время, память и ввод

Production entry включён `GAMEPLAY_FRONTEND=1`; принятый N20 diagnostic
SCN-путь остаётся отдельным compile path. Production не требует scene.dat
и не проигрывает диагностический каталог23 звуков. TITLE запускает110 once,
выход из TITLE останавливает его; вход в README запускает110 once повторно,
выход из README останавливает. Бесконечной музыки меню нет.

VBlank IRQ запоминает ранние A/Start с boot и фронты A/B/Start/Up/Down.
Основной controller атомарно забирает фронты; подтверждение не теряется при
чтении картинок, Start не зависит от длительности тяжёлого игрового кадра.
Только frontend меняет pause; N20 map consumer получает Start с замаскированным
битом. Пауза очищает controls и отменяет touch capture. До отпускания всех
кнопок и стилуса новый экран/игра не принимает удержанный ввод.

Shared scratch131072 bytes переиспользуется только вне игровой подготовки:
top INDEX8[49152], bottom INDEX8[49152], текст[32768]. Runtime/controller/
каталог/две палитры занимают свободный I/O промежуток61440…65535; static
assert требует размер≤4096. Каталог N04 после открытия session не заимствуется.
Map mirror65536…114687 и UI bank после114688 не перекрываются. Резерв655360
не расходуется, дополнительных кадровых буферов/heap slabs нет.

Перед изменением shared scratch, draw или языковой bind frontend probe
становится odd; even публикуется только после целого кадра. В PLAY указатели
frontend-пикселей не являются действующими snapshots. При возврате в игру
восстанавливаются Main/Sub palettes, видимость3D, dirty HUD и полная lower
map при следующей презентации. Время чтения меню исключается из игрового
интервала FPS; clock остаётся на паузе.

Язык меняет и render-only POPAL. Исходные111 графических пар проверены:
110 идентичны, у последнего кадра POPAL отличаются ровно три пикселя.
Оба исходных варианта проходят N04 SHA/schema validation и хранятся в packed
store текущего уровня. Дописывается только недостающий вариант до разбиения
того же asset slab на packed/raw partitions; raw cache остаётся≥196608 bytes.
Simulation bank, animation IDs, мир, события и stats не перепривязываются.
Только render demand последнего POPAL использует N04 ID выбранного языка.
Подробный контракт и1049583 host checks — [N21-visual.md](N21-visual.md).

## Границы N21

Production: выбор→BINTRO→PLAY, pause/resume/язык/управление/главное меню,
выход→ENDTXT→экран выключения. Реальные FINISH/death и повтор мира — N22.
Для проверки полного EINTRO до N22 `FRONTEND_FIXTURES=1` добавляет явные
Select+Start→OUTRO и Select+X→ENDTXT; chord имеет приоритет перед обычным
Start. OUTRO возвращает к выбору уровня. Production default0 не содержит
действующих диагностических chords. Эти fixtures не объявляются кампанией.
Select+Y отдельно переключает display-only pose последнего кадра POPAL перед
камерой. Он не является игровым actor, не участвует в disclosure/collision,
не двигает героя и не меняет stats/time. После формирования конечного
stream plan сохраняется копия фактически выбранного texture descriptor и
число ссылающихся на него quads; независимый observer проверяет все32768
байт actual decoded cache и выбранный языковой ID. На период compaction/
decode опубликованный borrowed pointer становится недоступен через odd
seqlock; во время предшествующей тяжёлой геометрии PLAY descriptor доступен.

Настройки читаются/пишутся только как `fat:/dok/settings.dat`. Отсутствие или
повреждение дают RU; запись недоступна — выбранный язык остаётся в RAM.
Никакого игрового прогресса, saves/autosave или импорта DOS/GBA нет.

## Воспроизведение и приёмка

Общие установки: Python3.14.6, Unicorn2.1.4, Zig0.13.0; Ghidra12.1.2
и JDK21.0.12.1+1; BlocksDS1.24.0, GCC16.2.0, Binutils2.47. Полный набор
пакетов проверяется против `ports/nds/toolchain_versions.json` при сборке.
Дополнительные toolchain-копии в проект не добавлялись.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File ports/nds/tools/frontend/export_evidence.ps1
py -3.14 -B -X utf8 ports/nds/tools/frontend/extract_frontend.py
py -3.14 -B -X utf8 ports/nds/tools/frontend/export_labels.py
py -3.14 -B -X utf8 ports/nds/tools/frontend/audit_original.py
py -3.14 -B -X utf8 ports/nds/tools/frontend/audit_visual_assets.py
py -3.14 -B -X utf8 ports/nds/tools/check_frontend_host.py
py -3.14 -B -X utf8 ports/nds/tools/check_frontend_visual.py
foreach ($taskBuild in 'fixture-visual-ru','fixture-visual-en','frontend-visual-ru','frontend-visual-en') {
    py -3.14 -B -X utf8 ports/nds/tools/check_frontend.py $taskBuild
}
```

ARM7 ELF наследуется от N18 и закреплён отдельным hash. `Makefile.n21`
использует `GAMEPLAY_FRAME_STORE=1`, `GAMEPLAY_FAST_OVERLAY=1`,
`GAMEPLAY_WATER_UV=1`, GPU fog и исходные часы. Production builds имеют
`FRONTEND_FIXTURES=0`, fixture builds1; compile sprite bank выбирается RU/EN
независимо от settings/defaultRU. Все compiler/generated inputs и драйверы
в `build/nds/n21/<target>/build-manifest.json`.

Runtime использует melonDS DS libretro1.4.0 в DS mode, обычный RAM-лимит,
настоящие joypad/touch callbacks и read-only наблюдение RAM/VRAM. Core DLL:
`C:/Users/dspev/AppData/Local/Programs/melonDS-DS/1.4.0/melondsds_libretro-win32-x86_64-Release/cores/melondsds_libretro.dll`,
SHA-256 `c7e6b58c667075a2a9868f2c3ba7a98e24200410a6da3051a7c555f1f0eef8ef`.
Settings write/restart работают с homebrew read-only=`disabled`; отдельный
режим `readonly` включает реальный read-only core и проверяет отказ записи.

```powershell
foreach ($taskLang in 1,2) {
    $taskSuffix = if ($taskLang -eq 1) { 'ru' } else { 'en' }
    $taskFixture = "build/nds/n21/fixture-visual-$taskSuffix"
    $taskProduction = "build/nds/n21/frontend-visual-$taskSuffix"
    py -3.14 -B -X utf8 ports/nds/tools/run_frontend_emulator.py --target $taskFixture --language $taskLang --mode full
    foreach ($taskButton in 'A','Start') {
        foreach ($taskPhase in 0..7) {
            py -3.14 -B -X utf8 ports/nds/tools/run_frontend_emulator.py --target $taskFixture --language $taskLang --mode early --button $taskButton --phase $taskPhase
        }
    }
    py -3.14 -B -X utf8 ports/nds/tools/run_frontend_emulator.py --target $taskProduction --language $taskLang --mode world-smoke
    py -3.14 -B -X utf8 ports/nds/tools/run_frontend_restore_emulator.py --target $taskProduction --language $taskLang
}
py -3.14 -B -X utf8 ports/nds/tools/run_frontend_emulator.py --target build/nds/n21/frontend-visual-ru --mode settings
py -3.14 -B -X utf8 ports/nds/tools/run_frontend_emulator.py --target build/nds/n21/frontend-visual-ru --mode restart --reuse-image 'build/nds/n21/frontend-visual-ru/frontend-settings/save/melonDS DS/dldi_sd_card.bin'
py -3.14 -B -X utf8 ports/nds/tools/run_frontend_emulator.py --target build/nds/n21/frontend-visual-ru --mode corrupt
py -3.14 -B -X utf8 ports/nds/tools/run_frontend_emulator.py --target build/nds/n21/frontend-visual-ru --mode readonly
py -3.14 -B -X utf8 ports/nds/tools/archive_frontend.py
py -3.14 -B -X utf8 ports/nds/tools/summarize_frontend.py
```

Archive/summary требуют явные root-acceptance/visual-review manifests и
отказываются принимать неполные либо не совпавшие по source/ROM/oracle hashes
результаты. Отчёты summary создаются только для N21; сторонние GBA/SDL2
summaries не переписываются. Принятые старые source pins сохраняются через
`supersession.json`, без глобального отключения проверки целостности.

Итог:142931 controller/settings и1049583 visual host checks;103/105 frontend
states,34 полные оригинальные страницы,20447232 INDEX8 и столько же actual
RGB pixels в полном RU/EN gate. Четыре полных PCM110 once captures покрывают
474361 исходных samples каждый, коэффициенты≈0.99243, audio faults0.
Все32 одно-VBlank ранних подтверждения прошли; две production сборки прошли
отдельный smoke; четыре настоящих settings/FAT случая прошли. Restore gate
на12 combinations проверил233472 HUD и1179648 map pixels отдельно от нового
frontend painter; карта сверена с native x86 original raster.

Финальная память: startup524288 + managed2293760 + SDK196608 + reserve655360
=3670016 bytes. SDK находится **ровно на минимуме**, дальнейшие код/данные
должны сначала освободить бюджет. Общий FPS budget остаётся N21P; физические
DS/DSLite, аппаратные IRQ/DMA/touch/stack и субъективное прослушивание — N24.
Реальные FINISH/death/retry N22 ещё не приняты; игровых saves нет.
