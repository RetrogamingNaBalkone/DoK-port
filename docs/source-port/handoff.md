# Передача контекста Source Port

26 октября 2026 года. **OG01 и SP00 завершены; следующий ID — SP01.**
Это отдельная от GBA линия работы. Не добавлять её результаты в `gba-plan.md`
и не менять общий GBA handoff по факту работы над Old Gold.

Пользователь запросил отдельную декомпиляцию Old Gold из
`originals/Old_Gold_3D`, отчёт об исчезнувших особенностях и оценку общего
SDL2 Source Port для Old Gold, PKR и PKE. Подробные результаты — в README
этого каталога. Реализация SDL2 не запрашивалась в рамках этой задачи.

Готово: Ghidra 12.1.2/MZ real mode, 889/889 функций; отдельный проект
`build/ghidra/old-gold-full/OldGold.gpr`, экспорт `decompiled/old-gold/`.
Образцы не менялись; SHA-256 — `analysis/old-gold/verification.json`.
Там же хеши артефактов, метрики, предупреждения и повторный чистый импорт.
Первые пробные проекты и журнал диагностического исследования сохранены
локально в `build/ghidra/old-gold/` и `build/old-gold/`.

Ключевые адреса **MZ Ghidra**, не NE и не DPMI: фабрика `1ae1:0003`,
герой ctor/check/stats `1ae1:128f/15a2/18b9`, projectile reader `1ae1:21ea`,
monster hit `1ae1:32ac`, world prepare `21b8:0d90`, main `20c4:03b0`,
HUD `1f1d:0251/04c8/05c5`, Speaker IRQ `2b41:000f`, SB IRQ `2b41:0077`.
Роли и основание каждой метки — `analysis/old-gold/seeds.tsv`.

Перед автоанализом обязателен `PrepareOldGold.java`: четыре CS jump tables
иначе разрешаются неверно. В C после overrides остаются адресные case labels;
проверять оригинальные WORD-таблицы. 53 функции имеют внутренние предупреждения;
runtime `1000:04fc/0ef5` — ошибки p-code в журнале. Вывод 889/889 не равен
проверенному поведению или собираемому исходнику.

Все 82 записи RES прошли существующий native C-reader. Все 49 изображений,
12 анимаций и 10 SVM прошли независимую сверку C/Python. Оригинальный
LEVEL4 имеет 119 определений и 105 рублей. `dok-level.exe` для Kremlin
отвергает его на строке 115: старый projectile payload короче, без impact/miss
animations; это первая практическая граница SP01. Geometry 336/672 против 480/960.
Сэмплы звука OG: 0…64, 16000 Гц, терминатор 0; их акустическая интерпретация требует DOS.

Вывод SP00: общее ядро с тремя профилями реалистично. PKR/PKE отличаются
балансом и параметрами врагов, а не только текстом. Общие assets readers
можно использовать непосредственно; перенос игровой логики проверять
по каждому оригиналу. Не переносить ABI/far pointers или GBA RAM/ROM layout.

Команды:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/decompile-old-gold.ps1
C:/Source/wasdify/.venv/Scripts/python.exe scripts/analyze_old_gold_port.py
```

Для повторного чистого импорта — параметр `-ProjectDirectory` с новым каталогом
и `-OutputDirectory build/old-gold/recheck-export`. Основной и контрольный
экспорты сравнивает Python verifier. Полные производные материалы и оригиналы
в Git не добавлять. DOS-прохождение, акустическая сверка и SDL2 не проверены.
