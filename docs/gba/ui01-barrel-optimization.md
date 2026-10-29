# UI01: ускорение комнаты с бочками, 29 октября 2026

По пользовательскому плейтесту оптимизирована отрисовка у первого медного
ключа LEVEL1. Масштабирование уже было целочисленным nearest-neighbor;
новая реализация сохраняет выбор исходных пикселей, освещение, прозрачность,
перекрытия и разрешение. Новые текстуры и уменьшенные кадры не используются.
Предыдущие исправления [предыстории и потолочных уступов](ui01-playtest-fixes.md)
включены в итоговую ROM.

## Место и причина

В `research/evidence/runtime/ru/LEVEL1-runtime.json` ключ — object99,
TMAP `(42,57)`, четыре бочки object96 — `(42..45,56)`; координаты клеток
нулевые. Бочка class6 использует исходное изображение 63×66, scale5.
Контрольное удаление бочек только из диагностического мира экономило примерно
60–150 тысяч CPU cycles в выбранных ракурсах. В поставляемой карте бочки сохранены.

Просадка определяется не одной бочкой. При помещении настоящего героя рядом
активируются до пяти AI owners. У статичных предметов уже были столбцы текстур
и prelit-данные; кадры анимации до этой правки читались построчно и готовили
освещённый столбец в C даже для исходной тождественной LIGHT0-таблицы.
Поэтому основной дополнительный выигрыш дали анимированные спрайты рядом.

## Изменения

- `platform/rect_tiled.S`: один ARM-вызов на прямоугольный спрайт вместо
  C-цикла с вызовами на каждый столбец. Координата U вычисляется через Q32
  reciprocal с точной коррекцией остатка; V и крайние пиксели прежние.
  Сохраняются alpha, coverage, black alias и счётчики. Используются прежние
  развёрнутые самплеры; стековая капсула 96 B, постоянного кеша нет.
- `scripts/gba_animation_columns.py`: при сборке создаются точные транспонированные
  копии исходных кадров, с дедупликацией и метаданными непрозрачных столбцов.
  24 ресурса,157 записей,148 уникальных картинок; банк533120 B, индекс1888 B.
  Исходный пакет не изменяется. Это обмен размера ROM на скорость доступа.
- `src/renderer_animation_columns.inc`, `renderer.c`, `renderer_rect.inc`:
  кадр определяется по смещению внутри пакета и точным размерам. Неизвестные
  изображения используют прежний путь. LIGHT0 допускает прямое чтение столбца;
  остальные уровни освещения сохраняют обычное преобразование цвета.
- `prototype_gba_bsp.py`: принятые флаги `HW_RECT_BATCH` и
  `HW_ANIMATION_COLUMNS` включены в `hw_v128_entity_rect` для frontend.
  Предыдущие флаги сохранены как `hw_v128_rect_control`; ранее объявленные
  экспериментальные варианты не переопределяются.

## Замеры

mGBA libretro0.11-212-7a12d6d, исходные карты, максимальная PCM-частота22050Hz.
Базовая ROM: `c12fff7033d02c6bc6cd2ddfd3d0c65ec5880ce2419b6cf50ac497973ab86844`.
Итоговая ROM: `297be4687e30ce96d2ab86135d05ad11e19400d9233b49fd1d57797a05b04273`,
16551104 B. Обе локали включены.

| Клетка героя | FPS до | FPS после | Прирост |
| --- | ---: | ---: | ---: |
| `(42,54)` |12,46|15,82|26,9%|
| `(44,55)` |12,05|15,96|32,5%|
| `(46,56)` |12,36|15,35|24,2%|

Это по120 показанных кадров после шести кадров прогрева, RU/EN дают одинаковые
FPS. Герой действительно помещён в комнату, камера следует ему, AI работает,
ввод неподвижный; тестовое HP32767 предотвращает смерть. Разное время кадра
меняет развитие AI и позы врагов, поэтому живые кадры не используются как
попарный пиксельный эталон. Это воспроизводимые локальные сценарии, а не
минимум полного прохождения и не замер физической GBA. Audio underruns —0.

В64 замороженных ракурсах самой комнаты вся PPU-картинка, coverage и геометрия
совпали точно. Отношение CPU cycles после/до: минимум0,9451, медиана0,9773,
максимум1,0013. Без живых врагов ускорение гораздо скромнее.

В880 распределённых замороженных ракурсах трёх уровней/RU/EN совпали PPU,
foreground, coverage и renderer stats. CPU after/before:0,9301 /0,9942 /1,0079
(min/median/max). Небольшие локальные замедления сохранены в отчётах;
универсальное ускорение каждого кадра и стабильные30FPS не заявляются.

EWRAM261444 B, свободно700 B — без роста. IWRAM code/data24324 B
(было24576 B); отдельно резерв8192 B под стеки/BIOS. Наблюдавшийся main stack
2576 B, запас5360 B; IRQ80 B, запас80 B. Эти значения относятся к пройденным
сценариям, не являются доказательством всех возможных состояний игры.

## Проверки и воспроизведение

Выходы — `build/gba/ui01-barrel-optimization/`, ELF/map/ROM —
`hardware/hw_v128_entity_rect/`. Перед сборкой нужны собственные оригиналы
и подготовленный пакет по `docs/building.md`. Zig0.13.0 берётся из общей
установки пользователя. Python3.14; для ARM/x86-векторов используется общая
Unicorn-библиотека `C:/Source/wasdify/build/research-deps`.

```powershell
py -3.14 -X utf8 ports/gba/scripts/build_gba_frontend.py --out build/gba/ui01-barrel-optimization
$candidate = 'build/gba/ui01-barrel-optimization/hardware/hw_v128_entity_rect'
$baseline = 'build/gba/ui01-playtest-fixes/hardware/hw_v128_entity_rect'
$reports = 'build/gba/ui01-barrel-optimization'
py -3.14 -X utf8 ports/gba/scripts/verify_performance_arm.py --build-dir $candidate --report "$reports/arm.json"
py -3.14 -X utf8 ports/gba/scripts/verify_animation_columns_gba.py --build-dir $candidate --reference-build $baseline --report "$reports/animation-ppu.json"
py -3.14 -X utf8 ports/gba/scripts/benchmark_barrels_gba.py --build-dir $candidate --reference-build $baseline --report "$reports/barrels.json"
py -3.14 -X utf8 ports/gba/scripts/benchmark_gba_hardware.py --details --languages 0 1 --build-dir $candidate --reference-build $baseline --report "$reports/hardware.json"
foreach ($lang in 0,1) {
  py -3.14 -X utf8 ports/gba/scripts/benchmark_gba_spatial_sweep.py --verify-output --language $lang --build-dir $baseline --report "$reports/sweep-before-$lang.json"
  py -3.14 -X utf8 ports/gba/scripts/benchmark_gba_spatial_sweep.py --verify-output --language $lang --build-dir $candidate --report "$reports/sweep-after-$lang.json"
}
```

ARM:4096 sampler,512 rectangle (341 cropped,98 raw-column),302 исходных
локализованных кадров с новым банком,157 индексных записей; ABI/стек и остальные
существующие векторы прошли. Ещё шесть неиспользуемых локализованных кадров
сохраняют fallback. Все308 исходных кадров из48 локализованных ресурсов
проверены на двух расстояниях через настоящую PPU mGBA:616 точных совпадений.

Дополнительно прошли host UI; Linux/Thumb compilation;396 PPU/HUD/weapon cases;
48 static-cache mutation cases; sprites,28 AI/combat cases, flow/UI/latency;
54 выхода из предыстории; исходные ceiling UV/7496 pixels;144 water phases;
294 перехода/5002 callbacks без смешанных кадров. Для этих скриптов использованы
`--build-dir $candidate --report "$reports/<name>.json"`; transitions также
`--mode4-transitions`, cache также `--reference-build $baseline`.
Имена отчётов и SHA-256 исходников/скриптов/отчётов сохранены в
`analysis/gba/ui01-barrel-optimization-20261029.json`.

Нового анализа EXE не проводилось. Происхождение потолочных x86-доказательств
и SHA оригиналов — в предыдущем документе. Физическая GBA и полное прохождение
не проверены. G21 остаётся в работе, бюджет G20 открыт; следующий ID — G21,
затем G22. NDS/SDL2 этой правкой не затронуты.
