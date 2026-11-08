# DoK-port

Три независимых направления восстановления игр по декомпиляциям, ASM и проверкам оригинального поведения:

| Направление | Код и инструменты | План | Текущий контекст |
|---|---|---|---|
| GBA: «Подземелья Кремля» | [ports/gba](ports/gba/) | [План](docs/gba/plan.md) | [Handoff](docs/gba/handoff.md) |
| Nintendo DS/DS Lite: «Подземелья Кремля» | [ports/nds](ports/nds/) | [План](docs/nds/plan.md) | [Handoff](docs/nds/handoff.md) |
| OGDoK (SDL2): Old Gold/PKR/PKE; Windows/Linux/PortMaster | [ports/sdl2](ports/sdl2/) | [План](docs/source-port/plan.md) | [Handoff](docs/source-port/handoff.md) |

GBA содержит реализацию и host-тесты.
Для Nintendo DS / DS Lite подготовлен
[Dungeons of Kremlin 1.0-RC1](https://github.com/RetrogamingNaBalkone/DoK-port/releases/tag/nds-1.0-rc1):
три уровня, RU/EN, две схемы управления и интерактивная карта на нижнем экране.
Игра поставляется одним `.nds`-файлом в ZIP.
[Особенности и управление](docs/nds/releases/1.0-RC1.md).
OGDoK — общий SDL2-порт Old Gold/PKR/PKE; текущие возможности и сборки описаны
в [плане](docs/source-port/plan.md) и [инструкции](ports/sdl2/PLAYTEST.md).
Завершение одного направления не требуется для работы над другим. Игровые статусы и следующий ID определяются
планом соответствующего порта.

## Структура

- `ports/` — код, тесты, инструменты и сборочные сценарии каждого порта.
- [docs/](docs/README.md) — планы, паспорта, описание восстановления и платформенных решений.
- [docs/originals/](docs/originals/README.md) — сведения об оригинальных EXE, ресурсах и поведении.
- [analysis/](analysis/README.md) — уникальные данные оригиналов и компактные результаты под контролем Git.
- `tools/ghidra/`, `tools/originals/`, `tools/reports/` — общие инструменты анализа и публикации сводок.
- `scripts/` — обслуживание репозитория и разрешение исторических путей.
- `originals/`, `decompiled/`, `research/`, `build/` — локальные материалы вне Git и поставки.
- `artifacts/<порт>/` — отобранные сборки; игровые ресурсы и ROM не включаются в Git.

Автоматический псевдокод хранится отдельно от редактируемых модулей и не считается собираемым или проверенным эквивалентом оригинала. Общий игровой движок между портами не выделен.

## Сборка и проверки GBA

Используются общие пользовательские установки инструментов, описанные в [инструкции сборки](docs/building.md).

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File ports/gba/scripts/build-native.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File ports/gba/scripts/build-gba.ps1 -Verify
```

Host-модули можно также собрать через CMake из корня или непосредственно из `ports/gba/`. Выходы сборки размещаются в `build/gba/`; исходные игровые данные предоставляет пользователь.

Перед работой читать [AGENTS.md](AGENTS.md), план, handoff и паспорт выбранного направления. [Перестройка репозитория](docs/tasks/RESTRUCTURE.md) описывает переносы, проверки и сохранение исторических доказательств.
