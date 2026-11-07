# SP-OPTIONAL-HITS — попадания у границ клеток

Статус: выполнено, 7 ноября 2026 года.

## Цель и граница

По запросу пользователя исправить стрельбу опционально, сохранив правила
простреливания стен. Добавить настройку геймплея, по умолчанию выключенную.
Классический режим должен сохранить прежние контактные трассы. Состояние
настройки хранится в профиле и применяется также после загрузки игры.
GBA/NDS, дверь LEVEL3 и оригинальные ресурсы не менять.

## Материалы и контракт

Прочитаны Source Port plan/handoff, SP-RC1-REVIEW, projectiles.cpp и его probes,
settings/frontend/main. Native-контакт описан в spec/projectile-native-audit.md;
это осознанная адаптация по запросу, не новое восстановление оригинала.
В дереве уже есть изменения меню SDL2 и параллельные изменения NDS: сохранить их.

Дополнительное попадание ищет живого монстра возле траектории с радиусом
штатного collision footprint (min_distance). Преграды между точкой контакта
и центром цели проверяются отдельно, включая углы клеток. Двери и поверхности
используют тот же predicate простреливания, что и основной снаряд:
Kremlin door flags8, OG door state2, остальные flags0x18.
Прежние попадания, first-wake, friendly-class filter, урон, анимации сохраняются.

## Работы и приёмка

1. Ввести настройку/меню/профиль и живое применение к Projectiles.
2. Дополнить контакт монстров с проверкой преград; по выключенному флагу
   оставить прежний путь без дополнительного поиска.
3. Проверить границы, углы, стены/простреливаемые поверхности и двери,
   default/off, сохранение настройки и native regression.
4. Release/CTest, probes, Windows-пакет; обновить паспорт, план и handoff.

## Результат

Реализована настройка «Геймплей → Исправление попаданий», RU/EN,
по умолчанию Выкл. INI: `gameplay.boundary_hits=0|1`, участвует в merge
пользовательских правок. Вкл. включает исправление, Выкл. сохраняет оригинал;
подсказка поясняет промахи на границе клеток. Полярность флага не менялась.
Сбрасывается в 0 кнопкой defaults. Применяется к
текущим снарядам сразу; новый World и загрузка получают актуальное значение
профиля. В сохранение World флаг не входит; формат сохранений не менялся.

Основной клеточный контакт сохранён. При включённой опции дополнительный поиск
проверяет пересечение короткого отрезка шага с кругом живого монстра радиуса
min_distance. Просматриваются только клетки bounding box отрезка с этим запасом,
цели дедуплицируются по glyph и упорядочиваются по первому касанию/OwnerId.
Свой класс и мёртвые цели отсекаются; вызов общего hit сохраняет первый wake
без урона, damage и effect lifecycle. Дополнение исправляет попадания по
монстрам; столкновение вражеских снарядов с игроком сохраняет клеточные правила.

Стены имеют приоритет: если штатный контакт шага остановил снаряд, расширенный
поиск не выполняется. При дополнительном попадании обе линии — весь шаг и
его конец → центр монстра — проверяются целочисленным supercover по клеткам,
включая обе боковые клетки точного угла. Поэтому соседняя цель за преградой
не получает урон. Используются прежние флаги 0x18 и отдельный door predicate
OG/Kremlin. Размеры спрайтов, высота и типы снарядов не задают новых правил.
Это ограниченная адаптация контакта у границ, не замена всего боя геометрией.

Изменения: projectiles.hpp/.cpp, settings.hpp/.cpp, frontend.hpp/.cpp, main.cpp,
README.txt; boundary_hits_probe.cpp, frontend_tests.cpp, check_boundary_hits.py,
CMakeLists.txt. Прежние изменения меню сохранены; параллельный NDS не редактировался.
Нового анализа оригинальных EXE не было. Native regression исполняет ранее
восстановленный oracle; SHA-256 EXE/RES до и после совпали.

Приёмка итоговых исходников:

- Release + CTest **20/20 PASS**; после последней правки изменённые translation
  units пересобраны по командам текущего Ninja-графа, все цели перелинкованы,
  CTest повторён. Не используется другой набор флагов/toolchain.
- `dok_boundary_hits_probe.exe .`: **180 PASS**, три игры, обе оси/стороны,
  опция on/off, solid/flags8/flags16, OG и Kremlin door state/flags,
  непростреливаемый и простреливаемый угол, боковая стена, dead/friendly,
  первый wake, radius−1/radius/radius+1.
- Native oracle `check_projectile_native.py --limit 256`: **1792/1792 PASS**,
  7 LEVEL × 256; это ограниченная регрессия, не повторение полного аудита 27917.
- `dok_projectile_probe.exe` на PKR/PKE: **54 PASS**, lifecycle/damage/render/FPS.
- `dok_rc1_review_probe.exe .`: прежние 2 door и 12 native-boundary сценариев PASS.
- `check_boundary_hits.py`: **18 процессов PASS**, повторено на packaged EXE:
  меню RU/EN, три игры × GL/software, немедленное применение, F5/F9, restart,
  выключение и сохранение в профиль.
- frontend_probe: прежние 40 resource-loss сценариев, 7 RES combinations,
  336 menu frames, 35 software scenes, 4 backend rebuilds PASS.
- На пакете: resource-refresh **12/12**, OG menu/start **8/8**, package **8/8 PASS**.
- `git diff --check` PASS. Реальные устройства/полное прохождение не проверялись.

Команды из корня:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File ports/sdl2/tools/build.ps1 -Configuration Release -DirectCommands
build/sdl2/windows/dok_boundary_hits_probe.exe .
build/sdl2/windows/dok_projectile_probe.exe originals/pkr/RUS/PKR.RES originals/pkr/ENG/PKE.RES
build/sdl2/windows/dok_rc1_review_probe.exe .
py -3.14 -B ports/sdl2/tools/check_projectile_native.py --limit 256
powershell -NoProfile -ExecutionPolicy Bypass -File ports/sdl2/tools/package.ps1 -PackageName windows-rc2-optional-hits
py -3.14 -B ports/sdl2/tools/check_boundary_hits.py artifacts/sdl2/windows-rc2-optional-hits/OGDoK.exe
py -3.14 -B ports/sdl2/tools/check_package.py artifacts/sdl2/windows-rc2-optional-hits --hidden
```

Текущий локальный пакет `artifacts/sdl2/windows-rc2-optional-hits/`, EXE SHA-256
`e25ef455441673834a5f2a3323a736c71ccbe6aba9d73cec6fbe879976d256ba`.
Штатная локальная упаковка с тремя RES. RC2 не опубликован.
Процессный отчёт — `research/evidence/source-port/sp-optional-hits/process-report.json`;
native — `research/evidence/source-port/sp11-native-complete/report.json`,
компактная сводка сформирована функцией summary из summarize_reports.py:
`analysis/summaries/sp-optional-hits-native.json`. Она не заменяет локальный отчёт.
Следующий основной ID — SP19: плейтест/подготовка RC2 по запросу пользователя.

### Уточнение названия — 7 ноября

По замечанию пользователя пункт назван «Исправление попаданий» / «Hit detection fix».
Подсказка поясняет границы клеток и значение Выкл. Полярность и INI сохранены:
Вкл. исправляет, Выкл. оставляет оригинал. Изменены только строка UI и документация.
Пересобраны frontend.cpp, его библиотека, OGDoK и frontend tests по текущему
Ninja-графу. frontend tests PASS; `check_package.py artifacts/sdl2/windows-rc2-hit-fix --hidden`
8/8 PASS; `git diff --check` PASS. Наличие новой строки проверено в самом EXE.
Текущий пакет `artifacts/sdl2/windows-rc2-hit-fix/`, EXE SHA-256
`ca2bca6037a2fc01b692aaeb05a2c5084f9567fba1621b283fe2c6e9c0c81929`.
Предыдущие полные функциональные проверки выше относятся к исходной реализации
опции; после этой правки повторены проверки интерфейса и упаковки.
