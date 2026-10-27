# Разрешение CPU-слоя на аппаратном рендере

27 октября 2026. Два отдельных прототипа; **универсальные15FPS не достигнуты,
G20/G20A/goal открыты**. Отчёт: `analysis/g20a-row-quality-trial.json`, сборщик:
`scripts/report_gba_row_quality.py`. Основной2775 автоматически не заменялся.

## Изменение и решение

Проверены120×96 и120×72 вместо120×128 программных отсчётов. Аппаратный пол и
базовый потолок остаются144-строчными; HUD/оружие сохраняют native240-wide
вывод. Пониженные потолки/уступы и их высоты сохранены.16 LIGHT rows вертикальных
текстур и отключение готовых масштабов сохранены. Игровая логика не менялась.

По просмотренным парным PPU кадрам96 строк — умеренный компромисс: добавилась
ступенчатость стен/потолков.72 строки заметно грубее и оставлены сравнением:
дополнительная уступка не устраняет L2<15FPS. Это визуальная оценка, не
доказательство неразличимости. Проверены в том числе тяжёлые L1cell3640225°
и L2cell1592135°. Старый128-кандидат, все прежние artifacts и оригиналы сохранены.

| Вариант | ROM SHA-256 | Размер |
|---|---|---:|
|96 |97f0b3cb1d97c958bb0750128cef72aa94f64fa4107a8edc53d743c38b557f81 |15 939 148B |
|72 |17696958e5d1f08672754f788861c31fdc6302d242f24f0b4a5223768688888a |15 939 260B |

ROM: `artifacts/gba/DoK-port_hardware_rows96_prototype.gba` и
`artifacts/gba/DoK-port_hardware_rows72_prototype.gba`. Пять тяжёлых видов каждого
разрешения: `artifacts/gba/row-quality/`, имена начинаются с128/96/72.
Например: `96-L2-cell1592-135-ppu.png` и `96-L1-cell3640-225-ppu.png`.

## Измерения

mGBA libretro5cac11…67e4, native16 777 216cycles/s, replacement BIOS,
WAITCNT4317h, PCM включён. Live120 отпускает diagnostic camera и следует игроку.
Ни CPU overclock, ни пропуск игровой логики не используются. Ниже RU FPS
предъявления кадров; EN отличается только в walking72 L3:25,875 вместо25,689.

| Вариант / маршрут | L1 | L2 | L3 |
|---|---:|---:|---:|
|128, turn120 |19,85 |16,86 |21,79 |
|96, turn120 |21,20 |18,24 |23,12 |
|72, turn120 |22,19 |19,80 |26,74 |
|128, walking120 |17,96 |17,92 |20,54 |
|96, walking120 |19,85 |20,13 |22,61 |
|72, walking120 |20,90 |21,14 |25,69 |

Spatial440 замораживает симуляцию, оставляет PCM/реальные карты/PPU. Это
распределённые доступные позиции и восемь направлений, не полное прохождение.

| Вариант | Минимум L1 | Минимум L2 | Минимум L3 | Видов быстрее / медленнее128 |
|---|---:|---:|---:|---:|
|128 |9,9546 |9,9546 |14,9319 |— |
|96 |11,9455 |11,9455 |19,9092 |255 /0 |
|72 |13,7833 |11,9455 |19,9092 |350 /0 |

Медианное отношение CPU time trial/128:96 —0,8954/0,8781/0,8974 по мирам;
72 —0,8133/0,7862/0,8159. Экономия полного кадра10–12% и18–21% соответственно.

## Реализация и найденное округление

Адреса здесь — runtime GBA, не файловые смещения EXE.96:12 bands по8 source
rows, physical step12, OBJ PD170/256,48 OBJs,12 288B foreground.72:9 bands,
step16, PD128/256,36 OBJs,9216B. PA128/256 сохраняет120 logical columns.
`scale_y` меняется с4/5 на3/5 либо9/20.4-word coverage ABI сохранён,
`raster_rect` получает границу96/72. `HW_EDGE_RAM_CALL` выключен ради IWRAM;
стековый резерв8192B не уменьшался. Legacy Mode4 upsample согласован с OBJ.

Row-color probe обнаружил, что приPD170 предыдущий OAM band перекрывает первую
строку следующего и выигрывает по меньшему OAM index. Это реальная affine
арифметика; последовательность отличается от равномерного resize.
Сохранён вариант без дыр, WIN0 ограничивает OBJ к240×144:04000040h=00F0h,
04000044h=0090h,04000048h=003Fh,0400004Ah=002Fh, DISPCNT3541h. Последняя лишняя
строка обрезается перед сообщениями; BG0 работает снаружи окна. Mode4 включает
свой прежний DISPCNT. Первые row/water проверки провалились до учёта этого
округления. Рабочая поставляемая сборка —v3; исходная ошибка не объявляется
passed. Декодер `scene_source_y` учитывает матрицу и OAM priority.

## Проверки

Оба ROM повторены byte-for-byte в `build/g20a-hw-row-quality-repeat/`;128 control
повторил2775. Все22 исходных SHA и прежние artifacts проверены. ARM vectors
проверяют bounds96/72,512 rectangle cases, samplers/coverage/arithmetic/ABI.
Отдельные row-color ROM доказывают mapping через настоящий PPU, их SHA отличаются
от обычных ROM. Вода проверена во всех144 phase cases у каждого варианта.

Оба прошли UI/flow/sprites/AI/28 AI-combat groups,109 item stat cases,58 pickup
cases, live pickup, independent projectile-source samples, interactions logic,
rational time и poisoned reset. G09 original affine pixel oracle явно выключен
для quality prototypes; совпадение с ним не заявляется.396 кадров каждого
варианта дают точный native HUD/weapon overlay.282 camera/weapon/message
transitions:4564 callbacks у96,4402 у72, mixed=0, overflow=0. Известная294
Pause/Resume baseline проблема этим набором не закрывается.

EWRAM261292/free852 у обоих; IWRAM24540/free36 у96,24364/free212 у72 до8192B
стеков. Main stack2520/margin5416, IRQ80/margin80.96 artificial all-awake stress
проходит guards/logic/audio, но даёт0,07–0,38FPS: бюджет не выполнен. Для72
этот stress повторно не запускался. Физическая GBA, original BIOS и whole-level
playthrough остаются открытыми. Пути всех входных отчётов и их SHA есть в JSON;
72 spatial/quality/ARM/water/transitions изv2 относятся к тому же ROM SHA.

## Измеренный узкий участок

Последующий порядок изменён пользователем: [сначала архитектура](gba-architecture-selection.md).
Профиль ниже сохраняется как доказательство стоимости; это не разрешение
продолжать локальный sampler tuning до сравнения крупных вариантов.

`profile96.json` теперь берёт per-primitive counters с отдельного held completed
draw после FPS samples. Раньше прямое чтение после collect могло получить уже
обнулённые counters следующего кадра. FPS и completed stage medians не меняются;
новые diagnostic timer ticks умножаются на64 для cycles.

L2cell1592135°: около1,330Mcycles всего, vertical0,661M, surfaces0,287M.
L2cell2871225°:1,375M всего, vertical0,693M, surfaces0,310M.
L3cell1544315°:0,815M всего, vertical0,501M. L1cell3640225°:1,207M всего,
CPU ceilings0,584M. В профиле есть instrumentation overhead; это атрибуция,
не новые fast-path FPS. Следующий приоритет — sampling/visibility вертикальных
граней и группировка поверхностей. Дальнейшее снижение разрешения без оценки
качества не выбранo. Адаптивный Mode2 после отрицательного grid4 опыта отложен.

```powershell
& C:/Source/wasdify/.venv/Scripts/python.exe scripts/prototype_gba_bsp.py --out build/g20a-hw-row-quality-v3 --variants hw_rows96 hw_rows72 hw_rows96_probe hw_rows72_probe hw_v128_entity_rect
& C:/Source/wasdify/.venv/Scripts/python.exe scripts/report_gba_row_quality.py --copy-rom
```

Сборщик требует перечисленные в JSON проверочные отчёты и чистое повторение;
одна компиляция не заменяет проверки.
