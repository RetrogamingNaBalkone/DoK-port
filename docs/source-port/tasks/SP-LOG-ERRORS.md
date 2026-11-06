# SP-LOG-ERRORS — только ошибки в журнале по умолчанию

Статус: выполнено, 6 ноября 2026.

## Цель и граница

Обычный запуск OGDoK пишет в port.log только ошибки. Подробная диагностика
включается явно через --verbose, включая автоматические проверки.
Журнал перезаписывается при запуске. Игровые правила не меняются.
По последующим замечаниям пользователя README описывает только действующие
параметры, без перечня неподдерживаемых FOV/aspect и истории фильтров.
CLI --filter принимает только nearest|smooth; старые aliases удалены.
В AGENTS.md закреплён отказ от предрелизной обратной совместимости без
отдельного запроса пользователя. Это не массовая переработка всех форматов.

## Материалы и контракт

Прочитаны AGENTS.md, Source Port plan/handoff, SP-CLASSIC-PAD,
main.cpp, launch.hpp, README.txt и процессные проверки.
База ec7795d56a485ccad04cdf771a176b4298e00c9e, исходное дерево чистое.
Это политика диагностики SDL2; анализ оригинальных EXE не требуется.
Ошибка остаётся доступна при любом режиме, подробный поток выключен по умолчанию.
Пути профилей и port.log прежние, флаг не записывается в настройки.

## Работы и приёмка

1. Разделить ошибки и информационные сообщения, добавить --verbose.
2. Явно включить подробный журнал в существующих диагностических инструментах.
3. Проверить пустой журнал исправного запуска, ошибки без флага,
   подробности с флагом и перезапись при повторном запуске.
4. Собрать Release/CTest, обновить и проверить Windows-пакет.
5. Обновить паспорт, plan/handoff; следующий основной ID — SP18.

## Результат

Информационный поток main.cpp отделён от ошибок и выключен по умолчанию.
--verbose включает его только в текущем процессе. Ошибки ресурсов, настроек,
видео, аудио, сохранений и снимков продолжают записываться. Игровые события,
пути профиля, успешные операции и завершение доступны только с флагом.
Диагностические инструменты явно включают --verbose.

Release/CTest 20/20 PASS; 43 launch-paths сценария PASS, включая семь новых:
исправный запуск, включение подробностей, отсутствие сохранения флага,
обычная игра без подробностей, ошибка ресурса, fatal и перезапись журнала.
SHA-256 RES до/после совпали. Frontend-process PASS, 148 video frames PASS,
8 packaged запусков PASS. Python syntax и git diff --check PASS.

Команды воспроизведения из корня:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File ports/sdl2/tools/build.ps1 -Configuration Release -DirectCommands
py -3.14 -B -X utf8 ports/sdl2/tools/check_launch_paths.py
py -3.14 -B -X utf8 ports/sdl2/tools/check_frontend.py
py -3.14 -B -X utf8 ports/sdl2/tools/check_video.py
powershell -NoProfile -ExecutionPolicy Bypass -File ports/sdl2/tools/package.ps1 -PackageName windows-playtest-ogdok
py -3.14 -B -X utf8 ports/sdl2/tools/check_package.py artifacts/sdl2/windows-playtest-ogdok --hidden
git diff --check
```

Доказательства: build/sdl2/checks/launch-paths/report.json,
build/sdl2/checks/frontend-process/report.json,
research/evidence/source-port/sp-video-apply/ctest.log и
research/evidence/source-port/sp08-sp16-checkpoint/windows-playtest-ogdok/report.json.
Пакет обновлён: artifacts/sdl2/windows-playtest-ogdok/.
Последняя UI-правка пересобрана командами существующего Ninja-графа
для frontend.cpp, frontend_tests.cpp, библиотеки SDL и двух исполняемых целей;
остальные объекты взяты из полной Release-сборки. Финальный CTest 20/20 PASS.
Windows 8, физический геймпад и Linux/ARM здесь не проверялись.
Следующий основной ID — SP18: ручная Windows-приёмка.
