# SP-FILTERS — фильтрация и аутентичность сохранений

Статус: Windows-реализация и программная приёмка завершены 6 ноября 2026 года.
Основание — прямой запрос пользователя.

Последующее сокращение похожих GL-режимов до Nearest/Smooth выполнено в
[SP-FILTERS-UX](SP-FILTERS-UX.md). Ниже сохранён результат исходной реализации
и её проверок; актуальный список описан в [контракте](../spec/filters.md).

## Цель и границы

Реализовать продолжение SP08: mipmaps, трилинейную фильтрацию, опциональную
анизотропию и один экспериментальный xBR. В геймплее «Сохранения» должны
быть выключателем сохранения и загрузки, включая быстрые команды и смерть.
Существующие файлы сохраняются. GBA/NDS, оригиналы и симуляция не меняются.

## Материалы и контракты

Прочитаны plan.md, handoff.md, SP08, SP17, SP22, spec/video-windows.md.
Основа — ports/sdl2/src/{gl_renderer,display,settings,frontend,main}.cpp.
Это современные опции порта, нового анализа EXE нет; адреса оригиналов
не применяются. Цвета mipmaps строятся после палитры и каждой строки LIGHT,
прозрачность смешивается в premultiplied RGB. Индексы не усредняются.
Nearest остаётся исходным значением. Старые INI читаются без сброса.
«Сохранения» сохраняются в профиле; выключение блокирует чтение и запись
игрового состояния, не удаляет слоты и не блокирует запись настроек.

## Работы и приёмка

1. Добавить цветовые mipmaps и выбор GL-фильтров, проверить LIGHT/alpha/cache.
2. Добавить экспериментальный xBR, проверить края и отдельный интерфейс.
3. Добавить сохраняемый выключатель и guards во всех путях сохранений.
4. Release/CTest, GL pixel probes, процессы трёх игр и обоих рендереров,
   RU/EN, перезапуск, нецелый масштаб; обновить плейтест в artifacts/sdl2.
5. Обновить паспорт, план и handoff с командами, результатами и ограничениями.

## Результат

База: `426c258bcc729696fa8b23e430866dc8ff6359ed`, исходное рабочее дерево чистое.
Реализованы Mipmaps/Trilinear/Anisotropic 2×/4×/8×, один экспериментальный
xBR-lv2, независимые настройки/CLI/F4 и сохраняемый выключатель сохранений.
Подробные алгоритмы, владение кэшем, LIGHT/alpha и ограничения —
[spec/filters.md](../spec/filters.md). Нового анализа оригинальных EXE нет.
Контракт отсутствия сохранений в DOS здесь принят из прямого уточнения
пользователя; новый независимый DOS-аудит этим изменением не заявляется.

Проверено:

- Release/CTest **20/20 PASS**: дополнены frontend/settings и отдельные
  CPU проверки цветовых/прозрачных/нечётных/прямоугольных mipmaps и xBR.
- GL pixel probe PASS: пять новых режимов усредняют RGB, immediate/deferred
  совпадают, палитра инвалидирует кэш, прозрачность не пишет глубину и не
  затемняется дважды; Anisotropic 4× сохраняет детали короткой оси footprint.
  Прежние nearest/linear, Sharp, FXAA, viewport/FBO проходят тот же probe.
- **124 реальных процесса на готовом пакете PASS**: семь карт, оба backend,
  RU/EN, все фильтры, FXAA/нецелый размер/поворот, отдельная неизменная панель;
  выключение/перезапуск/включение сохранений с сохранением исходных файлов.
- **68 процессов SP17 PASS**: ручные/быстрые слоты, межуровневая загрузка,
  перезапуск/перенос, ошибки/backup/атомарность, карта и компактный UI.
- Настоящие **PKR death и Game Over, software, 2/2 PASS**: в профиле есть
  валидный quicksave, выключение блокирует F9, HP/phase/menu и байты файла
  остаются прежними. Переходы остальных outcome-меню покрыты frontend guards;
  новые естественные death/Game Over для OG/PKE здесь не запускались.
- **8/8 packaged запусков PASS**; генератор локализации `--check` и
  `git diff --check` PASS. SHA-256 трёх исходных RES до/после совпадают.
- Quick benchmark: PKR LEVEL1–3 × три позы × семь GL-фильтров, 1024×768,
  warmup2/samples5, всего 63 случая. RTX 5070: худший p50 total по фильтрам
  0,302/0,395/0,445/0,443/0,411/0,445/0,449 мс в порядке меню.
  Это локальный benchmark без FXAA/present, не измерение полного runtime FPS
  и не прогноз для RK3326. Первичная генерация атласов в warmup не включена.

Просмотрены реальные кадры PKR Anisotropic 8×, xBR и компактный RU-геймплей.
xBR заметно меняет мелкие текстуры травы, поэтому остаётся экспериментальным.
Выбранные контрольные кадры сохранены; временные профили/остальные кадры удалены
самим checker по существующей политике очистки.

Готовый пакет обновлён: `artifacts/sdl2/windows-playtest-ogdok/OGDoK.exe`,
SHA-256 `b115095350944dfb87cf3b903cf46b6a8a4eea4d766c31908eae4a2ec13dd503`.
README и MIT-лицензия xBR поставляются в пакете. Это плейтест, не Windows v1.

Команды:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File ports/sdl2/tools/build.ps1 -Configuration Release -DirectCommands
build/sdl2/windows/dok_gl_probe.exe
py -3.14 -B -X utf8 ports/sdl2/tools/check_saves.py --out research/evidence/source-port/sp-filters-saves
powershell -NoProfile -ExecutionPolicy Bypass -File ports/sdl2/tools/package.ps1 -PackageName windows-playtest-ogdok
py -3.14 -B -X utf8 ports/sdl2/tools/check_package.py artifacts/sdl2/windows-playtest-ogdok --hidden
py -3.14 -B -X utf8 ports/sdl2/tools/check_filters.py --app artifacts/sdl2/windows-playtest-ogdok/OGDoK.exe
py -3.14 -B -X utf8 ports/sdl2/tools/check_save_outcomes.py --app artifacts/sdl2/windows-playtest-ogdok/OGDoK.exe --dataset ru --backend software --saves-disabled --out research/evidence/source-port/sp-filters-outcomes-final
# OUT должен ещё не существовать; повтор benchmark — новый путь опыта.
build/sdl2/windows/dok_render_bench.exe originals/pkr/RUS/PKR.RES research/evidence/source-port/sp-filters-bench/aniso8 all gl 1024 768 aniso8 quick 2 5
py -3.14 -B -X utf8 ports/sdl2/tools/generate_localization.py --check
```

Полные отчёты: `research/evidence/source-port/sp-filters/report.json`,
`sp-filters-saves/report.json`, `sp-filters-outcomes-final/run-1793945826082106200/report.json`,
`sp-filters-bench/*/native.json`. Компактный итог:
`analysis/source-port/sp-filters-summary.json`. Build log: `build/sdl2/filters-build-final.log`.

Аппаратная приёмка GLES/RK3326, точность/стоимость на устройстве и пределы
атласов остаются SP20; Windows v1 остаётся SP18/SP19. Следующий ID — **SP18**,
начать с актуального общего списка ручных/аппаратных проверок. Для SP20 отдельно
проверить расширенные фильтры и доступность OES_standard_derivatives.
