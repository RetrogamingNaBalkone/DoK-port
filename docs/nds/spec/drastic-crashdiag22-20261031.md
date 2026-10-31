# N23: отказ audio21 на первом уровне, crashdiag22

31 октября 2026 пользователь сообщил об отказе в DraStic на RG DS при ходьбе
и стрельбе на LEVEL1. Уточнил: запущена **audio21**. На фото верхнего экрана
`DoK loading candidate 20`, `Stage: 6`, `Code: 00010000`; нижний экран сохраняет
раскрытую карту. Метка около левого центрального прохода; точных координат
из фотографии нет. Пользовательский ROM не получен и его SHA не сверялся.

## Что установлено

В `platform/gameplay_probe/main.c` код 65536 присваивается при нулевом результате
`frontend_loop()`. Он объединяет ошибки аудио, симуляции, подготовки сцены,
текстур, GPU, карты и переходов. `loading_stage` после входа в игру остаётся 6;
это не стадия аварии. Текст версии был захардкожен ещё в loading20 и остался
в audio21. Данный экран не доказывает ни ошибку звука, ни ошибку снаряда.
Точная причина пользовательского отказа **не установлена**.

## Диагностическая сборка

`crashdiag22-ru` использует feature_selection из `audio21b-ru/build-manifest.json`.
ARM7 audio21 и игровая семантика сохранены. В fatal returns функций
`sprites_prepare`, `effects_prepare`, `mask_identity`, `render_one` и функций
`frontend/loop.inc` добавлена запись первой пары `__func__`/`__LINE__`.
Внешние обёртки её не перезаписывают. Запись выполняется только при отказе.
Числовые коды возвращаемых статусов API этим механизмом не сохраняются.

Экран `DoK crashdiag 22 (audio21)` показывает функцию/строку, кадр,
прежнюю effect line, game/AI/combat/effects fault, позу, GPU sticky flags,
map fault и raw DMA profile, опубликованные audio fault/underrun/stale/voice,
raw ACK/expected. DMA profile может быть устаревшим; это вспомогательные
сырые значения. Экран печатается до audio cleanup. Старый effect line мог
быть перезаписан внешним кодом, основная привязка — новая строка `At`.
Для иных кодов ошибки диагностический экран загрузки сохранён.

## Проверки

Общий BlocksDS/GCC через MSYS2, `-Werror`. Первый запуск в песочнице получил
`couldn't create signal pipe, Win32 error 5`; повтор с разрешённым выходом
из песочницы собрал ROM. Компилятор в проект не устанавливался.

```powershell
py -3.14 -B -X utf8 -c "import json,sys; from pathlib import Path; sys.path.insert(0,'ports/nds/tools/performance'); from build import build; f=json.loads(Path('build/nds/n23/audio21b-ru/build-manifest.json').read_text())['feature_selection']; build('crashdiag22-ru',f)"
py -3.14 -B -X utf8 ports/nds/tools/performance/run_logged.py build/nds/n23/crashdiag22-ru/crash-diagnostic.log.gz ports/nds/tools/performance/check_crash_diagnostic.py build/nds/n23/crashdiag22-ru
py -3.14 -B -X utf8 ports/nds/tools/performance/run_logged.py build/nds/n23/crashdiag22-ru/audio-transition.log.gz ports/nds/tools/performance/check_audio_transition.py build/nds/n23/crashdiag22-ru
git diff --check
```

Pinned melonDS DS1.4: короткий вход на LEVEL1 без движения PASS.
`check_crash_diagnostic.py` **явно меняет RAM эмулятора**: выставляет sticky GPU
flag в private `obs`, затем проверяет код65536/status3 и сохранение
`render_one:1369`, несмотря на внешний `frontend_loop`. Это синтетический
отказ, не воспроизведение пользовательского сбоя и не тест реальной ошибки GPU.
PNG верхнего экрана просмотрен: текст помещается, причина читается.
Обычный короткий title→LEVEL1 audio transition/waveform PASS; проверка
выстрелов по-прежнему не заявляется: на старте у героя нет оружия.
Маршруты и остальные уровни не запускались.

NitroFS проверен независимо: набор пяти файлов и SHA всех payload совпали
с build manifest. Поставка только отдельным файлом
`artifacts/nds/dok-crashdiag22.nds`, 10167808 байт, SHA-256
`69175dfd79db4837a536b2380dca0464cc23316e0df1d257eb8cdcb379582c01`.
Основной `dok.nds` и его manifest остаются audio21.
Сводка `analysis/nds-crashdiag22-20261031.json`; локальные результаты/PNG/logs
в `research/evidence/nds/n23/crashdiag22/`, исходники/ELF/build manifest —
в обычном immutable архиве `research/evidence/nds/n23/candidates/crashdiag22-ru/`.

Следующий ID **N23**: холодный запуск отдельного ROM в DraStic без старого
savestate, повтор ходьбы/стрельбы около отмеченной области, получить полный
верхний экран. Сопоставлять `At` только с crashdiag22 ELF/исходниками;
затем локализовать и исправить конкретный отказ. N23 не принята, N24 не начата.
GBA/SDL2, оригиналы и принятые N18 материалы не менялись.
