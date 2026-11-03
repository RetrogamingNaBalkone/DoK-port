# PERF06 — Упрощение дальнего окружения

Статус: эксперимент завершён 2 ноября 2026. Пользователь выбрал опубликованную
Beta2 (G22+UI02, 120×128); PERF04–PERF06 в выбранную версию не включаются.
Следующий открытый ID — G21. Без нового запроса оптимизации не продолжать.

## Границы и реализация

Входы: GBA plan/handoff/backlog, PERF04/PERF05. По запросу пользователя проверены
отдельно и вместе однотонный дальний потолок/уступы и упрощённые решётки.
Разрешение 120×128 сохранено: пользователь явно отверг его снижение.
Карты, AI, коллизии и предметы не изменены. NDS/SDL2 не затронуты.

Контроль: `build/gba/perf04/final/hardware/perf04_full8`, SHA
`19e78f84234872b035de7673cb6eaf2ee877623be66c44cd936851351141659c`.

Варианты: `perf06_ceiling`, `perf06_fences`, `perf06_both`.
Порог camera-space depth 1440 (три клетки). Дальний потолок и уступы однотонные,
освещение и coverage сохранены. У решёток glyph75 (LEVEL2 object76) пять
вертикальных полос заменены тремя, четыре горизонтальные сохранены, цвет плоский.
Ближняя детализация сохраняется. Порог жёсткий: возможен скачок детализации
при движении и поворотах. ARM IWRAM kernels рисуют spans/columns и колонку
решётки за один вызов. Первый C-вариант с вызовом на перекладину оказался
медленнее; итоговые результаты относятся к fused-сборке.

## Результаты

Живой stationary-прогон RU, 120 предъявлений кадра, без стрельбы:

| Сцена | PERF04 | Потолок | Решётки | Вместе |
|---|---:|---:|---:|---:|
| Вход 50,41 /270 |14,90|14,90|14,90|14,90|
| Диагональ 51,41 /315 |10,08|10,81|10,17|10,83|
| Медный ключ 14,48 /90 |14,08|14,16|14,08|14,16|

Со стрельбой PERF04 → вместе: вход 13,42 →13,63; диагональ 9,20 →9,21;
ключ 11,41 →11,56 FPS. EN практически повторяет результат. При разной частоте
кадров состояния боя могут расходиться; это не одинаковая боевая траектория.
Лучший живой пример +7,4%, не ×1,5. На входе заметного эффекта нет.

Spatial sweep: 64 позы LEVEL2 ×2 языка на вариант, 30 samples, 384 пары.
В 66 тяжёлых позах (<15 FPS) медиана изменения CPU: потолок −1,86%, решётки
+0,52%, вместе −1,76%; диапазон вместе −4,63…+0,21%. Снижение CPU не всегда
пересекает порог следующего VBlank. Решётки не оправдывают потери изображения.

## Проверки и воспроизведение

```powershell
py -3.14 -X utf8 ports/gba/scripts/prototype_gba_bsp.py --base build/gba/perf04/final/base --out build/gba/perf06/fused --variants perf06_ceiling perf06_fences perf06_both
py -3.14 -X utf8 ports/gba/scripts/check_perf06.py --stage spatial
py -3.14 -X utf8 ports/gba/scripts/check_perf06.py --stage quality
py -3.14 -X utf8 ports/gba/scripts/check_perf06.py --stage live
py -3.14 -X utf8 ports/gba/scripts/check_perf06.py --stage checks
py -3.14 -X utf8 ports/gba/scripts/check_perf06.py --stage summary
py -3.14 -X utf8 ports/gba/scripts/prototype_gba_bsp.py --base build/gba/perf04/final/base --out build/gba/perf06/control --variants perf04_full8
```

Frontend/time/audio/flow/sprites/hardware transitions прошли на combined;
1536 независимых Unicorn vectors проверяют три ARM kernel, coverage, границы
буфера и сохранение регистров. Quality: 50 камер на вариант, включая короткие
смещения по 60 units, RU/EN; lease hashes exact, coverage потолка exact.
Маска решёток намеренно другая. Визуально просмотрены вход и диагональ:
патроны и ближняя решётка сохранены, дальняя решётка явно беднее.
Полного прохождения и физической проверки GBA нет.

Контрольная PERF04 повторила прежний SHA после сохранения исходного порядка
вычисления vend в ветке без новых флагов (перенос в локальную переменную
менял кодогенерацию). Экспериментальные ветки от исправления не меняются.
Evidence/команды: `research/evidence/gba-perf06/`.
Исходники: `renderer_far_lod.inc`, `perf06_flat.S`, `gba_perf06_assets.py`,
`check_perf06.py`, `verify_perf06_arm.py`; флаги выключены в PERF04.

## Артефакты

- `artifacts/gba/DoK-port_PERF06_Environment.gba`: оба упрощения, 26 667 532 B,
  SHA `da541fa2869fb47e6bc0190fc4008fa0ea87b6504aac276374a062380b2851b0`.
- `artifacts/gba/DoK-port_PERF06_Ceiling.gba`: только потолок,
  SHA `8be488bb19388a1968cf76c61a5651e10abb1b008e4e17a9c880cc592454e0af`.
- `artifacts/gba/PERF06-before.png`, `PERF06-after.png`: диагональ комнаты.

Together IWRAM 24440 B, свободно 136 B при stack reserve 8192 B;
EWRAM 261476 B, свободно 668 B. Ceiling IWRAM 23740 B.
Это экспериментальные ROM, не новый релиз. Beta2 не заменена.
