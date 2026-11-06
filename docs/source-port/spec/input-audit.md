# Проверка ввода SP09

## Классический ромб — 6 ноября 2026

По запросу пользователя нижняя кнопка — бег, левая — огонь, верхняя — дверь,
правая — следующее оружие. В SDL GameController это A/X/Y/B; позиции важнее
букв на конкретном устройстве. Крестовина, L1/R1, Select-карта и Start-пауза
сохраняются. Современный пресет не менялся. Это собственная схема порта,
не копия SNES Wolfenstein 3D. Полный прежний stock classic набор кнопок и
триггеров мигрирует при загрузке INI; custom и отличающиеся наборы сохраняются.
Приёмка и ограничения — [SP-CLASSIC-PAD](../tasks/SP-CLASSIC-PAD.md).

4 ноября 2026 года. Платформа: Windows x64, SDL2 2.32.10, Release,
общий Zig 0.13.0. Область: адаптер SDL и существующие настройки порта.
Новые выводы об оригинальных EXE не делались; оригиналы не изменялись.

## Дополнение r3: короткие огонь/цифры оружия

По уточнению OG/IDKFA в SP11 воспроизведена потеря DOWN+UP до одного
render sample: огонь и2 не действуют при живой ходьбе/оружейном callback.
Теперь `SdlInput` сохраняет press edges только fire/1/2 до `sample()`,
main сохраняет fire edge до ровно ближайшей попытки player tick.
Огонь удержанием сохранён, ожидания будущей готовности оружия нет.
Выбор1/2 — одно действие на press, без повторного hide на каждом кадре.
Сбросы focus/clear/unplug/menu/session не пропускают отложенный огонь.

Unit `input`: быстрые Ctrl/ЛКМ/1/2/trigger,30/60/144 FPS без player tick,
одна попытка и held-fire PASS. Шесть actual GL/software before/after/reset
процессов PASS. [Точная репродукция и границы](combat-audit.md#короткие-нажатия-воспроизведение-и-исправление-r3).

## Исправление focus

`SdlInput::handle` сбрасывал клавиатуру/мышь/контроллер при FOCUS_LOST,
но продолжал принимать события осей и кнопок контроллера в фоне.
`sample()` скрывал накопленное состояние до FOCUS_GAINED; после него
могли снова появиться движение, поворот, use или fire. Клавиатурные события
в том же состоянии уже игнорировались. Дополнительно `open()` считывал
удерживаемые кнопки/оси устройства, подключённого во время отсутствия фокуса.

Теперь оба controller event пути проверяют `focused_`; `open()` считывает
начальное состояние только с фокусом. Удаление устройства работает и в фоне.
Возврат фокуса сам не возобновляет игру: основной цикл открывает паузу,
а `resume` очищает ввод. Свежие foreground-события разрешены.
Это исправление современного SDL-ввода, не перенос из DOS.

## Программная матрица

| Проверка | Результат и граница |
|---|---|
| Независимые forward/strafe/turn | PASS клавиатура и SDL virtual controller; суммирование с ограничением |
| Клавиатура/мышь | PASS remap, обе цифры оружия, classic Alt-strafe, mouse button remap |
| Оси/кнопки в фоне | PASS FOCUS_LOST → оси движения/поворота/триггера и use → FOCUS_GAINED остаётся нейтральным |
| Подключение в фоне | PASS удерживаемый virtual pad подключается, но refocus не возвращает движение/use |
| Возврат активности | PASS свежее foreground-событие снова двигает; `clear()` его снимает |
| Отключение | PASS active fire/run сбрасываются при удалении контроллера |
| Classic pad без правого стика | PASS одновременно D-pad движение/поворот, L1-стрейф, A-огонь/B-бег; отдельно X-use, Y-next weapon, Select-map, Start-pause, R1-стрейф |
| Edge actions | PASS map/pause/next weapon не повторяются на следующем sample при удержании |
| Настройки | PASS полный INI roundtrip с classic/custom remap и inertia=false; независимые пресеты, CLI isolation, NaN/version fallback, безопасный отказ замены файла |
| Инерция | PASS существующая movement-матрица трёх профилей × вкл./выкл. × 30/60/144 FPS; отпускание/реверс/диагональ и внешнее течение |

Назначение map в этой проверке означает генерацию InputActions.map.
Сам экран карты и справка принадлежат SP15. Настройки проверены сохранением
и повторным чтением; новый запуск приложения здесь не подменяется INI-тестом.
Физический геймпад, его mapping, эргономика и аппаратный hotplug не проверены.
SP09/A08 остаётся открытой в этой аппаратной границе.

## Воспроизведение

Из корня репозитория:

```powershell
$env:ZIG_GLOBAL_CACHE_DIR = Join-Path (Get-Location) 'build/sdl2/zig-cache'
$taskCmake = (& py -3.14 -c 'import cmake; print(cmake.CMAKE_BIN_DIR)').Trim()
& "$taskCmake/cmake.exe" --build build/sdl2/windows --target dok_input_tests dok_frontend_tests dok_movement_tests --parallel 2
build/sdl2/windows/dok_input_tests.exe
build/sdl2/windows/dok_frontend_tests.exe
build/sdl2/windows/dok_movement_tests.exe
```

Targeted build выполнен с разрешением вне песочницы: внутри Ninja не запускал
дочерний compiler process, как описано в building.md. Свой зависший запуск
остановлен; общий toolchain не устанавливался и не копировался.
Все три пересобранных теста завершились с кодом 0. Исходные stdout, SHA-256
проверенных тестовых EXE и результаты:
`research/evidence/source-port/sp09-input/report.json` и соседние `.log`.

Изменены `ports/sdl2/src/sdl_input.cpp`, `ports/sdl2/tests/input_tests.cpp`,
паспорт SP09 и эта спецификация. Общие plan/handoff/acceptance обновляет
основной агент после интеграции SP08. Следующий ID — SP10.
