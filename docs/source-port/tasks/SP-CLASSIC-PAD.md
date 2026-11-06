# SP-CLASSIC-PAD — классический геймпад, настройки по умолчанию и снимки

Статус: выполнено, 6 ноября 2026.

## Цель и граница

По окончательному выбору пользователя классический пресет получает ромб:
снизу — бег, слева — огонь, сверху — дверь, справа — следующее оружие.
SDL использует позиции Xbox: A/X/Y/B соответственно. Современный пресет,
клавиатура, крестовина, L1/R1-стрейф, Select-карта и Start-пауза сохраняются.
Схема SNES Wolfenstein 3D обсуждалась, но пользователь выбрал свою.

## Материалы и контракт

Прочитаны AGENTS.md, Source Port plan/handoff, SP09, input-audit.md,
menu-settings.md, sdl_input.cpp, settings.cpp, input_tests и frontend_tests.
База: `4df9044d683eabb0d7183147848151e150e30057`, дерево чистое.
Это адаптация SDL-ввода по запросу пользователя; исследования DOS не нужны.

InputBindings::gamepad(classic) задаёт новые позиции. При чтении INI
мигрирует только прежний неизменённый классический набор кнопок/триггеров;
custom и изменённые наборы сохраняются. Остальные настройки не сбрасываются.

## Работы и приёмка

Дополнение пользователя: новые настройки и сброс видеонастроек должны давать
FPS Выкл., FXAA Выкл., Nearest для GL-текстур и software-масштабирования.
Явно сохранённые значения прежних профилей остаются пользовательским выбором.
Проверить конструктор, отсутствующий/неполный INI, сброс и roundtrip.

Дополнение о снимках: F12 сохраняет в screenshots/ выбранного профиля,
рядом с settings.ini и saves/. В portable это каталог EXE. Явный
--screenshot ПУТЬ сохраняет прежнюю CLI-семантику. Проверить запуск из другой
рабочей папки, portable, кириллицу/пробелы и явный CLI-путь.

1. Обновить классический пресет и описание позиций в README.txt.
2. Проверить через SDL virtual controller одновременные движение, стрейф,
   поворот, бег и огонь; дверь, оружие, короткий огонь, карту и паузу.
3. Проверить миграцию старого INI, сохранность custom и современного пресета,
   повторную запись/чтение настроек.
4. Выполнить Release/CTest, обновить Windows-пакет, проверить его запуск.
5. Обновить паспорт, план и handoff; следующий основной ID — SP18.

## Результат

Реализованы новый классический ромб, миграция только прежнего неизменённого
classic набора и инструкция с позициями кнопок. Modern, custom, крестовина,
плечи, карта и пауза сохранены. Миграция не сбрасывает выключатель устройства,
deadzone, клавиатуру и видеонастройки; повторное чтение стабильно.

FPS по умолчанию выключен в AppSettings, при отсутствии поля INI и при сбросе
видео. FXAA уже был выключен, оба фильтра уже были Nearest; их значения
сохранены и включены в проверку defaults. Явные сохранённые значения остаются.
F12 теперь использует user_path/screenshots вместо cwd/screenshots;
--screenshot с явным путём сохраняет прежнее поведение.

Проверки:

- Release/CTest **20/20 PASS**, в том числе SDL virtual controller, короткий
  огонь, одновременные оси/бег/огонь, дверь/оружие, карта/пауза, современный
  пресет, INI migration/custom/roundtrip, первый запуск и сброс видео.
- **36 запусков готового пакета PASS** в check_launch_paths.py. Четыре новых
  случая: F12 portable, explicit, кириллица/пробелы и явный --screenshot.
  PNG проверены по сигнатуре, cwd/screenshots не создаётся.
- **8 packaged запусков PASS**, три игры и автообнаружение на GL/software.
- SHA-256 исходных RES до/после совпали, git diff --check PASS.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File ports/sdl2/tools/build.ps1 -Configuration Release -DirectCommands
powershell -NoProfile -ExecutionPolicy Bypass -File ports/sdl2/tools/package.ps1 -PackageName windows-playtest-ogdok
py -3.14 -B -X utf8 ports/sdl2/tools/check_launch_paths.py --app artifacts/sdl2/windows-playtest-ogdok/OGDoK.exe
py -3.14 -B -X utf8 ports/sdl2/tools/check_package.py artifacts/sdl2/windows-playtest-ogdok --hidden
git diff --check
```

Пакет: artifacts/sdl2/windows-playtest-ogdok/OGDoK.exe, SHA-256
`d27f5388e01cb4e15e31cf671ee83ac0c3f6e3f138a9eda4426c95d0806da930`.
Доказательства: research/evidence/source-port/sp-classic-pad/ctest.log,
launch-paths.json и package.json. Тестовые временные профили/снимки launch
runner очищает штатно, сохраняет отчёт с логами; это не утрата DOS-материалов.
Обновлены input-audit.md, menu-settings.md, README.txt, plan/handoff.
Физический геймпад, полный маршрут игры и Linux/ARM не проверялись.
GBA/NDS и исходные EXE не затронуты. Следующий ID — SP18: ручная Windows-приёмка.
