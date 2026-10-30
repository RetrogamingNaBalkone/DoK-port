# GBA-порт

Независимый порт «Подземелий Кремля». [План](../../docs/gba/plan.md), [handoff](../../docs/gba/handoff.md), [паспорта](../../docs/gba/tasks/README.md).

- `src/`, `include/` — C11-модули порта.
- `platform/` — аппаратная реализация GBA, ASM, startup и linker script.
- `tests/` — проверки модулей на компьютере и общие тестовые сценарии.
- `tools/` — C-инструменты и диагностические программы этого направления.
- `scripts/` — подготовка ресурсов, сборка, измерения и проверки GBA.

PowerShell-драйверы определяют корень репозитория по своему расположению. Отдельные Python-команды запускать из корня репозитория. Результаты находятся в `build/gba/`, выбранные ROM — в `artifacts/gba/` вне Git. SDK хранится в общей пользовательской установке.

Сборка игрового frontend по умолчанию использует защищённую перенастройку
построчного DMA в VBlank:

```powershell
py -3.14 -X utf8 ports/gba/scripts/build_gba_frontend.py --out build/gba/frontend
```

ROM — `build/gba/frontend/hardware/hw_v128_vblank_only/dok-diagnostic.gba`.
Предыдущий ранний вывод Beta 1 сохранён для экспериментов явным параметром
`--variant hw_v128_entity_rect`; он вызывал зависание на физической GBA.
Результаты аппаратного опыта и ограничения —
[в диагностике G21](../../docs/gba/beta1-hardware-freeze.md).

Общие сведения об оригиналах находятся в `docs/originals/`, данные — в `analysis/originals/`. GBA-реализация не служит общим движком других портов.
