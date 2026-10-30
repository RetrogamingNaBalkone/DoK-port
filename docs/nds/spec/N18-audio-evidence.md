# N18: исходные звуки, музыка и ABI

Образцы: `originals/pkr/RUS/PKR.EXE` и `originals/pkr/ENG/PKE.EXE`.
Адреса в таблицах — **Ghidra NE-loader**, не файловые смещения, не индекс
NE-сегмента и не DPMI runtime-selector. Использованы уже существующие экспорты
`research/evidence/audio/{ru,en}/`, `ui/{ru,en}/` и `flow/{ru,en}/`.
Нового анализа EXE вне Ghidra нет; экспортированный automatic C не является
исходником движка. Политика NDS реализуется независимо от GBA-кода.

Повтор проверки:

```powershell
py -3.14 -B -X utf8 ports/nds/tools/audit_audio_source.py
```

Python3.14.6, Unicorn2.1.4; предыдущие Ghidra-проекты —
`research/ghidra/projects/{ru,en}/Kremlin.gpr`, Ghidra12.1.2.
Полный отчёт и pins экспортов:
`research/evidence/nds/n18/source/report.json`; компактный итог:
[`analysis/nds-n18-audio-sources.json`](../../../analysis/nds-n18-audio-sources.json).
Исторические `analysis-manifest.json` экспортов могут содержать старые пути
`build/ghidra`/`scripts`; нынешнее расположение указано здесь и в input pins.

Текущие SHA-256 до/после проверки одинаковы:

- PKR: `12f78afc0eba05f76b5dc0bb99da38d7daa20585024c0ba958fe828aa3864d28`.
- PKE: `cce7bc5993d0734ad87e93db37a70b8b52faedfff14ac48d6e317e5ac996a37b`.

## Полный каталог

В каждой версии23 блока,961526 unsigned PCM8 mono samples. Все23 пары
**включая SVM-заголовки побайтно одинаковы**. Отдельных локализованных
PCM-последовательностей в этих RES нет; NDS сохраняет разрешение bindings по
выбранному языку. Локальные исходные блоки доступны по
`research/evidence/resources/{ru,en}/NNNN.bin`; WAV —
`research/evidence/sounds/{ru,en}/NNNN/pcm8.wav`. Все source/PCM/WAV hashes
проверены заново и записаны в полном отчёте.

| RES index | Исходное имя | Hz | Samples | Секунд при номинальной частоте |
|---:|---|---:|---:|---:|
|86|зМЫШЬ_И|22050|15706|0.712290|
|87|зМЫШЬ_А|22050|9800|0.444444|
|88|зМЫШЬ_У|22050|21900|0.993197|
|89|зБОЯР_И|22050|18075|0.819728|
|90|зБОЯР_А|22050|7888|0.357732|
|91|зБОЯР_У|22050|26800|1.215420|
|92|зРУЖЬЕ|22050|10567|0.479229|
|93|зБАБА_И|22050|32420|1.470295|
|94|зБАБА_А|22050|21400|0.970522|
|95|зБАБА_У|22050|21444|0.972517|
|96|ЧЕЛЗ_УБИТ|22050|16330|0.740590|
|97|зРУЖЬЕ_ВЫСТРЕЛ|22050|10567|0.479229|
|98|зЛАЗЕР_ВЫСТРЕЛ|22050|17318|0.785397|
|99|зВЗЯТЬ_КЛЮЧ|22050|39448|1.789025|
|100|зВЗЯТЬ_ПУЛЮ|22050|32670|1.481633|
|101|зВЗЯТЬ_ЖИЗНЬ|22050|28800|1.306122|
|102|зВЗЯТЬ_МУХ|22050|12912|0.585578|
|103|зВЗЯТЬ_РУЖЬЕ|22050|33954|1.539864|
|104|зВЗЯТЬ_КАРТУ|22050|30968|1.404444|
|105|зРАЗРЫВ_ПУЛИ|22050|8136|0.368980|
|106|зДВЕРЬД|22050|46262|2.098050|
|107|зДВЕРЬК|22050|23800|1.079365|
|110|ЗАСТАВКА_ЗВУК|11025|474361|43.025941|

`SVM` расположен в+2; LE version+5=1, flags+7=09, bits+8=8,
format+10=0, C+12=255, D+14=128, rate32+16, samples32+20.
PCM начинается с+36; длина каждого блока ровно36+samples.
Первое слово — сериализованное native значение, не magic; указатели+24 и
неизвестные+28/+32 нулевые. Header36 и ABI подробно описаны в
[`docs/originals/sound.md`](../../originals/sound.md).

Преобразование в signed PCM8 — `u8 XOR 0x80` (численно `u8-128`),
в PCM16 — `(u8-128)*256`. Не выполняются нормализация, усиление или
ресэмплинг. C/D остаются неизвестными; называть их громкостью/панорамой
необоснованно. DOS DSP передаёт lowWORD(rate), его аппаратное целочисленное
квантование не равно обещанию точного11025/22050Hz на любой Sound Blaster.
NDS backend должен отдельно указывать собственную таймерную частоту.

N04 уже содержит независимое представление: PCM_CLIP header24 и список
PCM_CHUNK; каждый CHUNK — signed PCM8 не более65536B. У титула8 chunks.
Этот внешний пакет — источник NDS runtime, полные WAV/сырой export в ROM
или Git не включаются.

## Один голос и identity

| Граница | PKR | PKE |
|---|---|---|
|Admission|10F0:005E|1058:186A|
|Global stop|10F0:00EA|1058:18F6|
|Stop указанного активного объекта|10F0:0125|1058:1931|
|Once|1108:01F7|1060:173D|
|Loop|1108:0232|1060:1778|
|Stop wrapper|1108:026D|1060:17B3|
|Global wrapper без аргументов|1108:0298|1060:17DE|
|Playing, возвращает bit80|1108:02B9|1060:17FF|
|DMA begin / fill|1118:0829 /046E|1070:083D /046E|

Реальный Sound36 имеет active bit80, requested-loop bit40 в flags+7.
Пока текущий объект активен, новый play отвергается, включая ту же identity;
позиция не перезапускается. Числовой priority и список нескольких голосов
не обнаружены. Прерывание текущего голоса — явный global stop у конкретных
игровых потребителей, затем новый once.

Once/loop меняют requested bit40 **до admission**. DMA begin копирует его
в отдельный latch RU DGROUP:D5A / EN:DFA; fill использует latch.
Rejected once не выключает уже звучащий loop; rejected loop не продлевает
once. При loop конец исходных samples возвращает начало без padding.
Два буфера0x800B обслуживают один поток. Stop по identity другой двери с тем
же PCM не должен останавливать текущий объект.

ABI: `cdecl16far`; far object pointer — offsetWORD/segmentWORD, caller cleanup,
`RETF`. Возврат admission — AX, pointer returns — DX:AX. Вызовы global wrapper
встречаются с лишним аргументом; тело не читает его и останавливает общий голос.
Конструктор resource binding RU1060:0145 выделяет Sound36 отдельно для каждого
потребителя, даже при общем кэше PCM; projectile clones также получают собственный
объект. Логическую identity NDS требуется отличать от номера PCM.

Независимое исполнение исходных x86 instructions подтвердило13 случаев на язык:
7 admission/identity/global-stop,2 wrapper mutation,4 loop-latch. Hardware
begin/stop перехвачены; DMA, IRQ и DSP не эмулированы. Ghidra selectors
не используются как real-mode paragraphs: fixture загружает код в7000,
DGROUP/stack3000, driver5000, objects9000.

## Игровые триггеры и bindings

Native slot+4C — default sound определения: `DOK_PRESENT_SOUND`.
Специальные Sound fields+A6/+F0/+F4 разрешаются через
`dok_presentation_get(glyph, native_offset, ...)`. Это offsets native object,
не смещения в RES и не адреса поля portable struct. SHOT берёт sound оружия,
damage/death — sound героя; projectile — собственный clone identity.

| Событие | Действие, native slot | PKR call | PKE call |
|---|---|---|---|
|Hero dead|once hero+A6|1068:1972|1020:1F26|
|Successful shot|once weapon+4C|1068:1C3E|1020:21F2|
|Hero damage|once hero+A6|1068:1C7F|1020:2233|
|Projectile removed|stop projectile+4C|1068:2826|1020:2EC9|
|Projectile impact/miss|once projectile+4C|1068:2AD0|1020:3173|
|Accepted pickup|global stop, once item+4C|1068:2FB5/2FC5|1020:3658/3668|
|Door opening|guard playing, loop+4C|1068:32B6/32CA|1020:396B/397F|
|Door opened|stop+4C|1068:330E|1020:39C3|
|Door closing|guard playing, loop+4C|1068:33AA/33BE|1020:3A5F/3A73|
|Door closed|stop+4C|1068:33E7|1020:3A9C|
|Door locked|once+4C|1068:351F|1020:3BD4|
|AI wake|guard playing, once+4C|1068:3768/377C|1020:3ED1/3EE5|
|AI contact|guard playing, once+F0|1068:38F3/3908|1020:405C/4071|
|AI ranged|guard playing, once+F0|1068:3B1B/3B30|1020:4284/4299|
|AI dying|global stop, once+F4|1068:3DA9/3DBA|1020:4512/4523|
|AI dead|stop+F4|1068:3EAD|1020:4616|

Отдельных звуков для AI HURT и EMPTY не добавлять. Projectile IMPACT и MISS
выбирают разный визуальный effect, но этот исходный участок берёт один default
sound. Очередь portable events должна сохранить исходный порядок, в том числе
global-stop→once; deferred consumption вне callbacks не меняет эту очередь.
Таблица содержит18 native callsites; все36 RU/EN PUSH/CALLF/ADDSP slices
исполнены с уникальными far-pointer fixtures: pointer и caller cleanup точны.
Это доказательство границ вызовов, не повтор полных AI/physics callbacks.
Playing guards дополнительно прочитаны в ASM; полные callback oracles находятся
в N11/N15/N16/N17.

## Стартовая музыка, меню и пауза

Титул RU1008:01BA / EN1008:023E привязывает `ЗАСТАВКА_ЗВУК`.
RU1008:0275 / EN1008:0304 вызывает **once**, затем после ожидания клавиши
RU1008:0294 / EN1008:0323 вызывает identity stop и освобождает объект.
Титульная композиция одна; отдельного музыкального формата и набора music loops
в этих RES нет. Исходное меню выбора уровней RU1008:02F7 / EN1008:044B
не запускает повтор этого PCM. Повтор титула в меню GBA — отдельная адаптация,
которую нельзя выдавать за DOS. В NDS default титул — once; menu-loop требует
отдельно документированного изменения поведения.

Пауза P: poll key0x19 RU10C0:1966→1980 вызывает modal10C0:155D;
EN1050:1CC9→1CE3 вызывает1050:18C0. Modal рисует текст, затем синхронно
ожидает input. В теле нет audio stop/pause/resume и CLI. Изолированное исполнение
обоих оригинальных modal bodies выполнило8 контролируемых input polls до
возврата, оставило active sound flags неизменными и IF включённым.
Text measurement/display и input boundaries явно перехвачены.

**Уровни доказательства:** исходный pause path не останавливает sound и допускает
продолжение аппаратного IRQ. Продолжение слышимого PCM во время DOS-паузы —
вывод из этого пути, отдельного DOS audio capture/прослушивания здесь нет.
Freeze игрового мира NDS по Start — уже принятая адаптация N08. Если NDS
сохраняет текущий аудиопоток при паузе, это соответствует исследованному
отсутствию audio-pause; нельзя объявлять численное равенство audible playhead.

Экран с текстом также вызывает этот титульный once через общий frontend:
RU1008:0602/0656, EN1008:086B/08BF. Ранний frontend вызывает тот же ресурс:
RU1008:0120/0174, EN1008:014F/01A3. RU data1190:0108/0159/01F2
содержат имя `ЗАСТАВКА_ЗВУК`; это offsets строк, не RES indices.
Разбор этих flow-экранов относится к N21/N22; новой музыки они не добавляют.

Hardware DS/DS Lite, полный DOS/DPMI audio capture, Sound Blaster equivalence,
пользовательское прослушивание NDS и отсутствие щелчков на физической DS
этот source audit не подтверждает. GBA listening относится только к GBA.
