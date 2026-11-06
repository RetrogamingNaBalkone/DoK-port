# SP-FILTERS-UX — отбор заметно различающихся режимов

Статус: выполнено, 6 ноября 2026. Прямой запрос пользователя после SP-FILTERS:
сократить похожие режимы фильтрации до вариантов с заметным назначением.

## Цель и граница

Сравнить прежние режимы на одинаковых сценах и оставить компактный выбор
в меню/F4/CLI. Сохранить чтение прежних INI и CLI без сброса настроек.
Software и GL сравниваются отдельно: масштабирование кадра и фильтрация
текстур имеют разные назначения. Выключатель сохранений не меняется.
GBA/NDS и оригиналы не затрагиваются; нового анализа EXE нет.

## Материалы и контракты

Прочитаны актуальные plan/handoff, SP-FILTERS и реализация settings/frontend/main.
Контрольные данные: research/evidence/source-port/sp-filters-bench — PKR,
1024×768, LEVEL1–3 × initial/heading45/fractional, все семь прежних GL-режимов.
Дополнительно сравнить software при нецелом увеличении и движение GL.
Проверять заметность в контексте сцены, числовые различия не считать сами по
себе доказательством видимого улучшения.

## Работы и приёмка

1. Зафиксировать сравнения и обоснованный итоговый список.
2. Сократить меню/F4, обеспечить миграцию INI и старых CLI-имён.
3. Release/CTest, переключения/перезапуск, реальные кадры трёх игр,
   обновление готового пакета в artifacts/sdl2.
4. Обновить паспорт, план и handoff с командами и ограничениями.

## Результат и обоснование

База: `3bf64a43cd36783ff9a82b89820668155ec280e8`, исходное дерево чистое.
Убраны отдельные пользовательские GL Linear/Mipmaps/Trilinear/Anisotropic
2×/4×/8×. Остались **Nearest / Smooth**. Smooth использует билинейную
фильтрацию при увеличении, трилинейные mipmaps и анизотропию 8× при уменьшении.
Промежуточные режимы сохранены в низкоуровневом GlRenderer/диагностическом
benchmark для воспроизводимости сравнений, но не являются пунктами меню/F4.

Сравнены девять одинаковых замороженных сцен PKR из исходного SP-FILTERS:
LEVEL1–3, исходная камера / 45° / fractional, 1024×768. Средняя разница RGB
на канал по шкале 0..255:

| Пара | Средняя разница |
|---|---:|
| Nearest / Linear | 2,634 |
| Linear / Anisotropic 8× | 0,251 |
| Mipmaps / Trilinear | 0,210 |
| Anisotropic 2× / 4× | 0,058 |
| Anisotropic 4× / 8× | 0,013 |

Просмотрены полные кадры Linear/Trilinear/Anisotropic 8× для LEVEL1 45°.
Разница промежуточных ступеней мала и не оправдывает отдельный пользовательский
выбор. Smooth заменяет все варианты сглаживания одной настройкой; Nearest
сохраняет отдельный заметный характер исходных пикселей. Цифры выше —
измерения различий, не метрика качества или универсальный порог заметности.

Software оставлен Nearest/Linear/Sharp Bilinear/xBR: при увеличении кадра
320×200 до 853×641 эти режимы различаются по характеру картинки. Отдельные
контрольные процессы используют одинаковые логические replay и фиксированный
шаг времени. Между Linear/Sharp средняя разница канала сцены 1,974–2,486,
Sharp/xBR — 1,687–2,033 в трёх играх. Просмотрены четыре кадра PKR:
Nearest сохраняет резкие блоки; Linear размывает; Sharp сохраняет блоки с
узкими переходами; xBR перестраивает контуры и заметно меняет траву.
На выводе без увеличения Sharp/xBR могут не давать отдельного эффекта —
это ограничение их назначения, а не основание добавлять похожие GL-ступени.

Старые INI: любой `linear=1` или `mip_filter=1..5` становится Smooth,
Nearest 0/0 сохраняется. Запись нормализована к `linear=1,mip_filter=5` для
Smooth или 0/0 для Nearest. Другие настройки, в том числе saves_enabled,
сохраняются. CLI: `--filter nearest|smooth`; прежние шесть имён сглаживания
принимаются как aliases Smooth. `--frame-filter` не изменён.

## Проверки и поставка

- Release/CTest **20/20 PASS**, в том числе 12 сочетаний старых INI-ключей,
  сохранение/перезапуск и независимость других настроек, меню и обратный цикл.
- **60 процессов** замороженных сравнений PASS: три игры, старые CLI aliases
  дают побайтово тот же кадр, что Smooth; F4 действительно имеет два значения;
  четыре software-варианта проверены статично и с детерминированным поворотом.
- **82 процесса** общей фильтрационной матрицы PASS: все семь карт, оба
  backend, независимый HUD, FXAA/нецелый размер/повороты, выключение/перезапуск/
  включение сохранений, компактный RU/EN геймплей.
- **8 packaged запусков PASS**, генератор локализации `--check` и
  `git diff --check` PASS. SHA-256 трёх исходных RES до/после совпадают.

Пакет обновлён: `artifacts/sdl2/windows-playtest-ogdok/OGDoK.exe`.
SHA-256 `5bca516b0d58f5a33c68ab9b88ce4b5f34449886b3bf38cd058706b412333be1`.

```powershell
py -3.14 -B ports/sdl2/tools/compare_filter_choices.py research/evidence/source-port/sp-filters-bench research/evidence/source-port/sp-filters-ux/comparison.json
powershell -NoProfile -ExecutionPolicy Bypass -File ports/sdl2/tools/build.ps1 -Configuration Release -DirectCommands
py -3.14 -B -X utf8 ports/sdl2/tools/check_filter_choices.py
py -3.14 -B -X utf8 ports/sdl2/tools/check_filters.py --out research/evidence/source-port/sp-filters-ux/matrix
powershell -NoProfile -ExecutionPolicy Bypass -File ports/sdl2/tools/package.ps1 -PackageName windows-playtest-ogdok
py -3.14 -B -X utf8 ports/sdl2/tools/check_package.py artifacts/sdl2/windows-playtest-ogdok --hidden
py -3.14 -B -X utf8 ports/sdl2/tools/generate_localization.py --check
git diff --check
```

Полные отчёты — `research/evidence/source-port/sp-filters-ux/{comparison,choices}.json`
и `matrix/report.json`; четыре выбранных software-кадра там же.
Build log: `build/sdl2/filters-ux-build.log`. Компактный итог:
`analysis/source-port/sp-filters-ux-summary.json`. Временные профили/кадры
удалены checker по существующей политике, исходные frozen captures сохранены.

Алгоритмы не изменялись, новые замеры производительности не требовались.
Windows v1 не объявлена принятой. Следующий ID — **SP18**, актуальный список
ручных/аппаратных проверок; GLES/RK3326 и стоимость Smooth на устройстве — SP20.
