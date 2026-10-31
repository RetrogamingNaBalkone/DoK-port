# Playtest16: ARM7 idle Halt и точные loop checkpoints

Фото пользователя playtest15 на RG DS/ROCKNIX/DraStic/ES-DE:
Stage58/Code4000; ACK0/1,Ready0,Seq0,Mirror02C52480,canonical ACK0/Ready0,
FIFO8400; A7 magic37424F41,boot9,Loop1,RX0,step0,Addr0,A7 FIFO C400,
IME1,IE00060051,Init1. Предыдущий ROM SHA
`7f295ef35aa9e77076c1f9135a9c9a984dcde04f3fb2befff541dd7da6da393d`.

Это подтверждает вход ARM7 в main loop, но не завершение первого прохода.
Предыдущая гипотеза «ARM7 вообще не стартовал» больше не соответствует фото.
Нельзя отличить застревание в fifoCheckAddress, producer, ожидании VBlank
или IRQ handler; прежние boot markers не покрывали этот участок. Значение
C400 включает FIFO ERROR, но само по себе не объясняет первопричину.

В простое ARM7 теперь вызывает `swiHalt()` вместо `swiWaitForVBlank()`:
просыпается по разрешённым IRQ FIFO/timer/VBlank без IntrWait software flags.
Producer по-прежнему немедленно продолжает работу, если создал PCM; кодек,
таймеры звука, mailbox и игровой runtime не менялись. Это кандидат обхода
ожидания, не доказательство ошибки реализации DraStic. Busy-spin не добавлен.

В trace16words сохранены все прежние поля. Новые поля, выводимые на ошибке:

- At1: вошёл в fifoCheckAddress;
- At2: polling завершён, далее produce_resident;
- At3: producer вернулся;
- At4: перед Halt;
- At5: Halt вернулся;
- VB: число входов в VBlank handler;
- Handler1: вход перед inputGetAndSend, Handler2: inputGetAndSend вернулся.

Loop/At вместе с VB/Handler сужают следующий анализ. Значения не являются
атомарным снимком всех стадий, но записи отдельных32-bit слов атомарны.
На ошибке запрашивать фото всего экрана, особенно последней строки.

Общие BlocksDS1.24.0/GCC16.2.0/Python3.14; `-Werror`, MSYS2 вне песочницы.
N23 собирает отдельный ARM7 в private WRAM. N18 не перезаписывался.

```powershell
py -3.14 -B -X utf8 -c "import json,sys; from pathlib import Path; sys.path.insert(0,'ports/nds/tools/performance'); from build import build; f=json.loads(Path('build/nds/n23/playtest15-ru/build-manifest.json').read_text())['feature_selection']; build('playtest16-ru',f)"
py -3.14 -B -X utf8 ports/nds/tools/performance/run_logged.py build/nds/n23/playtest16-ru/boot.log.gz ports/nds/tools/performance/check_audio_boot_steps.py build/nds/n23/playtest16-ru
py -3.14 -B -X utf8 ports/nds/tools/performance/run_logged.py build/nds/n23/playtest16-ru/smoke.log.gz ports/nds/tools/run_frontend_emulator.py --target build/nds/n23/playtest16-ru --mode world-smoke --no-storage
py -3.14 -B -X utf8 ports/nds/tools/performance/run_logged.py build/nds/n23/playtest16-ru/live.log.gz ports/nds/tools/run_joint_frontend_emulator.py --target build/nds/n23/playtest16-ru --language 1 --mode live --levels 1 2 3 --steady-presentations 60
py -3.14 -B -X utf8 ports/nds/tools/package_rom.py --target build/nds/n23/playtest16-ru
```

melonDS DS1.4.0 boot observer и world-smoke без DLDI PASS. Observer читает
настоящие ARM7 trace words: boot9, RX/ACK, а затем за3кадра проверяет продвижение
Loop и VB. Сводка содержит реальные значения и результат all3. Host IPC не
повторялся: ARM9 transport/structs не менялись. DraStic/RG DS локально нет;
проверка Halt на устройстве, физическая DS и полная приёмка N23 открыты.

ROM `artifacts/nds/dok.nds`,10165760bytes, SHA
`9426cdb8ace55956968672ebc875c8cfb4f05c1f810e79ac64acb088c500159f`.
ARM9 ELF SHA `067ab8ee7e407b55300c97e066adc8a3a1f3b56a2f4aca3f4e3e1bad1bb7ea82`.
Compiled evidence — `research/evidence/nds/n23/candidates/playtest16-ru/`;
отчёты — `research/evidence/nds/n23/playtest16-drastic/`;
сводка — `analysis/nds-playtest16-drastic-20261031.json`.

Следующий шаг: холодный запуск в том же окружении; при отказе получить
At/VB/Handler и выбрать исследование по последнему подтверждённому шагу.
Следующий ID N23; N23 не принята, N24 не начата. GBA/SDL2 не менялись.
