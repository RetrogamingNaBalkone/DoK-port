# Сборка первых модулей и VS Code

Сейчас собираются читалка RES, декодеры изображений/анимаций/звука,
загрузчик LEVEL, подготовка мира, адаптер native-состояния и тесты.
Приоритет — GBA: [позадачный план](gba-plan.md), первый ROM — задача G01.
Игрового GBA-ROM и исполняемого SDL2-движка пока нет.

## Один общий компилятор

По предпочтению пользователя компилятор установлен один раз:
`%LOCALAPPDATA%/Programs/Zig/0.13.0/zig.exe`.
Копии toolchain в проекте нет; временный ZIP после проверки и переноса удалён.
Общий кеш компилятора находится в стандартном пользовательском каталоге.
Проект хранит исходники, настройки и результаты сборки в `build/native/`.
Системный PATH не менялся.

Пакет получен с [официального сайта Zig](https://ziglang.org/download/):
`zig-windows-x86_64-0.13.0.zip`, SHA-256
`d859994725ef9402381e557c60bb57497215682e355204d754ee3df75ee3c158`.
Хэш проверен по официальному индексу перед использованием. Zig используется
как поставщик C-компилятора и системных библиотек; исходники остаются на C11.

Установленный Visual Studio 2026 содержит бинарные инструменты MSVC,
но проверка не обнаружила необходимых C-заголовков и Windows SDK;
`VsDevCmd` не дал работоспособную команду `cl`. Компоненты Visual Studio
в рамках этой задачи не изменялись.

## Windows

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build-native.ps1
# Дополнительно проверить компиляцию под Linux и ARM7TDMI:
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build-native.ps1 -CrossChecks
# При другом расположении общего компилятора:
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build-native.ps1 -ZigExe C:/Tools/Zig/zig.exe
```

Скрипт собирает C11 с `-Wall -Wextra -Werror` и запускает шесть тестовых EXE.
`build/native/dok-res.exe --list originals/pkr/RUS/PKR.RES` выводит каталог.
Проверка двух реальных архивов и выгрузка всех блоков:

```powershell
C:/Source/wasdify/.venv/Scripts/python.exe scripts/analyze_resources.py
# После готового первого этапа анализа Ghidra:
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/annotate-stage2.ps1
# Проверить и выгрузить анимации:
C:/Source/wasdify/.venv/Scripts/python.exe scripts/analyze_animations.py
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/annotate-stage3.ps1
# Проверить и выгрузить звук:
C:/Source/wasdify/.venv/Scripts/python.exe scripts/analyze_sounds.py
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/annotate-stage4.ps1
# Восстановить метки LEVEL в Ghidra и сверить определения и карты:
C:/Source/wasdify/.venv/Scripts/python.exe scripts/prepare_levels.py
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/annotate-stage5.ps1
C:/Source/wasdify/.venv/Scripts/python.exe scripts/analyze_levels.py
# Вывести модель уровня (строки CP866 представлены в hex):
./build/native/dok-level.exe build/resources/ru/0018.bin
# Создать начальный мир из оригинального архива:
C:/Source/wasdify/.venv/Scripts/python.exe scripts/prepare_world.py
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/annotate-stage6.ps1
C:/Source/wasdify/.venv/Scripts/python.exe scripts/analyze_worlds.py
./build/native/dok-world.exe originals/pkr/RUS/PKR.RES LEVEL1
```

Для `analyze_resources.py` нужен только Python 3.11+ и стандартная библиотека.
Путь venv выше — удобство текущей машины, а не зависимость C-модуля от WASDify.

## VS Code

Установлено общее расширение `ms-vscode.cpptools` (Microsoft C/C++, 1.34.4).
`.vscode/settings.json` ссылается на общий компилятор; `.vscode/tasks.json`
добавляет задачу **DoK: build and test C modules**. Запуск — `Ctrl+Shift+B`.
Сборка и исполнение проверены через тот же PowerShell-скрипт; интерактивная
отладочная сессия в редакторе пока не настраивалась и не проверялась.

Расширение само не заменяет C-компилятор; это ограничение описано в
[документации Microsoft](https://code.visualstudio.com/docs/languages/cpp).
При переносе проекта на другой компьютер путь можно переопределить.
Настройки `.vscode` рассчитаны на Windows; исходники C и CMake не зависят от них.

## Linux и GBA

Для обычного компилятора C11 добавлен `CMakeLists.txt`:

```sh
cmake -S . -B build/cmake
cmake --build build/cmake
ctest --test-dir build/cmake --output-on-failure
```

Сам запуск CMake в текущей среде не проверялся: CMake здесь не найден.
Проверены прямые команды компиляции тех же C-модулей. Получены статический
ELF x86_64 Linux для ресурсного инструмента и тестов native-состояния,
а также ARM7TDMI Thumb objects адаптера состояния, декодеров изображений
и анимаций/звука. Добавлены Linux-инструменты `dok-anim-linux` и
`dok-sound-linux`. Эти артефакты не запускались
на Linux/GBA. Объект ARM ещё не является GBA ROM: нет startup-кода, линковки
под память консоли, видеовывода, ввода и игрового цикла.

Итоговые контрольные суммы оригиналов и артефактов, число сверенных ресурсов
и аннотаций сохранены в [stage2-verification.json](../analysis/stage2-verification.json).

Проверка LEVEL, полей и ссылок: [stage5-verification.json](../analysis/stage5-verification.json),
[описание профиля и ограничений](levels.md).

Подготовка мира: [world.md](world.md), [stage6-verification.json](../analysis/stage6-verification.json).
Добавлены `dok-world-linux` и `world-arm7tdmi.o`; они только скомпилированы.
