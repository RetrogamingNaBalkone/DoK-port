# SP-XBR-GPU — исправление стоимости xBR

Статус: Windows-реализация и программная приёмка завершены 6 ноября 2026.
Пользователь сообщает падение до 40 FPS,
предлагает перенести обработку готовой сцены на GPU и сохранить требования.

## Цель и границы

Замерить прежний CPU xBR и вывод, перенести xBR на необязательный GL2/GLES2
путь без изменения программной растеризации мира. Windows 8 x64, GL2/GLES2
и платформенные границы сохраняются. При недоступном GPU-пути остаётся CPU.
Оружие/HUD/меню не фильтруются. GBA/NDS, оригиналы и симуляция не меняются.

## Материалы и контракты

Прочитаны plan/handoff, SP-FILTERS-UX, filters.cpp, display.cpp и gl_renderer.cpp.
База содержит незакоммиченные результаты SP-FILTERS-UX; они сохраняются.
Нового анализа EXE нет, оригинальные адреса не применяются.
Контрольный алгоритм — существующий xbr3 (Hyllian xBR-lv2, MIT).
Нужен отдельный необязательный shader, чтобы его ошибка/лимиты не ломали
обычные GL и SDL режимы. Использовать уже существующие FBO entrypoints;
без compute, texture arrays, новых версий API и обязательных расширений.

## Работы и приёмка

1. Зафиксировать CPU baseline на реальных кадрах в нескольких размерах.
2. Добавить GPU xBR с сохранением 3×-сетки и последующего Nearest, отдельный
   аппаратный вывод программной сцены, CPU fallback при недоступности.
3. Сравнить CPU/GPU пиксели, alpha, resize, вывод без увеличения и отдельный HUD.
4. Release/CTest, реальные процессы, измеренный полный вывод без vsync,
   обновлённый пакет artifacts/sdl2 и документация.

## Реализация и причина

База `3bf64a43cd36783ff9a82b89820668155ec280e8` плюс сохранённые изменения
SP-FILTERS-UX. Исходный CPU xbr3: blob `aff3a8dd868a93681213a59c634230fc9445ff0e`.
На текущем ПК он занимал **18,76 / 84,11 / 203,22 мс** на сценах 320×160,
640×384, 1024×614; transform+upload — 19,52 / 87,41 / 210,53 мс.
Это объясняет пользовательское падение FPS: дорогая обработка каждого входного
пикселя и построение 9 выходных пикселей выполнялись на CPU каждый кадр.

Добавлен независимый xBR shader GLSL 1.10 / ES 1.00 (`xbr_shader.inc`),
ленивое создание программы и переиспользуемый цветовой FBO 3×. Загружается
исходный кадр, а не увеличенный CPU-буфер. Затем результат выводится Nearest.
Readback между проходами отсутствует. Resize, исходный FBO, viewport и UI
проверены. Основной GL shader и xBR shader компилируются независимо.

Software-Display пытается использовать GL только как presenter; `Display::gl()`
остаётся nullptr и main продолжает CPU-растеризацию мира. Без контекста/базового
shader возвращается прежний SDL Renderer. Без xBR shader/FBO/highp/допустимого
размера остаётся CPU xbr3. Если CPU-результат превышает GL_MAX_TEXTURE_SIZE,
видимая часть выводится ограниченными плитками. Минимум GL2/GLES2/Windows 8 x64
не повышен; compute, texture arrays и обязательных новых расширений нет.

Обнаружена также нестабильность старого float-выбора контура: округление luma
и весов меняло сторону на равных расстояниях CPU/GPU. Для opaque 8-bit цветов
используется точное отношение luma 897:1761:342 с эквивалентно масштабированными
порогами. На фиксированной сетке 3× веса кратны 1/8; CPU хранит таблицу,
GPU привязывает веса к ней. Относительно старого CPU возможны небольшие
локальные изменения на таких границах, без смены варианта xBR. Новый CPU/GPU
совпадает с допуском 1 уровень RGB. Таблица ускорила и CPU fallback.

## Проверки

- Release/CTest **20/20 PASS**. Дополнительный `dok_xbr_probe` PASS:
  диагональ/alpha-диагональ дают точное совпадение, random RGB и реальная сцена
  PKR — максимум 1 уровень канала; CPU fallback побайтово совпадает с reference,
  включая принудительный лимит текстуры 32 и плитки. Проверены восстановление
  fullscreen FBO/UI и software-путь с `gl()==nullptr`.
- GL regression probe PASS: LIGHT/палитра/alpha/глубина, пакетные материалы,
  nearest/linear/mips/анизотропия, Sharp/FXAA, FBO/полноэкранное вписывание.
- **25 реальных процессов готового пакета**: три игры × три размера GPU/CPU,
  bypass без увеличения, принудительный SDL fallback; дополнительно PKR с
  SDL_VIDEODRIVER=dummy и настоящим отсутствием OpenGL. Renderer остаётся software,
  gpu_xbr=1 только при увеличении с доступным GPU. CPU/GPU отличаются не более чем
  на 1 уровень канала; исходные RES SHA-256 до/после совпадают.
- **60** процессов прежних aliases/F4/различимости software режимов PASS,
  **82** общей матрицы семи карт/фильтров/настроек PASS, **68** save/load PASS.
  **8 packaged запусков PASS**, localization `--check`, `git diff --check` PASS.

Замер на реальной статичной PKR-сцене, warmup2/samples12, p50, glFinish без
vsync/present. Размер окна в probe 960×480; FBO xBR обрабатывает полный вход 3×,
финальный прямоугольник в обоих путях одинаков и клипуется окном.
Это измерение фильтра+upload, не полного runtime FPS:

| Вход сцены | Старый CPU transform+upload | Новый CPU fallback | GPU upload+xBR |
|---|---:|---:|---:|
| 320×160 | 19,52 мс | 15,04 мс | 0,146 мс |
| 640×384 | 87,41 мс | 69,09 мс | 0,478 мс |
| 1024×614 | 210,53 мс | 173,44 мс | 1,227 мс |

Шейдер/FBO инициализируются до измеренных итераций. Первая активация может
иметь разовую стоимость компиляции. Числа относятся к текущему ПК; переносить
их на RK3326 нельзя. GLES/устройство/драйверы с ограничениями требуют SP20;
запуск на реальной Windows 8 здесь не выполнялся.

## Поставка и воспроизведение

Пакет: `artifacts/sdl2/windows-playtest-ogdok/OGDoK.exe`.
SHA-256 `074beed8f0cc735d2756a2f14a83d8b7e969bd08528efb37d5cf244ebec0be96`.
Контракт: [filters.md](../spec/filters.md). README и MIT license обновлены/сохранены.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File ports/sdl2/tools/build.ps1 -Configuration Release -DirectCommands
build/sdl2/windows/dok_xbr_probe.exe . research/evidence/source-port/sp-xbr-gpu
build/sdl2/windows/dok_gl_probe.exe
py -3.14 -B -X utf8 ports/sdl2/tools/check_filter_choices.py --out research/evidence/source-port/sp-xbr-gpu-choices
py -3.14 -B -X utf8 ports/sdl2/tools/check_filters.py --out research/evidence/source-port/sp-xbr-gpu-matrix
py -3.14 -B -X utf8 ports/sdl2/tools/check_saves.py --out research/evidence/source-port/sp-xbr-gpu-saves
powershell -NoProfile -ExecutionPolicy Bypass -File ports/sdl2/tools/package.ps1 -PackageName windows-playtest-ogdok
py -3.14 -B -X utf8 ports/sdl2/tools/check_xbr_gpu.py --app artifacts/sdl2/windows-playtest-ogdok/OGDoK.exe --out research/evidence/source-port/sp-xbr-gpu-packaged
py -3.14 -B -X utf8 ports/sdl2/tools/check_package.py artifacts/sdl2/windows-playtest-ogdok --hidden
```

Полные evidence: `sp-xbr-gpu/{probe.log,timings.json}`, `sp-xbr-gpu-baseline/`,
`sp-xbr-gpu-packaged/report.json`, `sp-xbr-gpu-{matrix,saves}/report.json`,
`sp-xbr-gpu-choices/choices.json` в `research/evidence/source-port/`.
Baseline измерен до исправления численной устойчивости через `--measure-only`:
его GPU-кадры имели известное расхождение и не являются принятыми эталонами.
Сохраняются исходный CPU baseline и итоговый probe; промежуточные сборки не
архивировались. Временные профили/кадры удалены checker по политике.
Компактная сводка: `analysis/source-port/sp-xbr-gpu-summary.json`.
Основная Release-пересборка/CTest — `build/sdl2/xbr-gpu-release-build.log`;
после добавления плиточного fallback gl_renderer/display/main/probe перелинкованы
по тому же Ninja-графу, итоговый probe повторён.

Вопросы пользователя об общей настройке масштабирования GL/software и подписи
FXAA разобраны отдельно от устранения тормозов: технической необходимости
привязывать масштабирование только к software нет, особенно при внутреннем
пониженном GL-разрешении. Но применение нового выбора к GL-сцене и перестройка
композиции/HUD в этой задаче не реализованы. FXAA остаётся независимым эффектом
краёв, полезным при 1:1, в отличие от увеличения Sharp/xBR.

Следующий ID — **SP18**, текущий список ручных/аппаратных проверок; расширения
общей настройки композиции требуют отдельного явно согласованного объёма.
GLES/RK3326 и стоимость опций на устройстве — SP20. GBA/NDS не менялись.
