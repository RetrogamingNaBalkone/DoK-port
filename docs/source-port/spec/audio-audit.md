# SP13: цифровой звук и реальные DOS-записи

4 ноября 2026 года. Это проверка Sound Blaster-пути OG/PKR/PKE и современного
SDL-вывода. GBA/NDS-код не использован как реализация. Настоящие DOS-записи,
исполнение исходных инструкций и проверки портового PCM учитываются отдельно.

## Оригиналы и воспроизводимость

Fresh read-only Ghidra-экспорты: `research/evidence/source-port/sp13-audio/ghidra/`.
`export_audio_audit.py` проверяет SHA-256 EXE до/после, проект и хеш каждого
экспортированного диапазона. Все шесть исходных EXE/RES неизменны. Адреса
PKR/PKE ниже — NE Ghidra, OG — MZ Ghidra; это не файловые смещения и не
runtime-селекторы. MZ-листинг может нормализовать far-адрес: например, bytes
`9a 5d 02 a6 18` вызывают `18a6:025d`, а Ghidra показывает alias `1000:8cbd`.

| Граница | PKR | PKE | OG |
|---|---|---|---|
| Admission | 10f0:005e | 1058:186a | 1823:0065 |
| Once / loop wrappers | 1108:01f7 / 0232 | 1060:173d / 1778 | 18a6:01f7 / 022a |
| Stop / global stop | 1108:026d / 0298 | 1060:17b3 / 17de | 18a6:025d / 0280 |
| Аппаратная частота | 1118:0222 | 1070:0222 | 180b:0002 |
| DMA fill / direct DAC IRQ | 1118:046e | 1070:046e | 2b41:0077 |
| Заставка / предыстория | См. menu-settings | См. menu-settings | 20c4:0027 / 0198 |

Проверены far ABI, порядок wrapper → admission, signed/unsigned деление,
IN/OUT, IRET и перенос far sample pointer через 64 KiB. Конец OG IRQ пересекает
блоки Ghidra: `2b41:00ee OUT` → `2b50:0000 POP AX` → `2b50:0001 IRET`.
Экспорт `irq-tail.bin` сохраняет последние две исходные инструкции.

## Исправления

OG не использует отдельный DMA loop latch Кремля. Wrapper меняет bit40
Sound+7 **до admission**; IRQ читает этот же бит при встрече нулевого байта.
Поэтому отклонённый повторный once/loop для **той же identity** меняет режим
уже активного OG-звука. Порт раньше отбрасывал запрос целиком. Теперь этот
эффект восстановлен только для OG; запрос другой identity не меняет текущий
голос, а Kremlin сохраняет свой latch.

Нулевой терминатор OG не передаётся DSP. На его IRQ loop возвращает указатель
к началу, не записывая DAC: последний байт удерживается ещё один PIT-интервал.
Порт раньше начинал следующий цикл немедленно. Теперь дополнительный интервал
сохранён. Буфер с первым нулём допускает native пустой loop без чтения за
границей; once завершает его. Полностью пустой ресурс по-прежнему не запускается.

OG timer `180b:0069` знаково расширяет WORD частоты перед IDIV; low byte частного
задаёт младший байт PIT, старший байт равен нулю. Восстановлено signed WORD для
частот с установленным старшим битом. Все десять исходных OG-звуков имеют
16000 Hz и остаются с делителем 74. Кремль использует unsigned WORD и
округлённый период `round(1000000 / rate)`, затем DSP time constant.

OG заставка и предыстория обе вызывают loop wrapper `18a6:022a`; ключи
DS `3255:08d5` и `3255:091e` содержат один `ЗАСТАВКА_ЗВУК`. В main это
исправлено отдельной OG-ветвью; PKR/PKE сохраняют once. Завершение экрана
останавливает его владельца. Ручное подтверждение вместо исходного ожидания
10/60 секунд остаётся адаптацией интерфейса.

Исходные OG samples действительно лежат в диапазоне 1–64 до терминатора.
SB IRQ `2b41:008b–00a5` отправляет **сырой байт** после команды DSP `10h`;
SHL, масштабирования 6 → 8 бит и нормализации там нет. Современный PCM остаётся
`(u8 - 128) * 256`. Полученные записи не использованы для подбора усиления или
DC-компенсации. Неактивный голос выдаёт цифровую тишину; это адаптация SDL,
а не обещание удержания аналогового напряжения native DAC после stop.

## Playhead-контракт для SP17

`VoiceSnapshot` сохраняет identity, loop, профиль, output rate и фазу. Фаза —
числитель позиции: source index = `phase / (period * output_rate)`.
`SdlAudioSnapshot` добавляет ключ ресурса, pause и gain. Указатели не сохраняются.
Получение/замена голоса выполняются под SDL audio lock; перед восстановлением
устройство приостанавливается, чтобы callback не продвинул восстановленную фазу
между unlock и применением pause. Неверный ключ/профиль/фаза не заменяют голос.

Это API логического состояния, **не формат сохранений**. SP17 обязан проверять
совместимость RES и восстанавливать owner/binding identity. Пока output rate
восстанавливается только при совпадении с текущим устройством; другое значение
безопасно отклоняется. Перенос фазы между разными negotiated output rates
потребует отдельного рационального преобразования в SP17. Настройки gain/pause
не превращаются в поля исходного Sound36.

## Native и программные проверки

`check_audio_oracle.py` исполняет исходные OG wrappers/admission/IRQ в Unicorn.
48 сценариев: 0–3 ненулевых samples, initial once/loop, повторные once/loop,
та же/другая identity, семь IRQ. Сравниваются активная identity и каждый
цифровой результат. Hardware begin/stop заменены явными stubs, IN сообщает
готовность DSP, OUT записывается. **48/48 PASS**. Пауза терминатора и live
loop доказаны инструкциями, а не повторением C++ формулы.

Ещё **42/42** native rate-вектора исполняют исходные hardware timer/DSP функции
трёх игр; проверяются реальные OUT-делители, включая signed WORD OG и нулевой
low byte. Full PCM всех **56** звуков сравнивается с независимым Python,
использующим измеренный исходными инструкциями период. Все samples совпали,
включая нули внутри Kremlin PCM и OG терминатор. Это цифровая проверка до
эмуляторного/аналогового mixer и не выдаётся за акустическое равенство.

SDL dummy audio device реально открыт для каждого набора. Пройдены admission,
identity, stop, gain, отсутствие продвижения на pause, snapshot/restore и
отказ неизвестного ресурса. Unit проверяет продолжение PCM побайтно после
восстановления once/loop двух профилей, границы gain и недопустимую фазу.
Единая Release-сборка с SP12/SP14 прошла CTest **17/17**.
Три отдельных процесса актуального приложения с private user-dir подтвердили
music action: OG title и story оба дали `loop=1 active=1`, PKR/PKE —
`loop=0 active=1`. Это фактический main/Frontend/SdlAudio путь, не только
чтение ветки исходника. Логи: `app-music/{old-gold,ru,en}/port.log`.

## Настоящая акустическая сверка

Использован установленный DOSBox-Automation 0.85.1 (7567ec4), SHA-256 EXE
`bbcef5754411fcab12fca5fbffb7e3d1b335063c3ae7510597492d7b6892f6c0`.
HTTP audio capture записывает **фактический вывод эмуляции**, а не WAV,
собранный из RES. Смонтированы только отдельные evidence-копии. SB16,
48 kHz stereo, fixed 60000 cycles, SB filter/reverb/chorus/compressor off;
SDL dummy не открывает окно и не захватывает фокус.

PKR/PKE: неинструментированные title-записи `dos-title/{ru,en}/capture/`.
OG: исходный title consumer мог остановить звук задолго до конца записи.
Для отдельной проверки удержан **только main consumer** перед stop/free:
Ghidra `20c4:00f9`, runtime linear `0x12c89`, исходные relocated bytes
`9a5d029b0a` временно заменены `ebfe909090`. IRQ, PCM, timer и Sound flags
не патчились. После записи байты восстановлены и повторно прочитаны;
process завершён, исходные EXE/RES хеши неизменны.

Runtime подтверждает именно SB, а не PC Speaker: driver far `244a:356a`,
DSP write port `22c`, active Sound flags `E9`, IRQ0 после нормализации
`2b41:0077` (linear `2b487`). Лог, полная запись, временная инструментация
и trajectory находятся в `dos-held-trace/old-gold/`.

| Запись | NCC одного сдвига, 3 s | Медиана NCC 100 ms окон | Подогнанное отношение временных шкал |
|---|---:|---:|---:|
| PKR | 0,9211 | 0,9714 | 0,999975 |
| PKE | 0,8629 | 0,9270 | 0,999902 |
| OG, удержанный consumer | 0,9221 | 0,9912 | 0,998519 |

Сравниваются усреднённые блоки 4 kHz; локальные сдвиг, gain и DC **только
измеряются**, не применяются к звуку игры. Совпадение коротких окон подтверждает
характер waveform, но не весь высокочастотный диапазон или bit-exact WAV.
Ранее отдельная OG-запись имела global NCC около 0,30 при локальных 0,94–0,99:
накопленный сдвиг и редкие скачки делают один глобальный сдвиг недостаточным.

Независимое runtime-наблюдение: 128433 guest IRQ за 7,96597 host seconds,
16122,71 Hz против идеальных 16124,08 Hz (−0,0085%). Это **не** то же
измерение, что wave-fit ratio 0,998519. HTTP/host scheduling имеют погрешность;
данные не доказывают CPU overrun как причину всей акустической разницы.
Пересчёт direct-DAC clock, mixer/resampler и запись остаются отдельной границей.
В частности, [upstream Dac](https://raw.githubusercontent.com/dosbox-staging/dosbox-staging/main/src/hardware/audio/soundblaster.cpp)
измеряет частоту поступления direct-DAC writes; это ориентир интерпретации,
а не доказательство точной реализации установленного fork.

Первые попытки `dos/` с API type записали тишину и не входят в доказательство;
`dos-title/old-gold` и `dos-title-device/old-gold` — обрезанные consumer-записи.
Они не подменяют удержанную SB-проверку.

## Команды и оставшаяся граница

Из корня репозитория:

```powershell
py -3.14 -B ports/sdl2/tools/export_audio_audit.py
powershell -NoProfile -ExecutionPolicy Bypass -File ports/sdl2/tools/build.ps1 -Configuration Release
py -3.14 -B ports/sdl2/tools/check_audio.py
```

Создание **новой** записи, не перезаписывая старые evidence inputs:

```powershell
py -3.14 -B ports/sdl2/tools/capture_audio_reference.py --profile ru --label title-NEW --seconds 10
py -3.14 -B ports/sdl2/tools/capture_audio_reference.py --profile en --label title-NEW --seconds 10
py -3.14 -B ports/sdl2/tools/capture_audio_reference.py --profile old-gold --hold-og --label held-NEW --seconds 8
```

`check_audio_reference.py` обычно проверяет локальную waveform и публикует
различие временных шкал. `--strict` дополнительно требует global NCC >0,9
для всех трёх записей и сейчас закономерно отклоняет PKE; это не скрытый PASS.

Полные отчёты: `research/evidence/source-port/sp13-audio/report.json`,
`oracle-report.json`, `wave-comparison.json`, `check-*.log` и Ghidra manifests.
Компактный итог: `analysis/source-port/sp13-audio-summary.json`.

Реальная акустическая сверка трёх title waveform проведена; full gameplay
event matrix, аналоговая карта Sound Blaster, точная hardware latency и
bit-exact mixer equivalence не приняты. State API для SP17 готов в описанной
границе, игровые сохранения не добавлены. Следующие владельцы — SP14 тексты,
SP15 интерфейс/музыкальные экраны, SP16 реальные маршруты и исходы.
