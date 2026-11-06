# N23: экранная диагностика diag33

6 ноября 2026. Пользователь поручил усилить диагностику без отладчика DraStic.
База — feature_selection discovery32-ru; новый кандидат `diag33-ru`.

## Поведение

Один ROM `artifacts/nds/dok.nds`, 10174976 байт, SHA-256
`e310a0a53534381ae763dd68c66980606172854e094c6d42f7f524103f1f83e6`.
RU/EN и ресурсы встроены. На экране отказа выводится реальный immutable build
label `diag33-ru`; соответствующие исходники и ELF закреплены build manifest.

Верхний экран: Stage/Code, первый файл/строка/функция отказа, операция,
исходный status и два параметра A/B, кадр/уровень/поза, game/AI/combat/effects
faults, GPU flags, DMA карты и текстур, audio fault/underrun/stale/voice/ACK,
использование CPU texture cache. Для проверок без захваченного статуса
пишется `unknown`; последняя стадия не выдаётся за установленную причину.

Нижний экран: последние восемь событий от старого к новому. Каждое содержит
кадр, название этапа, ARM9 cpuGetTiming и A/B. Числа T/A/B — шестнадцатеричные.
Это история этапов, не трасса всех функций и не восемь полных кадров.

`crash_diagnostic.inc` хранит 188 байт состояния и кольца в ARM9 RAM,
дополнительно используются указатели и таблица названий. События не выполняют
SD writes, IPC, allocation или cache flush. Первый отказ замораживает историю,
внешние обёртки его не перезаписывают. DIAG_CHECK вычисляет вызов один раз,
затем читает параметры — в том числе опубликованные результатом audio service.

Исходные статусы захватываются для input/simulation, загрузки CPU textures
(включая batch), effect cache, texture plan/prepare/commit, GPU submit,
audio service. GPU sticky mask и elapsed deadline также показываются как
числовые значения соответствующих проверок. Остальные fatal paths сохраняют
первый файл/строку и последний этап, но могут иметь status unknown.

| Операция | A | B |
|---|---|---|
| texture, одиночная загрузка | asset ID | размер payload |
| texture, batch | capacity | used |
| effects, cache failure | capacity | used |
| plan | число textures | CPU cache used |
| submit | число cells | VCOUNT после вызова |
| upload prepare/cold commit | upload bytes | VCOUNT после вызова |
| upload prepared commit | upload bytes | start line OR (end line << 16) |
| audio | audio fault | underrun count |
| input | held keys | 0 |
| simulation | hero callbacks | game fault |
| GPU | DISP3D sticky | GXSTAT sticky |
| deadline | upload bytes | допустимые 46860 ticks; status = elapsed ticks |

История дополнительно содержит frame(level,ordinal), simulation(held,callbacks),
sprites(count,0), effects(demand count,used), submit(cells,textures),
wait(upload bytes,VCOUNT до ожидания), map(start,end) и boot(stage,0).
Эти параметры относятся к конкретному событию, а не последующей операции.

Fatal screen публикует NDS_RENDER_FAILED до переключения видео и затем не
вызывает audio cleanup или ожидание ACK. Экран остаётся неподвижным в локальном
цикле; для продолжения нужен холодный перезапуск. Возможен продолжающийся звук
ARM7. История не является сохранением и после перезапуска исчезает.

## Проверки и воспроизведение

Общие BlocksDS1.24/GCC16.2, `-Werror`. Первая sandbox-попытка MSYS2 не смогла
создать signal pipe (Win32 error5); разрешённая сборка вне песочницы PASS.

```powershell
py -3.14 -B -X utf8 -c "import json,sys; from pathlib import Path; sys.path.insert(0,'ports/nds/tools/performance'); from build import build; build('diag33-ru',json.loads(Path('build/nds/n23/discovery32-ru/build-manifest.json').read_text())['feature_selection'])"
py -3.14 -B -X utf8 ports/nds/tools/performance/check_crash_diagnostic.py build/nds/n23/diag33-ru --fault gpu
py -3.14 -B -X utf8 ports/nds/tools/performance/check_crash_diagnostic.py build/nds/n23/diag33-ru --fault audio
py -3.14 -B -X utf8 ports/nds/tools/performance/check_crash_diagnostic.py build/nds/n23/diag33-ru --fault plan
py -3.14 -B -X utf8 ports/nds/tools/performance/check_boot_input.py build/nds/n23/diag33-ru
py -3.14 -B -X utf8 ports/nds/tools/performance/check_audio_transition.py build/nds/n23/diag33-ru
py -3.14 -B -X utf8 ports/nds/tools/package_rom.py --target build/nds/n23/diag33-ru
git diff --check
```

Pinned melonDS: три fault-injection сценария PASS. Тесты **явно меняют RAM**:
GPU sticky=1; audio runtime fault=51; stream_palette=NULL. Последний вызывает
настоящий возврат DOK_NDS_RENDER_ARGUMENT=1 из stream_plan. Проверены первые
function/line, op/status/параметры, порядок кольца и неизменность после ещё
60 emulated frames. На всех трёх LCD captures оба экрана просмотрены: текст
помещается, читается. Это проверка диагностики, не воспроизведение DraStic.

Обычные boot/title240frames/X/Y/touch PASS; title→LEVEL1 transition, обнуление
title ring и waveform PASS, audio underrun/stale/fault=0. Выстрелы не заявлены:
на старте нет оружия. Полные маршруты, остальные уровни, новые FPS measurements,
физическая DS и DraStic этой итерацией не проверялись. Упаковщик независимо
сверил пять NitroFS payloads с manifest. Сводка и доказательства:
`analysis/nds-diagnostics33-20261106.json`,
`research/evidence/nds/n23/diag33/`,
`research/evidence/nds/n23/candidates/diag33-ru/`.

## Действия при сбое и ограничения

Запустить diag33 холодно, без старого savestate. При появлении аварийного
экрана сфотографировать **оба** дисплея целиком до перезапуска. Строки и адреса
сопоставлять только с diag33 manifest/ELF/source snapshot. Адреса — runtime
NDS, T — аппаратный таймер ARM9; это не DOS/Ghidra addresses.

Диагностика предназначена для обработанных fatal returns. Новый exception
handler/watchdog не добавлен: при жёстком зависании CPU, повреждении памяти или
падении самого эмулятора экран может не появиться. Не все нижележащие API ещё
сохраняют исходный status; по одному последнему событию причина не доказана.
Восстановление звука/кадра и исходная причина отказа DraStic не исправлялись.

N23 не принята, N24 не начата. Следующий ID N23: пользовательский mid-level
повтор на diag33, получение двух экранов, локализация первого отказа.
GBA/SDL2 и оригиналы не менялись. Игровых сохранений и диагностических SD writes нет.
