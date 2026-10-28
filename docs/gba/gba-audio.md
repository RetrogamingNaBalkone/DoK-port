# GBA: один PCM-голос и поток из ROM

G16 переносит одноголосную политику DOS и воспроизводит оригинальные unsigned
PCM8 из ROM через FIFO A. Микшера, нормализации, усиления и ресэмплинга исходных
ресурсов нет. Поля SVM C=255/D=128 остаются неизвестными параметрами и не
используются как громкость/панорама. Аппаратный FIFO A направлен в оба канала
с фиксированным коэффициентом100%; это настройка нового backend, не вывод о C/D.

## Проверенная native-политика

Образцы — `originals/pkr/RUS/PKR.EXE` и `originals/pkr/ENG/PKE.EXE`.
SHA-256 до/после, версии скриптов и проекты Ghidra зафиксированы в
`research/evidence/audio/{ru,en}/analysis-manifest.json`. В этих каталогах сохранены
`evidence.asm`, отдельный автоматический `automatic.c`, сегментные выгрузки и
`ghidra.log`. C-псевдокод служит свидетельством, не исходником движка.
Адреса далее — адреса NE-загрузчика Ghidra: это не файловые смещения,
не runtime-селекторы DPMI и не физические real-mode paragraphs.

| Граница | RU | EN |
|---|---|---|
| Mono admission | `10F0:005E` | `1058:186A` |
| Stop global | `10F0:00EA` | `1058:18F6` |
| Stop указанного активного объекта | `10F0:0125` | `1058:1931` |
| Play once / loop | `1108:01F7` / `0232` | `1060:173D` / `1778` |
| Stop wrapper / global wrapper | `1108:026D` / `0298` | `1060:17B3` / `17DE` |
| Is playing | `1108:02B9` | `1060:17FF` |
| Latch loop, затем DMA fill | `1118:0829` / `046E` | `1070:083D` / `046E` |

Начало нового звука отвергается, пока текущий объект имеет flags+7 bit80.
Отвергается также повторный запуск этого же объекта; позиция не сбрасывается.
Once очищает bit40, loop устанавливает его **перед** admission. Однако backend
копирует этот флаг только при успешном старте в глобальный latch: RU `D5A`,
EN `DFA`. Fill проверяет latch. Поэтому rejected once у текущего loop не
отключает звучащий повтор; rejected loop у текущего once не продлевает его.
Portable `requested_loop` и `loop` разделяют эти два состояния.

Обычный stop проверяет активный флаг переданного объекта; в одноголосной модели
это остановка по identity. Global wrapper `0298` не принимает объект; лишний
аргумент в автоматическом C у вызывающих мест не меняет его семантики.
`dok_audio_playing` нормализует native bit80 к Boolean. Исходные far-указатели
заменены заимствованным проверенным view и отдельной logical identity.
RU `1060:0145` каждый раз выделяет новый Sound36 (`016B PUSH24`, `016D malloc`,
`018D bind`); EN соответствующий bind находится `1018:1965`. Он привязывает объект к кешированному
PCM; кеш PCM не означает общего объекта звука. Identity определения —
`(objectID<<16)|bindingSlot`, тогда как stable resource ID выбирает только PCM.
Клоны projectile также получают собственный Sound36: RU `1068:2368`→bind`24D4`,
EN `1020:2960`→bind`2B58`; их identity — `40000000|(dynamicSlot+1)`. Namespace тестовых
standalone play — `80000000|resourceID`. Поэтому остановка другой двери с тем
же PCM не останавливает текущую, а разные клоны не смешивают active flags.
Native вызовы — `cdecl16far`, аргументы far-pointer передаются двумя словами,
очистка стека у caller. Типы/flag-операции сверены с ASM, а не с одной сигнатурой
декомпилятора. `verify_audio_original.py` исполняет на язык7 admission/stop,
2 wrapper-mutation и4 DMA-latch случая с перехватом аппаратных границ.
Это изолированные x86-вызовы, не полная DOS-сессия.

## Игровые потребители

Все bindings разрешаются из текущего языка/уровня, без хранения native pointers.
Уже звучащий view остаётся заимствованным из неизменного ROM при смене языка.
Release balance общий; исходные RU/EN исследуются отдельно.

| Событие | Binding и действие | Native свидетельство RU |
|---|---|---|
| AI WAKE | default sound, guarded once | `1068:3747` |
| AI CONTACT / RANGED | `F0`, guarded once | `1068:3845`, ветви contact и burst |
| AI DYING | global stop, затем `F4` once | `1068:3845`, вход death |
| AI DEAD | stop `F4` | `1068:3845`, завершение death |
| Player damage | `A6` once | `1068:1B10` |
| Successful shot | звук определения оружия | `1068:1B10` |
| Projectile impact/miss / removal | default once / stop | `1068:26E8`, impact и completion |
| Accepted item | global stop, default once | `1068:2F8A` (EN `1020:362D`) |
| Door moving | default guarded loop | `1068:3262`, `334E` |
| Door opened/closed | default stop | те же callbacks |
| Door locked | default once | `1068:347E` |

У AI HURT отдельный звук не придуман. Событие MOVING добавлено в конец enum
interactions и испускается при каждом реальном шаге створки; guard исключает
перезапуск уже активного door sound. Очистка/загрузка уровня останавливает DMA
и текущий голос до нового набора consumers. Death text остаётся G17.

## Аппаратная организация

Timer0 задаёт sample period: ближайшее целое `16777216/rate` —761 для22050Hz
и1522 для11025Hz. Фактические частоты приблизительно22046.276 и11023.138Hz
(−0.0169%). Сэмплы не выбрасываются и не интерполируются. DOS DSP также
квантует частоту, но равенство высоты/длительности с Sound Blaster не заявляется.

DMA1 переносит по16bytes из двух буферов по16bytes в FIFO A; IRQ после burst
заполняет освобождённый буфер signed-байтами `unsigned-128` прямо из ROM.
В начале32bytes предзагружаются в FIFO, следующие32 готовы в staging.
Нет полного PCM в RAM и дополнительного signed-банка в ROM. На границах
нечётного loop переход source end→start происходит внутри блока, без padding.
Once после конца заполняет staging нулями; Timer1 каскадируется от sample Timer0
и выключает вывод по sample count (частями до65536samples). Обработка IRQ и
перевооружение Timer1 имеют небольшую задержку; точное аппаратное равенство
последнего сэмпла DOS не объявляется.

Timer2/3 сохраняют rational game clock с prescale64/cascade. Renderer profiler
теперь использует эту свободно идущую пару, квант64cycles; он не выключает clock.
DMA3 presentation разбита на части до256words, сохраняя все38400bytes кадра.
Один immediate transfer9600words блокировал CPU/IRQ примерно на90kcycles:
DMA1 продолжала работать, но refill не успевал. Разбиение — условие непрерывного
аудио при медленном renderer, не уменьшение разрешения и не выполнение G20.

IRQ trampoline сохраняет LR и соблюдает ARM/Thumb interworking. System stack и
IRQ stack измеряются отдельно: startup watermark `0D15EA5E` в
`03007F00..03007FA0` не затрагивает BIOS-область выше7FA0. Main stack watermark
остаётся `03006000..03007F00`. Физическая консоль/original BIOS не проверены.

Аппаратные правила: [GBATEK, Direct Sound/DMA/timers](https://mgba-emu.github.io/gbatek/).
В частности, звуковой DMA требует staging во внутренней памяти; FIFO содержит32bytes,
запрашивает16bytes при половине заполнения и сам не знает длины ресурса.

## Воспроизведение проверки и ограничения

Итоговый frozen ROM — `build/gba/g16/dok-diagnostic.gba`,32867908bytes,
SHA-256 `99b4e21fabba12543bffa9217adf7e2a4d78655a45b7b0e57de1b0a0a022273e`.
Static EWRAM248136/free14008; IWRAM23980/free596 до резерва8KiB.
Voice/cursor/staging занимают128bytes, весь прирост static EWRAM —264bytes.
Object36/player60/ARM world33864 сохранены.

Actual mGBA/libretro65536Hz stereo capture прошёл все23 звука: совпадение
моно L/R, native-rate once EOF и числовая корреляция с независимо извлечённым
original PCM по всему звуку. Whole-recording коэффициенты0.98793..0.999947;
это проверка формы после фильтрации/ресэмплинга mGBA, не bytewise equality.
Длинный звук474361samples: расчёт43.033209s при GBA period1522,
наблюдение43.028755s,123 завершённых renderer frames во время потока.
За весь ARM-прогон81888 DMA IRQ, underruns0, max observed IRQ gap412ticks64
(26368cycles), max C refill/rearm1280cycles.

Одна фиксированная сцена, по12 кадров на вариант, framebuffer SHA одинаков:

| Поток | Средние elapsed cycles | Прибавка к silent |
|---|---:|---:|
| Silent | 5383840 | — |
| 22050 nominal | 6163621.33 | 14.48% |
| 11025 nominal | 5748458.67 | 6.77% |

Это интегральная нагрузка IRQ/DMA на полный renderer, не отдельное измерение
всех инструкций ISR. У main stack high water2360bytes, свободный наблюдавшийся
margin5576bytes; IRQ stack80bytes used/80bytes margin, включая BIOS IRQ saves.
Регрессии renderer/RU-EN/reset/guards, interactions, rational clock и все28
G15 enemy variants прошли на том же ROM. Silent renderer regression
4671616..4693696cycles всё ещё значительно выше бюджета561792cycles/30FPS;
выполнение G20 не заявляется.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File ports/gba/scripts/build-gba-audio.ps1 -CrossChecks -OriginalEvidence
```

Host — `analysis/gba/g16-host-verification.json`, native —
`analysis/gba/g16-original-verification.json`, actual libretro capture —
`analysis/gba/g16-audio-gba.json`, aggregate — `analysis/gba/g16-review.json`.
ELF/ROM/map и WAV сохранены в `build/gba/g16/`. Части pipeline выполнены отдельно;
итоговый aggregate успешно связал ROM, source/artifact hashes и все отчёты.

`g_audio_probe`: request/ack/op/resource/result; op1 once,2 loop,3 identity stop,
4 isplaying,5 level reset,6 global stop;7 binding once,8 binding loop,9 binding stop,
10 binding isplaying (resourceword=`objectID<<16|slot`). Диагностика различает starts/rejected/stops,
completed, IRQ count, source prefetch position/wraps, gap, refill cost и RAM.
`position/loops` опережают физический FIFO и не являются audible playhead.
`underruns` считает IRQ gaps больше32sample periods по часам64cycles;
это детектор потери обслуживания, не аппаратный FIFO error register.
`refill_cycles` — максимум измеренной C-части fill/rearm, без BIOS/trampoline
и входа/выхода C; из него нельзя объявлять полную загрузку CPU.

WAV libretro — фактический вывод mGBA с его фильтрацией и ресэмплингом,
не bytewise WAV исходного PCM. Числовая проверка формы и непрерывности полезна,
но не заменяет слуховую оценку. Пользователь прослушал фактический mGBA excerpt
и ответил «Звучит нормально». Свидетельство — `analysis/gba/g16-listening.json`;
неизменяемый прослушанный файл `build/gba/g16/audio-check/listened-short-loop.wav`,
SHA-256 `81d9ab296af0ce1fa91a9afa75ad2b931a3c88184dc87b4837812906efeda94b`.
Он снят с ROM `5f65b9c4d9a64e1b006f2d83ee512cafb11302e8b31d65161ae78ec561e09238`
после исправления DMA3 pauses; последующее изменение identity не меняло PCM/DMA.
Это ограниченная пользовательская слуховая оценка short loop, не объявление
прослушивания всех23 ресурсов. Полная DOS-сессия, Sound Blaster equivalence
и физический GBA не проверены.
