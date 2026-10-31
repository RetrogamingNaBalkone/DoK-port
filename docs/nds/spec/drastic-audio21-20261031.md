# N23: audio21 — завершение звуков и исключение хвоста заставки

31 октября 2026 пользователь подтвердил: loading20 запускает игру на RG DS,
ROCKNIX/DraStic через ES-DE. Затем сообщил о фрагментах мелодии заставки в
уровнях; они прекращаются во время звуков дверей/выстрелов. Многие звуки
выстрелов пропускаются. Это подтверждение загрузки, не приёмка аудио.

## Установленные факты и границы

В текущем ARM7 все звуки, включая короткие once, использовали SOUND_REPEAT
и длину кольца 65536. Resident decoder включал канал после первых 1024 samples,
а конец once обеспечивал timer1 IRQ. Streamed title и эффекты используют
один PCM ring. STOP выключал канал и таймеры, но оставлял PCM заставки в RAM.
Это проверенные свойства исходного кода, но **не доказанная внутренняя причина
ошибки DraStic**. Локального DraStic нет; в melonDS20 фрагменты не воспроизведены.

Пропуски выстрелов не объявлены исправленными. Оригинал имеет один голос и
отвергает новый play, пока активен предыдущий: native evidence, образцы,
адресная система и ASM в [N18-audio-evidence.md](N18-audio-evidence.md), раздел
Sound36/admission. Правила приоритетов, sound binding, shot triggers и
полифония в этой итерации не менялись. Нельзя автоматически считать все
пользовательские пропуски следствием этого исходного правила.

## Кандидат

Изменён только ARM7 audio lifecycle в `platform/audio/arm7.c`:

- При остановке streamed voice PCM ring очищается после выключения канала и
  таймеров. Это исключает сохранённый хвост заставки в последующих эффектах.
- Resident once целиком декодируется до включения канала. SOUND_ONE_SHOT,
  LENGTH=ceil(samples/4), хвост слова нулевой; hardware не повторяет старый
  64 КБ ring за пределами эффекта. Сэмплы и частота остаются исходными.
- ARM7 main проверяет аппаратное завершение once. Если канал уже выключен,
  status/remaining доводятся до конца и admission освобождается даже при
  задержанном timer IRQ. IRQ продолжает точный учёт в обычном пути.
- Streamed title и resident loops сохраняют ring transport. RAM input fix20,
  command mailbox, сборочные параметры и игровой код сохранены.

Очистка ring выполняется ARM7 при stop streamed mode; это дополнительные
65536 bytes writes в critical section. Её аппаратная latency не измерена.
Отложенный IRQ специально не инжектировался: fallback проверен кодом и обычным
запуском, но отдельное fault-injection доказательство не заявляется.

## Проверки и поставка

Общий BlocksDS1.24/GCC16.2, -Werror, MSYS2 вне песочницы. Финальный build
`audio21b-ru`; промежуточный `audio21-ru` не поставляется.

```powershell
py -3.14 -B -X utf8 -c "import json,sys; from pathlib import Path; sys.path.insert(0,'ports/nds/tools/performance'); from build import build; f=json.loads(Path('build/nds/n23/loading20-ru/build-manifest.json').read_text())['feature_selection']; build('audio21b-ru',f)"
py -3.14 -B -X utf8 ports/nds/tools/performance/run_logged.py build/nds/n23/audio21b-ru/audio-transition.log.gz ports/nds/tools/performance/check_audio_transition.py build/nds/n23/audio21b-ru
py -3.14 -B -X utf8 ports/nds/tools/performance/run_logged.py build/nds/n23/audio21b-ru/boot-input.log.gz ports/nds/tools/performance/check_boot_input.py build/nds/n23/audio21b-ru
py -3.14 -B -X utf8 ports/nds/tools/performance/run_logged.py build/nds/n23/audio21b-ru/title-audio.log.gz ports/nds/tools/performance/check_title_audio.py build/nds/n23/audio21b-ru
py -3.14 -B -X utf8 ports/nds/tools/package_rom.py --target build/nds/n23/audio21b-ru
```

Pinned melonDS DS1.4:

- Boot/title 240 frames/X/Y/touch PASS.
- Полная waveform заставки PASS (все исходные samples, correlation >0.99).
- Отдельный короткий переход title→menu→LEVEL1: STOP, ring 65536 bytes=0,
  начальный once resource89/18075samples/22050Hz, waveform correlation0.99956277,
  underrun/stale/fault0. Никаких маршрутов и прогона всех уровней.
- Проверка нажатия R после входа дала 0 sound requests, потому что герой начинает
  без оружия. Это **не проверка выстрелов**. Первый вариант observer ошибочно
  требовал несколько starts; FAIL сохранён в intermediate audio21 logs,
  исправлен тест, игровая логика ради теста не менялась.

ROM `artifacts/nds/dok.nds` и `artifacts/nds/dok-audio21.nds`, 10166784 bytes,
SHA `7df6f228302c80b4dacfeb7a3011b34b4e4d08896eb63b7e5cd87a5ee60e9c18`.
ELF ARM9 `f058e49785d9fe57a714c706bfef123c9160641637e5928c0175a8f9db473ae1`.
Summary `analysis/nds-drastic-audio21-20261031.json`, reports/PCM evidence
`research/evidence/nds/n23/drastic-audio21/`.
N18 accepted ELF hash825e93dc… unchanged. GBA/SDL2/originals не менялись.

Следующий ID N23: повтор audio21 на RG DS без savestate; проверить остатки
заставки, завершение дверей и несколько выстрелов после получения оружия.
Если проблема остаётся, нужны actual channel/PCM/status данные устройства;
не объявлять FIFO/кэш/таймер установленной причиной по одному симптому.
N23 не принята, N24 не начата.
