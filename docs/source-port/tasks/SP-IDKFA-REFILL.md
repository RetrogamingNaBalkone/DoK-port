# SP-IDKFA-REFILL — пополнение патронов при каждом IDKFA

Статус: выполнено, 6 ноября 2026.

## Цель и граница

По запросу пользователя каждый IDKFA заполняет текущий запас патронов
до действующего максимума в Old Gold, PKR и PKE. Повторный ввод не увеличивает
вместимость и не сбрасывает анимацию или переключение оружия.

## Материалы и контракт

Прочитаны правила проекта, Source Port plan/handoff, weapons.cpp, items.cpp,
PlayerState и существующие idkfa_tests/hud_desktop_probe. Исходное дерево чистое,
HEAD `063726d6370e592cfd3dc008a70a41deaa00e98e`.
grant_all выдаёт отсутствующие слоты с обычным бонусом подбора оружия, затем
устанавливает attributes[1] = max_charge(). Ключи и сообщение сохраняются.
Это запрошенное изменение поведения порта; соответствие DOS-читу не заявлено.
Нового анализа EXE нет, оригиналы и другие порты не меняются.

## Работы и приёмка

1. Проверить неполный и нулевой запас, повторный ввод при полном запасе,
   неизменность максимума, здоровья и выбранного оружия на семи уровнях.
2. Обновить существующие проверки повторного IDKFA во время стрельбы и смены.
3. Выполнить Release/CTest, целевые сценарии для трёх RES, обновить текущий
   Windows-пакет и проверить его запуск.
4. Записать результаты и команды в паспорт, план и handoff. Далее — SP18.

## Дополнение пользователя: поставляемая инструкция

README переведён в TXT; таблицы заменены параметрами с описаниями на отдельных
строках. Удалены границы плейтеста, внутренние статусы/паспорта, старый список
исправлений и отдельный раздел с правилами Old Gold. Сохранены запуск,
управление, настройки, сохранения, CLI и сведения для сообщения об ошибке.
Источник — ports/sdl2/README.txt; PLAYTEST.md оставлен ссылкой для прежних
ссылок репозитория. package.ps1 поставляет README.txt и удаляет старый README.md;
check_package.py проверяет новый формат и отсутствие старой инструкции.

## Результат и воспроизведение

- Release/CTest: 20/20 PASS.
- dok_idkfa_tests: 147 сценариев семи уровней при 30/60/144 FPS PASS;
  дополнительно нулевой/частичный/полный запас на каждом уровне. Максимум,
  здоровье и выбранное оружие сохраняются при повторе.
- check_idkfa_refill.py: 6 процессов готового EXE, три игры × GL/software,
  реальный SDL-ввод кода → выстрел → пополнение → повтор при полном запасе PASS.
  SHA-256 исходных RES до/после совпали. Логи и отчёт:
  research/evidence/source-port/sp-idkfa-refill/.
- check_package.py: 8 запусков готового пакета PASS, проверены UTF-8 README.txt,
  полнота CLI, отсутствие Markdown-таблиц и README.md. git diff --check PASS.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File ports/sdl2/tools/build.ps1 -Configuration Release -DirectCommands
build/sdl2/windows/dok_idkfa_tests.exe originals/Old_Gold_3D/OLD_GOLD.RES originals/pkr/RUS/PKR.RES originals/pkr/ENG/PKE.RES
powershell -NoProfile -ExecutionPolicy Bypass -File ports/sdl2/tools/package.ps1 -PackageName windows-playtest-ogdok
py -3.14 -B -X utf8 ports/sdl2/tools/check_idkfa_refill.py
py -3.14 -B -X utf8 ports/sdl2/tools/check_package.py artifacts/sdl2/windows-playtest-ogdok --hidden
git diff --check
```

Готовый пакет: artifacts/sdl2/windows-playtest-ogdok/.
OGDoK.exe SHA-256: `2c4b10802b8b223171e9265a30db35777e6659328de217fa2d3ebdc63faa87be`.
Нового DOS-исследования, полного прохождения и аппаратной/Linux/ARM-приёмки
нет. Это отдельная адаптация чита по запросу пользователя. Следующий ID — SP18;
первый шаг — ручная Windows-приёмка текущего пакета. GBA/NDS не затронуты.
