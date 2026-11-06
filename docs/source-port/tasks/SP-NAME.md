# SP-NAME — пользовательское название OGDoK

## Цель и граница

По запросу пользователя от 6 ноября 2026 года общий SDL2-порт Old Gold/PKR/PKE
называется **OGDoK**. Имя приложения и сборочной цели — `OGDoK`, Windows EXE —
`OGDoK.exe`; Linux-приложение — `OGDoK`. Обновить сборку, упаковку, текущие
инструкции и пути в инструментах проверок. Игровые меню, HUD и титры не менять. Заголовки окна ОС и системной ошибки
используют имя приложения OGDoK.

## Материалы и контракты

Прочитаны plan.md, handoff.md, CMakeLists.txt, build.ps1, package.ps1,
README.md, PLAYTEST.md и инструменты проверок SDL2. Исходное дерево чистое.
Оригинальные EXE не анализируются: это название современного порта,
а не восстановление DOS-поведения; адресов и декомпиляторных выводов нет.

Каталоги `ports/sdl2/`, `build/sdl2/`, `artifacts/sdl2/`, `docs/source-port/`
сохраняются. Внутренние `dok_*`, namespace и каталог пользовательского профиля
не переименовываются; настройки и сейвы сохраняют существующие пути.
Исторические имена файлов и targets в датированных доказательствах остаются
исходными. Текущие команды и автоматически создаваемые копии EXE используют OGDoK.

## Работы

1. Переименовать проект CMake и цель приложения, пути текущих проверок и упаковки.
2. Обновить документацию и правила проекта, записать решение в план и handoff.
3. Собрать Release, выполнить CTest, упаковать и проверить готовый EXE.

## Приёмка

Windows-сборка создаёт `OGDoK.exe`, упаковщик поставляет его с актуальным README;
проверки запускают новое имя. CTest и packaged smoke проходят. В игровых меню и HUD
нет правок; каталоги реализации и профилей прежние. Linux-имя задаётся общей
CMake-целью, реальная Linux-приёмка остаётся SP20.

## Результат

Завершено 6 ноября 2026 года. Исходная ревизия
`aff56d12784a138dba57949f16ad8b1a34358a1f`, рабочее дерево было чистым.
Изменены CMake-проект/цель, текущие EXE-пути в tools, упаковщик, README,
PLAYTEST, документация и правила проекта. Заголовки окна ОС и системной
ошибки используют OGDoK. Обновление существующего пакета удаляет известный
устаревший `dok_port.exe`; контрольные старые пакеты отдельно не переименовываются.

Команды из корня:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File ports/sdl2/tools/build.ps1 -Configuration Release -DirectCommands
powershell -NoProfile -ExecutionPolicy Bypass -File ports/sdl2/tools/package.ps1 -PackageName windows-playtest-ogdok
py -3.14 -X utf8 -B ports/sdl2/tools/check_package.py artifacts/sdl2/windows-playtest-ogdok --hidden
py -3.14 -X utf8 -B ports/sdl2/tools/check_launch_paths.py --app artifacts/sdl2/windows-playtest-ogdok/OGDoK.exe
```

Release/CTest **19/19**, готовый пакет **8/8** (три игры и автопоиск,
GL/software), профили/CLI/portable/перенос **32/32 PASS**. Исходные RES
до/после побайтово неизменны по SHA-256. Синтаксис изменённых Python-инструментов
проверен через ast.parse; CMakeCache/build.ninja содержат проект OGDoK и
`OGDoK.exe`, прежней цели приложения в графе нет. В готовом EXE есть OGDoK
и отсутствует прежняя строка заголовка DoK Source Port.

Пакет: `artifacts/sdl2/windows-playtest-ogdok/`.
SHA-256 EXE: `e1b24f2b10f2492d5684be6565f520a9d370e6da9884d997729cdde6555c80cb`.
Локальные отчёты: `research/evidence/source-port/sp08-sp16-checkpoint/windows-playtest-ogdok/report.json`
и `build/sdl2/checks/launch-paths/report.json`. Последний отчёт привязан к SHA
текущего EXE; временные профили проверка удаляет штатно.

Обычный Ninja завис без дочернего компилятора; принадлежавший запуску процесс
остановлен, штатный DirectCommands завершил сборку и CTest. Предупреждения
при сборке libunwind относятся к общему Zig 0.13.0. Linux/ARM/PortMaster и
аппаратная приёмка не выполнялись; GBA/NDS, оригиналы и их статусы не менялись.
Новая сборка остаётся плейтестом.
Следующий ID основной очереди — SP18; переименование её не закрывает.
