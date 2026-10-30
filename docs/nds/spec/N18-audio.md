# N18: ARM7 audio и ARM9 streaming

Исходные образцы, адресная система, каталог всех 23 PCM, native ABI,
приоритеты и игровые вызовы закреплены в [N18-audio-evidence.md](N18-audio-evidence.md).
NDS-тракт написан независимо. Автоматический C Ghidra не компилируется в порт.

## Владение и память

`platform/audio/arm7.c` — отдельный BlocksDS ARM7 core. Он сохраняет системный
FIFO, touch/input, RTC и power callback стандартного ядра. Maxmod и стандартный
sound FIFO не установлены. Только ARM7 владеет channel 0, sound registers,
timer 0/1 и всеми словами `NdsAudioStatus`. ARM9 timer 0 profiler и timer 2/3
игрового clock находятся на другом процессоре и не пересекаются с ними.

Audio slab 262144 байта разделён на ring 65536, transient fill 32768 и
resident packed pool 163840. Ещё 163840 байт выделены явно из прежнего запаса
SDK heap, с проверкой оставшегося минимума 196608 байт и неизменного reserve
655360 байт. Всего отведено 425984 байта. Все 22 эффекта, 487165 исходных
signed PCM bytes, хранятся lossless в 314352 байтах двух packed pools.
Точный общий static/startup/SDK headroom публикует runtime memory report;
дополнительный heap включён в closure, не скрыт за managed slab.

В startup исходные chunks проверяются N04 loader и пакуются независимым
`core/audio_codec.c`: modulo-256 deltas, блоки по 256 samples и Rice coding,
с raw fallback. Заголовок блока содержит длину для range decoding. Это
lossless runtime cache, не ADPCM и не новый источник игровых данных.
Все сжатые bytes flush один раз и затем неизменны; ARM7 их заимствует.
Метаданные каталога, текущая binding identity и IPC descriptor статические.

Для потокового титула ARM9 публикует готовность части через uncached mirror только после
`DC_FlushRange` её PCM. ARM7 читает только часть с ожидаемым ordinal; после
границы публикует `consumed_parts`. Только выбывшую часть может снова писать
ARM9. На deadline без готовой части ARM7 останавливает канал и увеличивает
underruns: старые данные не выдаются за новый звук. FIFO копирует команду;
bounded ACK подтверждает её обработку до reuse метаданных. Ready и status
разнесены по cache lines. Status читается через seqlock; diagnostic probe
имеет отдельный seqlock в последнем слове.

Для resident эффектов ARM9 отправляет только pointer/size/rate/identity.
ARM7 немедленно подтверждает logical admission, затем в своём main loop
декодирует PCM блоками по 1024 samples с опережением. Первые 1024 samples
готовы до включения канала; далее producer заполняет свободную часть ring.
IRQ проверяет запас готовых samples каждые 1024 samples. Этот путь не делает FAT I/O и не декодирует
PCM на ARM9; ACK не ждёт полного заполнения ring. ARM9 не пишет и не flush
ring во время ARM7 producer mode. После global barrier ring может снова
принадлежать ARM9 для титула. IRQ input/clock остаются включены во время
декодирования; partial producer publication и late STOP защищены отдельно.

`arm9.h` предоставляет init/write/begin/resident/stop/status и volume 0..127.
Global stop identity 0 — lifecycle barrier. Stop другой identity — допустимый
no-op. Volume задаёт амплитуду нового backend, не интерпретирует SVM C/D.
Следующие UI/settings-задачи могут вызывать эту команду вне игровых callbacks.

## Частота, конец и повтор

Сэмплы пакета N04 уже signed PCM8: исходный unsigned sample XOR 0x80.
DSP, смешивание, нормализация и программный resampling не эмулируются.
Sound timer period округляется один раз из `BUS_CLOCK / 2 / rate`.
Для DS BUS_CLOCK 33513982 это period 760 и 1520, приблизительно
22048.67237 и 11024.33618 Hz. Это выбранный NDS-тракт, не точная Sound Blaster.

Аппаратный канал повторяет весь ring, независимо от частоты ARM9 кадров.
Timer 0 считает сэмплы с тем же периодом; timer 1 каскадом отмечает границы.
Quantum — 1024 samples для ARM7 resident producer и 32768 для титульного
потока ARM9. Полные quantum используют аппаратную перезагрузку timer 1 без накопления
IRQ latency. Для последнего неполного once deadline учитываются уже прошедшие
сэмплы. Остановка акустического вывода имеет начальный register-write skew и
IRQ latency; точность до последнего физического sample проверяется в N24.

Заполнение loop возвращается к началу исходного PCM внутри любой части,
включая нечётную длину. Никакой padding не вставляется между повторами.
Once заполняет остаток ring нулевой signed амплитудой, но deadline использует
исходное sample count. Word-granular SCHANNEL_LENGTH относится к ring,
а не округляет длину исходного звука.

## Исходная политика и интеграция

`runtime.c` разрешает языковые bindings через N05 presentation. PCM bytes
RU/EN совпадают, имена и bindings остаются языковыми. Один активный голос
отказывает новым play, включая ту же identity. Rejected once не изменяет
звучащий loop. Global stop у pickup и AI dying выполняется перед новым once;
hero damage/death не получает придуманного global stop. Default sound,
hero+A6, AI+F0/+F4 и clone projectile identities различаются.

Gameplay sinks делают только admission/identity/stop и сохраняют один
ожидающий успешный start. Все эффекты уже resident: при боях/дверях нет чтений
из пакета. Для титула 110 все восемь исходных chunks проверены при startup;
во время потока прямой read-only reader читает не больше 512 байт за service
в свободную часть ring. Готовность публикуется только после всей части.
Две части по 32768 samples дают около трёх секунд на refill при 11025 Hz.
Начальные 65536 samples читаются на явной границе frontend/load до запуска
игрового clock и новой presentation sequence.

Это bounded cooperative supply, не асинхронный FAT driver. Его фактическая
стоимость входит в presentation intervals. Требование ARM9 0.3 мс является
целью N17P; совместный audio/HUD/map бюджет повторяет N21P. Отсутствие задержек
на произвольной физической microSD по эмулятору не гарантируется.

Два отклонённых варианта сохранены компактно: исходные синхронные загрузки
целого chunk давали 153 мс service; resident lossless, декодируемый ARM9,
давал 118 мс. Оба функционально прошли PCM, но провалили cadence и не были
приняты. Их результат нельзя выдавать за итоговую производительность.

Packed banks живут весь процесс и не перепаковываются при смене языка:
все 23 исходных RU/EN header/PCM пары побайтно идентичны. Resident lookup
игнорирует только два language bits, а bindings по-прежнему разрешаются
через выбранную presentation. STOP ACK запрещает дальнейший commit старого
producer в ring, но старое private декодирование может ещё дочитывать bank
до проверки generation. Поэтому STOP ACK не является разрешением освободить
или переиспользовать packed bank; такой lifecycle потребует отдельного
quiescence протокола. В текущем N18 bank не освобождается.

До destruction/reset мира вызывается global barrier, затем новый epoch и
presentation. Stale epoch outputs не меняют текущий голос. Язык меняется
через такой же clear/rebind. Pause не останавливает ARM7 clock; ARM9 продолжает
refill во время READY. Это соответствует исследованному modal DOS path,
который не вызывает stop и не маскирует IRQ. Freeze игровой симуляции остаётся
принятой адаптацией N08. Полного DOS acoustic capture этого modal нет.

Титульный ресурс 110 проигрывается once; исходного menu-loop не обнаружено.
N21 должен вызывать title/stop при переходе frontend и не добавлять loop
как якобы восстановленную семантику. Каталог диагностической сборки сначала
проигрывает все 23 once; затем odd-length loop 89, rejected once и чужой stop.
Один N17P live route продлён до 1800 presentations, чтобы весь титул прошёл
под движением/AI/render нагрузкой. Это явно искусственный stress, не музыка
в оригинальном игровом процессе.

## Воспроизведение

Общий Python: `C:/Users/dspev/AppData/Local/Python/pythoncore-3.14-64/python.exe`.
BlocksDS 1.24.0-1 / GCC 16.2.0, Zig 0.13.0, Python 3.14.6, Unicorn 2.1.4.
SDK остаётся в общей установке пользователя. Команды из корня:

```powershell
py -3.14 -B -X utf8 ports/nds/tools/audit_audio_source.py
py -3.14 -B -X utf8 ports/nds/tools/check_audio_codec.py
py -3.14 -B -X utf8 ports/nds/tools/check_audio_host.py
py -3.14 -B -X utf8 ports/nds/tools/check_audio.py prepare
py -3.14 -B -X utf8 ports/nds/tools/check_audio.py host
py -3.14 -B -X utf8 ports/nds/tools/check_audio.py final-release-ru
py -3.14 -B -X utf8 ports/nds/tools/check_audio.py final-release-en
py -3.14 -B -X utf8 ports/nds/tools/run_audio_emulator.py --target build/nds/n18/final-release-ru --packet build/nds/n18/scene.dat
py -3.14 -B -X utf8 ports/nds/tools/run_audio_emulator.py --target build/nds/n18/final-release-en --packet build/nds/n18/scene.dat --language 2
py -3.14 -B -X utf8 ports/nds/tools/summarize_audio.py
```

Host fake consumer проверяет production loader и точные PCM bytes; это не
исполнение ARM7. Отдельный source audit исполняет исходные x86 call slices и
mono/modal bodies. Отдельный melonDS capture проверяет реальный ARM7 IPC,
timer и sound channel. Эти три уровня не взаимозаменяемы.

`summarize_audio.py` по умолчанию сохраняет технические результаты со статусом
`software-checks-complete-awaiting-listening`: числовые проверки не заменяют
субъективное прослушивание. Флаг `--accept-listening-deferred` допустим только
после явного согласия пользователя перенести это прослушивание в N24.

Core melonDS DS 1.4.0 SHA-256:
`c7e6b58c667075a2a9868f2c3ba7a98e24200410a6da3051a7c555f1f0eef8ef`.
DS mode, builtin BIOS/direct boot, software renderer, JIT/threading off,
DLDI read-only, network/sync off. Capture checker использует точную AV sample
rate core; WAV header округляет её до целого Hz и сам по себе даёт drift
при длинном сравнении. Сравнение фильтрованного output с исходным PCM —
корреляция/покрытие, а не побайтное равенство.

Итоговые метрики, SHA ROM/ELF/map/capture, результаты и ограничения должны
читаться в [паспорте N18](../tasks/N18.md) и `analysis/nds-n18-audio.json`.
Полные доказательства — `research/evidence/nds/n18/`, текущие сборки —
`build/nds/n18/`. Исходники принятой N17P и её ROM/reports не перезаписываются;
её preserved pin report остаётся независимым входом. GBA/SDL2 не менялись.
