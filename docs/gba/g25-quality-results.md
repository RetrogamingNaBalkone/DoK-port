# G25: 96 строк на тяжёлых сценах и локальный LOD потолка

## Решение 2 ноября 2026 года

**Итоговая пользовательская оценка: H05 не выбирать основным вариантом.**
Прирост частоты кадров ощущается, но не оправдывает снижение разрешения.
Основной кандидат — G22 (`g22_split_active`), сцена 120×128; G25 остаётся
экспериментом. Это заменяет первоначальный выбор по эмуляторной проверке.
Цикл оптимизаций завершён, новых исследований не требуется.
Это видимый компромисс: текстуры и мелкие контуры грубее, вертикальная выборка
меняется во всей сцене. UI остаётся 240×160. На рассмотренных кадрах предметы,
решётки, проходы и цели сохраняют читаемость; полной незаметности не заявляем.
Проверки попаданий, AI, времени и переходов прошли. Пользовательская/аппаратная
оценка комфорта остаётся частью G21; общего нового плейтеста не требуем.

**H09: отклонить проверенный горизонтальный sampler пар потолочного материала.**
Он сохраняет ближний путь до depth960, геометрию, независимый coverage каждого
пикселя и leases. Но warm CPU −0,052…+0,274%, медиана 0%; бои не ускорились.
Это результат конкретного kernel, а не опровержение любого возможного LOD.

Пользователь уточнил приоритет: выигрыш на лёгких сценах около20 FPS сам по себе
не решает задачу; оценивать нижний FPS, тяжёлые ракурсы и бои. Фиксированного
целевого процента или обещания20–30 FPS нет.

## Измеренный эффект в тяжёлых сценах

Контроль — G22+UI02 `g22_split_active`, SHA-256
`53fdf1b1b868effecfd68c8feb87ffb60bcc72190a4d01f38d8684a781860247`.
Кандидат `g25_rows96` добавляет только `HW_ROWS96` к этому контролю, включая
принятые active edges. Прежнее сравнение G22 было split против split+96;
его проценты не подменяют новую пару.

Frozen warm, PCM одинаковый, 6 прогревов, 30 samples, replacement BIOS mGBA:

| RU ракурс: уровень / cell / направление | CPU кадра | FPS до → после |
|---|---:|---:|
| L1 /520 /225° | −13,23% | 11,95 →13,27 |
| L1 /3640 /225° | −13,72% | 11,95 →14,93 |
| L2 /1592 /135° | −11,41% | 11,95 →11,95 |
| L2 /2871 /225° | −11,45% | 9,95 →11,95 |
| L3 /1544 /315° | −12,40% | 14,93 →19,91 |

По18 warm позам RU/EN: CPU **−14,933…−11,403%, медиана −12,398%**.
Cold geometry: **−13,609…−9,600%, медиана −11,680%**; cold FPS отсутствует,
поскольку удержание draw исключено из CPU и нарушает обычное предъявление.
В этих строках CPU означает полную измеряемую работу кадра, не только sampler.

В пространственной выборке **880 поз** (440 на язык), 6 samples на позу,
CPU **−15,449…−6,765%, медиана −11,301%**. Минимальное предъявление
**9,9546 →11,9455 FPS**. Пять худших L2-поз дополнительно перепроверены на30
samples: исходные9,95 FPS стали11,95. Поза L1/520/315 осталась11,95 при
меньшем CPU: ожидание VBlank делает FPS ступенчатым. Это минимум выборки,
не гарантированный минимум всей игры.

При FPS около10 прибавка примерно2 кадра/с помогает, но тяжёлые сцены всё ещё
медленные. Ускорение CPU не означает такого же изменения FPS: кадр может
попасть в тот же VBlank либо перейти на следующую ступень предъявления.

## Массовые бои и движение

Исходные комнаты PERF01–03, обычный AI, огонь,120 предъявлений на fixture:

| RU бой | FPS до → после |
|---|---:|
| L2 last-key | 11,89 →13,01 |
| L3 west-key | 12,96 →14,28 |
| L3 southeast-key | 13,70 →15,82 |
| L3 northeast-key | 13,32 →14,16 |
| L3 shotgun-witches | 10,73 →12,36 |

EN совпал, кроме southeast-key:13,70 →15,65 FPS. Это нагрузочные сценарии,
а не причинное сравнение идентичных живых состояний: при другом каденсе AI,
состояния и длительность120 предъявлений расходятся. Причинный вывод о CPU
опирается на frozen A/B выше. Во всех боях нет audio underruns.

Шесть live-маршрутов, по600 video frames, одинаковое расписание ввода0/200/400:
RU L1/L2/L3 **16,33/15,43/22,10 →18,52/17,32/24,79 FPS**;
EN **16,33/15,43/21,80 →18,52/17,32/24,79**. CPU p95 RU
1160960/1147904/811072 →1059840/1036736/769408 cycles.
Checkpoints, game ticks и задержки наблюдения ввода сохранены; live states
не объявляются exact, физическая задержка кнопки не измерялась.

## Качество и регрессия

480 PPU captures:160 равных поз ×3 сборки. Включены короткие сдвиги/повороты
в восьми местах, тяжёлые потолки, предметы, решётки и16 исходных ракурсов
мышей на960/1440 units. Последовательности заморожены: они показывают
изменение выборки при движении камеры, но не заменяют интерактивный плейтест.
Просмотрены нативные пары и начало/середина/конец последовательностей:
coarse textures и ступени мелких контуров видимы; исчезновения показанных
решёток/предметов или перекрытия проходов не обнаружено. Отдельные мыши могут
быть закрыты оружием/геометрией в обоих вариантах; эти кадры не доказывают
видимость каждой дальней цели. Автоматические боевые проверки подтверждают
попадания и правила, а не субъективную лёгкость наведения.

Дополнительно396 detail PPU: UI/оружие/сообщения/RU–EN;18 основных кадров
имеют7288–22062 отличающихся RGB pixels. Эти отличия ожидаются для96 строк
и не использованы как автоматический отказ. H09 сохраняет coverage/leases
во всех160 позах, основной PPU diff0–370 pixels.

Полный frontend `--verify`: latency, frontend/UI/flow/credits, audio/time,
ARM math, water, sprites, Mode4 transitions. Дополнительно combat, AI-combat,
projectile cells (точный control trace), centered shots, midrange bats,
close hits, ceiling sides, story, automap,48 cache mutations и profile-game
с искусственными нагрузками. Windows host и Linux/Thumb cross-build прошли;
Linux binaries не исполнялись. Native suite `build-native.ps1 -CrossChecks`
прошла. Физическая GBA/original BIOS/full playthrough не проверены.

В `verify_ceiling_sides_gba.py` исправлен фиксированный128-row PPU oracle:
`scene_source_y` теперь получает фактическое число OBJ strips из диагностики.
Проверка исходного texture V/LIGHT/palette осталась точной и прошла на обеих
схемах96/128. Это исправление измерителя, не ослабление pixel gate.
Cache RGB проверяется внутри96-row layout, а не на равенство128 строкам.

Первые H09 прогоны исключены: новый ASM object сначала оказался в ROM, затем
не получил prelit/COVER4 flags. Они не доказывают замедление LOD и сохранены
только как явно invalid controls. Финальный object `ceiling-lod-columns.o`
попадает внутрь копируемого IWRAM, получает те же flags, что tiled kernel;
новые coverage/lease captures и все финальные timing reports привязаны к SHA.

## Артефакты, память и воспроизведение

Экспериментальный образец с 96 строками, не основной кандидат:
[`artifacts/gba/DoK-port_G25.gba`](../../artifacts/gba/DoK-port_G25.gba),
**26649436 B**, SHA-256
`ec0d4375f48e06f6e5d08544a0faf9ef65af732435fb848761dde2abe680512e`.
EWRAM261476 B/free668; IWRAM23140 B/free1436 при отдельном stack reserve8192 B.
Две сборки из разных base и повторная линковка совпали. Повторный контроль
остался G22 SHA. H09 SHA
`b1f669d931f5f09b351772b0ee7cb3cd9866d5c570a4c5b057d3e0bae240c8c7`.
Исторический default frontend-builder сохранён для прежних воспроизведений;
эксперимент воспроизводится с `--variant g25_rows96`, основной G22 —
с `--variant g22_split_active`.

```powershell
py -3.14 -X utf8 ports/gba/scripts/build_gba_frontend.py --variant g22_split_active --out build/gba/g25/control
py -3.14 -X utf8 ports/gba/scripts/prototype_gba_bsp.py --base build/gba/g25/control/base --out build/gba/g25/rows96 --variants g25_rows96
py -3.14 -X utf8 ports/gba/scripts/prototype_gba_bsp.py --base build/gba/g25/control/base --out build/gba/g25/lod-iwram --variants g25_ceiling_lod
py -3.14 -X utf8 ports/gba/scripts/check_g25.py --stage warm
py -3.14 -X utf8 ports/gba/scripts/check_g25.py --stage cold
py -3.14 -X utf8 ports/gba/scripts/check_g25.py --stage spatial --variants control rows96
py -3.14 -X utf8 ports/gba/scripts/check_g25.py --stage tail --variants control rows96
py -3.14 -X utf8 ports/gba/scripts/check_g25.py --stage battles
py -3.14 -X utf8 ports/gba/scripts/check_g25.py --stage live
py -3.14 -X utf8 ports/gba/scripts/check_g25.py --stage ppu
py -3.14 -X utf8 ports/gba/scripts/audit_g25_quality.py
py -3.14 -X utf8 ports/gba/scripts/build_gba_frontend.py --variant g25_rows96 --out build/gba/g25/verified --verify
py -3.14 -X utf8 ports/gba/scripts/check_g25.py --stage qualify
py -3.14 -X utf8 ports/gba/scripts/prototype_gba_bsp.py --base build/gba/g25/verified/base --out build/gba/g25/repeat --variants g22_split_active g25_rows96
py -3.14 -X utf8 ports/gba/scripts/build_gba_frontend.py --variant g25_rows96 --out build/gba/g25/current
powershell -NoProfile -ExecutionPolicy Bypass -File ports/gba/scripts/build-native.ps1 -CrossChecks
py -3.14 -X utf8 ports/gba/scripts/report_g25.py
py -3.14 -X utf8 tools/reports/summarize_reports.py
py -3.14 -X utf8 tools/reports/summarize_reports.py --check
git diff --check
```

Detail PPU дополнительно запускается `benchmark_gba_hardware.py --details`
с builds96/control, languages0/1 и report `rows96-detail-ppu.json`.
Финальные JSON, SHA и команды — `research/evidence/gba-g25/`; PNG comparison
слева направо:128 rows /96 rows /H09. Полный отчёт
`analysis/gba/g25-quality-20261102.json`, Git-сводка — `analysis/summaries/`.
Вызовы выполнялись по стадиям, после исправления oracle продолжены только
оставшиеся проверки. H09 timing повторён после исправления flags.

G25 закрывает H05/H09 в проверенном объёме; следующий открытый ID — **G21**
(финальная приёмка, BUG02 остаётся отложенным пользователем). Нового релиза
не опубликовано; независимые GBA/NDS/SDL2 handoff и оригиналы не смешивались.
