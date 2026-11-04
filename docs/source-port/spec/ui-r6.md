# SP15/SP19 R6 — запуск и стабильность меню

4 ноября 2026 года. Прямые уточнения пользователя после R5.

- EXE собирается с Windows GUI subsystem (PE Subsystem=2), без отдельной консоли.
  WIN32_EXECUTABLE задаёт намерение CMake; для общего Zig 0.13 дополнительно
  указан `-Wl,--subsystem,windows`, поскольку одного `-mwindows` оказалось
  недостаточно: это проверено чтением PE готового EXE, не предположением по флагам.
  Только dok_port стал оконным; диагностические probe остаются консольными.
- Фатальная ошибка сохраняется в port.log и показывается отдельным окном.
  `--hidden` подавляет окно ошибки, stderr и exit code сохраняются для проверок.
  Если SDL/каталог пользователя ещё не инициализирован, журнал может быть недоступен.
- Батники Play/Play-OpenGL/Play-Software удалены из поставки и package.ps1.
  При обновлении существующего пакета удаляются только эти три известных файла.
  Запуск — dok_port.exe, выбор рендерера — меню графики, настройка сохраняется.
  CLI overrides оставлены для диагностики, отдельные launcher-файлы не нужны.
- Исправлен скачок после ресайза при движении мыши: R5 вычисляла высоту footer
  по help текущего focus. При наведении на строку без help вся группа меняла
  размер и центрировалась заново. Теперь резерв help одинаков для всех строк
  одного экрана: учитывается наличие описания у любой строки, а не выбранной.
- «Как играть» удалён из главного меню и паузы, индексы действий обновлены.
  Управление описано в README; существующий внутренний help-screen не удалён
  из enum/renderer, чтобы не менять диагностические номера экранов.
- Из RU/EN титров удалена строка о происхождении из титров и README.

## Проверки

Release CTest17/17 PASS. Frontend regression проверяет неизменность положения
заголовка и попадание мыши по выбранной строке при смене focus после ресайза
на 800×600, 1280×720, 1920×1080. В графике обходятся 12 шагов навигации,
включая строки без описаний, disabled и прокрутку.
518 CPU-кадров и 288 GL/software readback трёх игр/RU/EN PASS; main/credits
просмотрены. 17 menu UX и шесть frontend процессов PASS. Итоговый GUI EXE
ещё раз прошёл шесть frontend и восемь packaged процессов (три игры/no-RES,
GL/software). Package gate проверяет PE Subsystem=2 и отсутствие .cmd/.bat.
Все диагностические окна hidden, аудио dummy, настройки изолированы.
Игровые механики и полные прохождения повторно не проверялись.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File ports/sdl2/tools/build.ps1 -Configuration Release
$env:SDL_AUDIODRIVER='dummy'
build/sdl2/windows/dok_menu_font_probe.exe . research/evidence/source-port/sp15-ui/r6 --readback
py -3.14 -B -X utf8 ports/sdl2/tools/check_menu_ux.py research/evidence/source-port/sp15-ui/r6-process
py -3.14 -B -X utf8 ports/sdl2/tools/check_frontend.py
powershell -NoProfile -ExecutionPolicy Bypass -File ports/sdl2/tools/package.ps1 -PackageName windows-playtest-20261104-sp15-r6
py -3.14 -B -X utf8 ports/sdl2/tools/check_package.py artifacts/sdl2/windows-playtest-20261104-sp15-r6 --hidden
```

Пакет: `artifacts/sdl2/windows-playtest-20261104-sp15-r6.zip`, пять файлов,
без оригинальных RES/EXE. R5 сохранена. Следующий основной ID — SP17.
SP19 целиком не закрыта: это исправление поставки текущего плейтеста.
