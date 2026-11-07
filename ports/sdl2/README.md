# OGDoK

Пользовательское имя приложения — **OGDoK** (`OGDoK.exe` в Windows).
Каталоги реализации, сборки и поставки сохраняют имя `sdl2`.

Независимый общий SDL2-порт Old Gold/PKR/PKE для Windows, Linux и PortMaster. [План](../../docs/source-port/plan.md), [handoff](../../docs/source-port/handoff.md).

Windows RC2 объединяет PKR/PKE/Old Gold, программную отрисовку и OpenGL2.
[Инструкция](README.txt), [изменения относительно RC1](../../docs/source-port/releases/OGDoK-Win-1.0-RC2.md).
«Геймплей → Исправление попаданий» опционально устраняет промахи у границ
клеток; по умолчанию выключено, сохраняется в профиле. Текущая поставка —
`artifacts/sdl2/OGDoK-Win-1_0-RC2/`, публичный ZIP без ресурсов оригиналов.
Стабильная Windows 1.0 и другие платформы остаются отдельными этапами.
Движок восстанавливается по оригиналам независимо от GBA и NDS.

Команды сборки и реальные результаты: [building](../../docs/source-port/building.md).
