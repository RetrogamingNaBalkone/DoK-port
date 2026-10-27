# DoK-port: «Подземелья Кремля»

Главная цель — GBA-порт «Подземелий Кремля» на основе анализа оригинальных
PKR.EXE (RUS) и PKE.EXE (ENG). Windows/Linux используются для инструментов
и проверки логики. SDL2 и PortMaster остаются последующими целями.

Работа разбита на отдельные чаты: [позадачный план](docs/gba-plan.md),
[текущая передача контекста](docs/handoff.md), [паспорта задач](docs/tasks/README.md).

Выполнены первичная декомпиляция и разбор контейнера RES. Добавлены собираемые
модули C11 для ресурсов, изображений, анимаций, звука, LEVEL, подготовки мира
и части native-состояния. Собираемого игрового
движка и SDL2/GBA-портов пока нет. Декомпилятор не восстанавливает исходные имена,
комментарии, классы и точные типы автоматически.

## Результаты

- [Декомпиляция PKR](decompiled/ru/decompiled.c) и [PKE](decompiled/en/decompiled.c).
- [PKR с типами и глобальными символами](decompiled/ru/with_types.c),
  [PKE с типами и глобальными символами](decompiled/en/with_types.c).
- Отдельные функции: `decompiled/ru/functions/` и `decompiled/en/functions/`.
- В каждом каталоге: `listing.asm`, `functions.tsv`, `references.tsv`, `blocks.tsv`,
  `unrecovered.tsv`, `summary.txt`, `ghidra.log`.
- Сохраняемые проекты Ghidra: `research/ghidra/projects/ru/Kremlin.gpr` и
  `research/ghidra/projects/en/Kremlin.gpr`. Они содержат оригинальные версии без ASM-патчей.

Исходные игровые файлы, полные экспорты, проекты Ghidra и рабочие копии исключены
из Git через `.gitignore`. Скрипты, небольшие таблицы анализа и Markdown-документация
предназначены для хранения в репозитории. Git-репозиторий здесь пока не инициализирован.

## Документация

- [Результаты и ограничения декомпиляции](docs/decompilation.md).
- [Формат EXE, адреса и различия версий](docs/binary-format.md).
- [Что перенесено из исследования WASDify](docs/wasdify-findings.md).
- [Воспроизведение анализа](docs/reproducing.md).
- [RES, изображения и различия ресурсов RUS/ENG](docs/resources.md).
- [Частичные структуры игрока и событий](docs/native-state.md).
- [Формат анимаций, декодер и проверенные кадры](docs/animations.md).
- [Звуковые ресурсы, PCM-декодер и WAV](docs/sound.md).
- [Грамматика LEVEL](docs/levels.md) и [подготовка мира](docs/world.md).
- [Параметры врагов](docs/enemy-parameters.md), [сводные таблицы](docs/enemy-tables.md),
  [оружие и снаряды](docs/weapon-parameters.md), [различия версий](docs/level-version-comparison.md).
- [Сборка C-модулей и VS Code](docs/building.md).

Существенные результаты исследования фиксируются в Markdown. Для каждого вывода
нужно указывать образец, систему адресов, доказательства и степень уверенности;
гипотезы не должны превращаться в подтверждённые структуры или поведение порта.
