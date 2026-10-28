# DoK-port

Три независимых направления восстановления игр по декомпиляциям, ASM и проверкам оригинального поведения:

| Направление | Код и инструменты | План | Текущий контекст |
|---|---|---|---|
| GBA: «Подземелья Кремля» | [ports/gba](ports/gba/) | [План](docs/gba/plan.md) | [Handoff](docs/gba/handoff.md) |
| Nintendo DS/DS Lite: «Подземелья Кремля» | [ports/nds](ports/nds/) | [План](docs/nds/plan.md) | [Handoff](docs/nds/handoff.md) |
| SDL2: Old Gold/PKR/PKE; Windows/Linux/PortMaster | [ports/sdl2](ports/sdl2/) | [План](docs/source-port/plan.md) | [Handoff](docs/source-port/handoff.md) |

GBA содержит реализацию и host-тесты. Для NDS и SDL2 подготовлены планы; реализация ещё не начата. Завершение одного направления не требуется для работы над другим. Игровые статусы и следующий ID определяются планом соответствующего порта.

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
