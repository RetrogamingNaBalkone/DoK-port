# N23: loading20 — устранение блокирующей отправки системного ввода

Фото diagnostic19 на RG DS/ROCKNIX/DraStic/ES-DE: IRQ LR03801F8C,R3=00008404,
SPSR6000003F; Ctl43424D41,serial1,Cmd02052440; boot9,Loop68,RX0,At4,
VB168,Handler2,Stage58/4000. Образец19 SHA
`ba8e7d7e6c2a3b291799a1b899104776f437a5c19198e85c1bc331bf0e1cf338`.

## Установленная точка и границы вывода

Actual diagnostic19 ELF: LR−4=**0x03801F88**, внутри
`fifo_buffer_wait_block`0x03801F69,size64. Это ARM7 runtime address custom ELF,
не адрес DOS/Ghidra. Инструкция LSLS формирует IRQ_SEND_FIFO перед вызовом
ожидания; предыдущие инструкции читают FIFO_CR и OR send-empty IRQ.
Сохранённый r3=8404 соответствует этому значению. Функция вызывает allocator,
повторяет попытку при отсутствии свободного блока, включает IME и ждёт IRQ.
Это конкретная точка блокировки системного SDK, а не idle main/наш decoder.
Главный marker At4 остаётся от main, прерванного до этой вложенной обработки.

Вызовы из `inputGetAndSend` идут по цепочке fifoSendDatamsg → fifoInternalSend
→ fifo_buffer_wait_block. ARM7 вызывает inputGetAndSend каждый VBlank ещё
до готовности ARM9/audio. Код SDK и фактическая цепочка проверены по ELF;
почему именно в DraStic pool/доставка не освобождаются корректно, пока не
установлено. Не объявлять width/JIT/BIOS конкретной причиной без доказательств.

## Исправление пути ввода

Linker wrapper ARM7 перехватывает только FIFO_SYSTEM/SYS_INPUT_MESSAGE точного
SDK размера. **Сам inputGetAndSend сохранён**: touch filtering, calibration,
debounce и raw X/Y/lid bits остаются SDK. Готовые touch12bytes/keys вместо FIFO
публикуются в ARM7-owned32-byte RAM input mailbox с odd/even sequence.
Все прочие datamsg направляются исходному SDK; системные службы не отключены.

ARM9 wrapper scanKeys читает согласованный bounded snapshot (до8попыток),
передаёт его в SDK setTransferInputData и вызывает исходный scanKeys.
Клавишные edge/held/repeat и touchRead сохраняют SDK-поведение.
В pinned BlocksDS1.24 setTransferInputData ABI проверен по ASM: r0=touchPosition*,
r1=raw key WORD, копирует12bytes и WORD. Это внутренняя SDK entry point,
при обновлении SDK её ABI следует перепроверить. Новая platform source:
`platform/audio/input_bridge.c`.

Boot region128bytes: ARM7 trace64, ARM9 audio-control32, ARM7 input32. Адрес
получается из ARM7 ELF; cache-line ownership не смешивается. ARM7 publication
critical относительно IRQ; ARM9 только инвалидирует input line и читает.
Audio RAM mailbox и диагностический IRQ capture сохранены. Busy idle пока
остаётся с известной ценой повышенной активности; hardware power не измерен.

## Проверка только загрузки

Общие BlocksDS1.24.0/GCC16.2.0/Python3.14, `-Werror`, MSYS2 вне песочницы.
Никакие уровни, маршруты или full title waveform этой итерацией не запускались.

```powershell
py -3.14 -B -X utf8 -c "import json,sys; from pathlib import Path; sys.path.insert(0,'ports/nds/tools/performance'); from build import build; f=json.loads(Path('build/nds/n23/bootdiag19-ru/build-manifest.json').read_text())['feature_selection']; build('loading20-ru',f)"
py -3.14 -B -X utf8 ports/nds/tools/performance/inspect_boot_elf.py build/nds/n23/loading20-ru
py -3.14 -B -X utf8 ports/nds/tools/performance/run_logged.py build/nds/n23/loading20-ru/boot.log.gz ports/nds/tools/performance/check_audio_boot_steps.py build/nds/n23/loading20-ru
py -3.14 -B -X utf8 ports/nds/tools/performance/run_logged.py build/nds/n23/loading20-ru/boot-input.log.gz ports/nds/tools/performance/check_boot_input.py build/nds/n23/loading20-ru
py -3.14 -B -X utf8 ports/nds/tools/package_rom.py --target build/nds/n23/loading20-ru
```

melonDS DS1.4.0 boot PASS. Title soak240frames: input mailbox получил240новых
сообщений, ARM7 loop продолжает работу, UI остаётся в title. Через настоящий
emulated input X/Y/touch дают SDK keys1C00, calibrated touch(120,80), release0.
Это тест startup transport, не unit test игровых уровней.
В actual ARM7 binary inputGetAndSend BL направлен на RAM wrapper0x038002C8,
на ARM9 присутствует wrap_scanKeys0x02027049 и SDK setter0x0202B8B1.
Бинарная проверка исключает предположение, что linker wrap лишь записан во flags.
На RG DS исправление ещё не подтверждено; локального DraStic нет.

Поставка `artifacts/nds/dok.nds` и именной `artifacts/nds/dok-loading20.nds`,
10165760bytes,SHA `332627c7a265fad6578da7c301e1ed8cc344c94f019f7addf95c414ebf243b8e`.
На ошибке заголовок `DoK loading candidate 20` и счётчик Input.
ARM9 ELF SHA `4871ee08750b5213eb72caa6ebe59936fdb577f04d4bb6405117f90bbae284a8`;
ARM7 ELF SHA `174facf41e185ca188fe1c9f9dacbbc8de385e76d8cbc0a8e5e7f3262617d405`.
Compiled evidence — `research/evidence/nds/n23/candidates/loading20-ru/`;
boot/code evidence — `research/evidence/nds/n23/drastic-loading20/`;
summary — `analysis/nds-drastic-loading20-20261031.json`.

Следующий шаг: холодный запуск loading20 на том же RG DS. При успехе проверить
управление/звук и затем убрать лишние диагностические/idle обходы обоснованно.
При отказе получить IRQ LR/R3/PSR и Input; продолжать адресный boot analysis.
Не повторять уровневые проверки до устранения отказа. Следующий ID N23;
N23 не принята,N24 не начата; N18 artifact/GBA/SDL2 не менялись.
