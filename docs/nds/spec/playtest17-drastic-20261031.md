# Playtest17: обход ARM7 BIOS idle wait

Фото playtest16 RG DS/ROCKNIX/DraStic/ES-DE: Stage58/4000, ACK0/1, Ready0,
canonical ACK0; A7 boot9,Loop1,RX0,At4,VB168,Handler2,IME1,Init1,
IE00060051,A7 FIFO C400. Предыдущий ROM SHA
`9426cdb8ace55956968672ebc875c8cfb4f05c1f810e79ac64acb088c500159f`.

Снимок показывает: polling и producer уже вернулись, ARM7 остаётся перед
возвратом из swiHalt. VBlank IRQ исполняется много раз, inputGetAndSend возвращается.
Следовательно, недостаточно говорить «IRQ не работают» или «ARM7 не стартовал».
Ошибка локализована до BIOS idle wait/возврата из него, но её внутренний механизм
в DraStic ещё не воспроизведён локально.

В N23-кандидате ARM7_IDLE_BIOS=0: idle использует ограниченный цикл256итераций
с volatile счётчиком и NOP. SWI/Halt на этом пути отсутствует, основной loop
продолжает polling. IRQ, producer, mailbox и звук сохранены. Прямую запись
HALTCNT не добавляли: установленный libnds system.h помечает доступ BIOS-only;
она не нужна для проверки обхода. У старых целей default ARM7_IDLE_BIOS=1,
N23 явно передаёт0 и фиксирует `arm7_idle_bios=false` в manifest.

Это проверочная мера совместимости с повышенной активностью ARM7 в простое;
потребление и влияние на производительность реальной DS не измерены. Не
объявлять этот путь оптимальным аппаратным решением N24. Устранение проблемы
именно на RG DS требует повторного запуска. Busy loop ограничен, прерывания
не блокируются, произвольной задержки игрового времени нет.

Общие BlocksDS1.24.0/GCC16.2.0/Python3.14, `-Werror`, общий MSYS2 вне песочницы.
Capstone из общего Python использован для чтения реального ARM7 ELF: main
0x03800569,896bytes, SVC/SWI инструкций внутри main нет. Это не утверждение,
что все вызываемые SDK-функции не содержат SWI. Отчёт `arm7-idle-check.json`.
N18 ELF сохранён, GBA/SDL2 и оригиналы не менялись.

```powershell
py -3.14 -B -X utf8 -c "import json,sys; from pathlib import Path; sys.path.insert(0,'ports/nds/tools/performance'); from build import build; f=json.loads(Path('build/nds/n23/playtest16-ru/build-manifest.json').read_text())['feature_selection']; build('playtest17-ru',f)"
py -3.14 -B -X utf8 ports/nds/tools/performance/run_logged.py build/nds/n23/playtest17-ru/boot.log.gz ports/nds/tools/performance/check_audio_boot_steps.py build/nds/n23/playtest17-ru
py -3.14 -B -X utf8 ports/nds/tools/performance/run_logged.py build/nds/n23/playtest17-ru/smoke.log.gz ports/nds/tools/run_frontend_emulator.py --target build/nds/n23/playtest17-ru --mode world-smoke --no-storage
py -3.14 -B -X utf8 ports/nds/tools/performance/run_logged.py build/nds/n23/playtest17-ru/live.log.gz ports/nds/tools/run_joint_frontend_emulator.py --target build/nds/n23/playtest17-ru --language 1 --mode live --levels 1 2 3 --steady-presentations 60
py -3.14 -B -X utf8 ports/nds/tools/package_rom.py --target build/nds/n23/playtest17-ru
```

melonDS DS1.4.0 boot progression и world-smoke без DLDI PASS, all3 — в сводке.
Read-only observer проверяет рост Loop/VB, ARM7 callback/ACK и успешную загрузку.
Host IPC не повторялся, т.к. ARM9 transport/structs неизменны. Реальная DS и
DraStic здесь недоступны; full N23 FPS/полные маршруты не закрываются.

ROM `artifacts/nds/dok.nds`,10165760bytes, SHA
`0619e840622cddd34f8cd1b45cd53c429c2219a14d76303e8306b08e5d8f65a4`.
ARM9 ELF SHA `54eb97d0112f286f051de1118765bbf08ac88e857e521d5650a014dd096f3fc3`.
ARM7 ELF SHA `0b13daf47ec7145aefe6a80a742a5f731a57ac69813d1d9c3ccb732f9bd7ac62`.
Compiled evidence — `research/evidence/nds/n23/candidates/playtest17-ru/`;
отчёты — `research/evidence/nds/n23/playtest17-drastic/`;
сводка — `analysis/nds-playtest17-drastic-20261031.json`.

Следующий шаг: холодный запуск на том же устройстве. При успехе проверить звук
и вход на уровень; при отказе получить весь диагностический экран. Если запуск
восстановится, отдельно исследовать экономичный idle, не скрывая текущую цену
обхода. Следующий ID N23; N23 не принята, N24 не начата.
