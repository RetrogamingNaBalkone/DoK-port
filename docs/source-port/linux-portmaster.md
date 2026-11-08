# OGDoK — Linux x86_64 и PortMaster

Работа SP20-01…08, 8 ноября 2026. Windows принята пользователем на двух ПК.
Публикация и другие платформы вне текущей очереди. Пакеты ниже предназначены
для плейтеста; аппаратная приёмка не выполнена.

## Окружения и воспроизведение

Linux: Ubuntu 22.04 x86_64, GCC 11, glibc 2.35, системная SDL2 2.0.20.
Сборка не требует Windows SDK или Zig. CMake >=3.20, Ninja, Python3.
В Docker компилятор находится в общем image storage, не копируется в проект.
Команды из корня, в Linux/WSL (Docker доступен текущему пользователю):

```sh
docker build -t ogdok-linux:22.04 -f ports/sdl2/tools/linux-builder.Dockerfile ports/sdl2/tools
docker run --rm -v "$PWD:/workspace" ogdok-linux:22.04 bash ports/sdl2/tools/build-linux.sh OPENGL
docker run --rm -v "$PWD:/workspace" ogdok-linux:22.04 bash ports/sdl2/tools/build-linux.sh GLES2
```

Нативная сборка: установить build-essential, cmake, ninja-build, libsdl2-dev,
libgles2-mesa-dev, затем `bash ports/sdl2/tools/build-linux.sh OPENGL`.
API задаётся CMake-параметром `DOK_GRAPHICS_API=OPENGL|GLES2`; desktop GL —
по умолчанию, GLES2 — отдельная сборка, включая все GL-пробы. Игровой CLI
`--renderer gl|software` общий. Контекст ES2 не требует GL4ES. Профиль,
семантика симуляции и формат сохранений общие.

Headless unit-тесты используют Xvfb при наличии, иначе SDL dummy. Для настоящих
графических smoke-проверок нужен Xvfb/Mesa или реальный дисплей. `--init`
обеспечивает корректную передачу сигналов Xvfb в контейнере:

```sh
docker run --init --rm -v "$PWD:/workspace" ogdok-linux:22.04 xvfb-run -a python3 ports/sdl2/tools/check_unix.py --exe build/sdl2/linux-x86_64-opengl/OGDoK --out build/sdl2/checks/linux-new
docker run --init --rm -e SDL_AUDIODRIVER=dummy -e MESA_GLES_VERSION_OVERRIDE=2.0 -v "$PWD:/workspace" ogdok-linux:22.04 xvfb-run -a build/sdl2/linux-x86_64-gles2/dok_gl_probe
```

Для ресурсных проверок добавить к check_unix.py до трёх `--res` с путями:
`originals/Old_Gold_3D/OLD_GOLD.RES`, `originals/pkr/RUS/PKR.RES`,
`originals/pkr/ENG/PKE.RES`. Для GLES указать соответствующий executable и
`--expect-api GLES2`. Выходной каталог должен быть новым. SHA-256 RES
проверяются до/после, fallback GL→software считается ошибкой GL-прогона.
Тесты только читают оригиналы; реальные аудиоустройство и геймпад не проверяют.

## PortMaster aarch64

Использован [рекомендуемый builder](https://portmaster.games/docker.html):
`ghcr.io/monkeyx-net/portmaster-build-templates/portmaster-builder`,
зафиксированный digest
`sha256:88aefcc72eae495732c8706e416439b5a6eef7ec58c439374c947838dc6e39c6`.
Фактическая база image — Ubuntu 20.04, GCC 9.4, SDL2 2.32.0; это не
предположение, что любая Ubuntu 22.04-библиотека пригодна для портативки.
На x86 host требуется зарегистрированный QEMU aarch64 binfmt; нативный ARM
host не требует эмуляции. В текущем WSL QEMU установлен общим пакетом
qemu-user-static, через update-binfmts включён qemu-aarch64.

```sh
docker run --rm --platform linux/arm64 -v "$PWD:/workspace" -w /workspace ghcr.io/monkeyx-net/portmaster-build-templates/portmaster-builder@sha256:88aefcc72eae495732c8706e416439b5a6eef7ec58c439374c947838dc6e39c6 bash ports/sdl2/tools/build-linux.sh GLES2
```

Launcher следует [контракту PortMaster](https://portmaster.games/packaging.html):
control.txt, модификации прошивки, get_controls, SDL controller mapping,
gptokeyb только для системного выхода, pm_platform_helper и pm_finish.
Каталог `ogdok` определяется относительно launcher; профиль — `ogdok/user`,
RES — рядом с OGDoK. В архиве нет профиля: обновление файлов не заменяет
пользовательские настройки/сохранения. GL4ES и эмуляция клавиатуры не нужны.
SDL2 и драйвер устройства используются системные; их ABI и наличие всех
динамических зависимостей должны быть подтверждены на устройствах.

## Пакеты

```sh
docker run --rm -v "$PWD:/workspace" ogdok-linux:22.04 python3 ports/sdl2/tools/package_unix.py linux-x86_64 --build build/sdl2/linux-x86_64-opengl --name OGDoK-Linux-x86_64-playtest
docker run --rm -v "$PWD:/workspace" ogdok-linux:22.04 python3 ports/sdl2/tools/package_unix.py portmaster-aarch64 --build build/sdl2/portmaster-aarch64-gles2 --name OGDoK-PortMaster-playtest
```

Выходы — `artifacts/sdl2/`: Linux tar.gz и PortMaster ZIP с executable mode.
В установочном PortMaster ZIP в корне только `OGDoK.sh`, остальные файлы —
в `ogdok/`, включая `port.json`, `gameinfo.xml` и инструкцию `ogdok.md`.
Шаблоны исходников лежат рядом в ports/sdl2/portmaster; это другая структура.
Преобразование подтверждено функцией build_port_zip официального
[build_release.py](https://github.com/PortsMaster/PortMaster-New/blob/main/tools/build_release.py).
Упаковка ARM выполняется на x86 builder: readelf читает ARM ELF без исполнения,
а его Git поддерживает scoped safe.directory для Windows bind mount. Старый Git
в ARM image отклонял этот mount даже с `-c safe.directory`; глобальные настройки
Git не менялись. Это ограничение упаковщика в старом image, не ARM-бинарника.
Упаковщик проверяет ELF64/архитектуру, API, GLIBC <=2.35, записывает зависимости,
хеши файлов/исходников, git revision и dirty-флаг, SHA-256 архива. Старый выход
не перезаписывается. RES, пользовательский профиль, build и SDK не включаются.
Лицензии движка и включённых материалов поставляются. Для установки на Vita
распаковать `OGDoK.sh` и `ogdok/` в каталог ports, добавить свои RES и запустить
через оболочку. Библиотеки из builder и графические драйверы не копировать
на устройство вслепую. Подача в каталог, включая каталожный screenshot, отдельно.

## Будущие аппаратные проверки — обязательный реестр

| Устройство | Роль и режимы | Статус |
|---|---|---|
| RG Vita Pro, ROCKNIX | Основная приёмка PortMaster, 1280×720 GLES Nearest, FXAA/xBR выкл.; дополнительно 1024×768 и родное разрешение | Ожидает доступа; SSH пользователь отложил |
| RG DS Plus, RK3568 | Будущий плейтест и производительность 1024×768 на одном экране; записать состояние второго | Устройство в будущем; прошивка/SDL/драйвер неизвестны |
| RK3326 | Ранее принятый ориентир 1024×768/60 FPS GL | Только расчётная оценка; реальные измерения отсутствуют |

Модель DS Plus: [спецификация производителя](https://win.anbernic.com/product/763.html).
Никакие результаты RK3568/RK3576 не выдавать за аппаратную приёмку RK3326.
Дополнительная будущая DS Plus не блокирует основной пакет Vita.

Для каждого устройства:

1. Запустить `bash collect-device.sh NEW_REPORT.txt` из пакета. Вручную дописать
   версии PortMaster/прошивки, не попавшие в вывод, частоты/режим питания,
   разрешение и конфигурацию дисплеев. Скрипт не меняет настройки системы.
2. Чистая установка, холодный старт, оба пресета управления, полный маршрут
   по меню без клавиатуры, карта, звук/музыка, RU/EN, все семь уровней.
3. Сохранение/перезапуск/загрузка, перенос слотов Windows ↔ Linux ↔ PortMaster,
   обновление пакета с сохранением user, штатный выход и системная комбинация.
4. Сон/пробуждение, отсутствие зависаний, зажатых кнопок, потери звука или
   повреждения профиля. Записать фактическое поведение, а не ожидание от ОС.
5. Прогрев и длительные тяжёлые сцены. Замерять p50/p95/p99, FPS и просадки,
   основной и дополнительные режимы, фильтры отдельно. Цель Vita — устойчивые
   60 FPS; снижать её только отдельным решением пользователя.

Профилирование полного приложения включается переменной окружения, без нового
игрового CLI: `DOK_FRAME_TIMINGS=/absolute/new.csv`. После штатного выхода CSV
содержит work/present/total; present включает VSync и SDL_Delay. Work — время
CPU, перекрывающее работу GPU, не отдельный GPU timer. В память сохраняются
первые 120000 кадров; JSON-сводка явно указывает этот лимит. Снимки, меню,
паузы и загрузки входят в полный кадр: выделять интервалы игрового прогона
отдельными запусками, не смешивать их с интерактивным плейтестом.

```sh
python3 ports/sdl2/tools/summarize_frames.py new.csv --warmup 300 --output new-summary.json
```

Для поиска GPU-узких мест использовать существующий dok_render_bench с теми
же сценами и API. Его glFinish-пробы не равны полному игровому FPS.
Ограничение 60 FPS скрывает запас: для оценки переноса на RK3326 нужны также
прогоны без VSync/ограничения и отдельная калибровка на самом RK3326.

## Результаты текущего участка

Исходная ревизия: `91dfce919e29b12316fedb60e7ca23e4693ac077`, начальное дерево чистое.
Текущие пакеты содержат изменения рабочего дерева; manifests указывают dirty=true
и SHA-256 исходников. Изменены только SDL2, его CI и документы Source Port.

- Windows Release/CTest 20/20; Linux GL 20/20; Linux GLES 20/20; ARM64/QEMU 20/20.
- Исходные GL/GLES smoke: по 14 процессов; финальный строгий GLES2 и Linux
  runtime без SDK: по 15 процессов, включая потерянный RES. Все три набора,
  GL/software, UTF-8/пробелы, cwd, portable/platform/explicit, снимки и CSV.
- Mesa принудительно ограничена GLES 2.0: dok_gl_probe PASS с производными
  и без них. Без производных тест подтверждает побайтовый fallback к linear,
  не требует эффекта анизотропии, недоступного в этом режиме.
- Python Unix tools 3/3 и launcher 1/1 PASS (Linux). Первый запуск Python-тестов
  в Windows sandbox не прошёл из-за ACL Temp; код тестов проверен в Linux.
- Реальные процессы Windows/Linux/ARM создали quicksave PKR; каждый из трёх
  executable загрузил все три файла — 9/9 PASS, байты слотов не изменились.
  Это проверка формата под QEMU, не файловой системы microSD и не всех игр.
- Linux runtime image содержит только системные runtime-библиотеки и средства
  smoke-проверки, без CMake, компилятора, headers и SDK.

Полные отчёты: `build/sdl2/checks/linux-opengl-01`, `linux-gles2-final`,
`linux-runtime-01`, `save-exchange-01`. Компактная сводка —
`analysis/summaries/sp20-platforms-20261108.json`. Точные повторные команды
с теми же параметрами требуют новых выходных каталогов.
Копии JSON-отчётов, CTest-журналов и manifests сохранены в
`research/evidence/source-port/sp20-platforms-20261108/`, вне build и Git.

Готовые архивы без RES:

- `artifacts/sdl2/OGDoK-Linux-x86_64-playtest-20261108-r2.tar.gz`
- `artifacts/sdl2/OGDoK-PortMaster-playtest-20261108-r3.zip`

Рядом — SHA256.txt; проверка полного состава и executable mode:

```sh
python3 ports/sdl2/tools/check_unix_package.py artifacts/sdl2/OGDoK-Linux-x86_64-playtest-20261108-r2.tar.gz
python3 ports/sdl2/tools/check_unix_package.py artifacts/sdl2/OGDoK-PortMaster-playtest-20261108-r3.zip
```

Восстановление матрицы обмена PKR quicksave: для Windows, Linux и ARM/QEMU
выполнить `check_save_exchange.py create`, затем `load`, задавая собственные
`--platform`, `--exe`, `--res originals/pkr/RUS/PKR.RES` и один общий новый
`--out`. Пример Linux: `python3 ports/sdl2/tools/check_save_exchange.py create
--platform linux --exe build/sdl2/linux-x86_64-opengl/OGDoK
--res originals/pkr/RUS/PKR.RES --out build/sdl2/checks/save-exchange-next`.
Для `load` заменить только первый аргумент. Все три create должны предшествовать
всем load, иначе проверена неполная матрица. Загружаемые байты не меняются.

Аппаратные плейтесты, производительность, звук и управление устройств не
проверены. CI-конфигурация добавлена, удалённый GitHub Actions ещё не запускался.
