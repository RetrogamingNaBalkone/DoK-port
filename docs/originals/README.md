# Исследования оригиналов

Общая база восстановления трёх независимых портов. Документы сохраняют собственные ограничения и границы проверенного поведения.

- [Декомпиляция](decompilation.md), [воспроизведение анализа](reproducing.md), [формат EXE и адреса](binary-format.md).
- [RES](resources.md), [анимации](animations.md), [звук](sound.md).
- [LEVEL](levels.md), [подготовка мира](world.md), [различия уровней PKR/PKE](level-version-comparison.md).
- [Параметры врагов](enemy-parameters.md), [таблицы](enemy-tables.md), [оружие](weapon-parameters.md).
- [Исходная частота обновлений](original-cadence.md), [контрольные прогоны](reference-runs.md).
- [Old Gold](old-gold-version-comparison.md), [исследования WASDify](wasdify-findings.md).

Данные находятся в `analysis/originals/`; полные экспорты — локально в `decompiled/`, проекты и доказательства — в `research/`. Инструменты Ghidra размещены в `tools/ghidra/`, анализаторы ресурсов — в `tools/originals/`. Некоторые анализаторы используют диагностические программы GBA как проверочный инструмент; это не зависимость архитектуры NDS или SDL2.
