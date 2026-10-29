# N17: полный бой и смерть

29 октября 2026. Готово (эмулятор).

Основа — N14 read-only Ghidra PKR/PKE export с SHA/ABI; source supersession
N17 сохраняет N16 AI, N15 combat и N12 sprites до расширения. GBA не основа.
Адреса ниже — Ghidra NE loader: active1068:3845/1020:3FAE,
melee1068:3910…39FE /1020:4079…4167; burst1068:3AB4…3B86 /
1020:421D…42EF; death1068:3D98…3EC2 /1020:4501…462B;
sight1068:1276/1020:1780; hit1068:37CF/1020:3F38.

Принято: один исходный animation counter для переходов walk/attack/hurt,
отдельно displayed и next. Next не сворачивается на границе walking sequence
до следующего accessor: иначе hurt при переходе к более длинной attack sequence
получал бы неверный кадр. Первый hit sleeping только будит; health0 уже не
поглощает projectile даже до corpse cleanup. Активный SUB16/clamp ставит state3.

Контакт class1 проверяется по старому heading, движение останавливается.
Attack запускается с0 при переходе1→2; после последнего interval следующий
callback наносит hero5 HP и снова показывает attack0. Hurt при контакте сначала
исполняет attack step/возможный melee, затем death0 без запуска смерти, state2.
Без контакта временный death0 перезаписывается ordinary walking в том же callback.

Burst: unsigned elapsed/10000 строго больше sign-extended WORD delay. Signed
shots<limit, front class!=8/9 и source sight; shots++ до allocation, counter=N.
Refusal allocation не возвращает место в серии. Неудачный guard после порога
перезапускает timer/обнуляет shots. Между shots действуют walking intervals.
Sight —100-unit advancing MMap, затем ещё100 TMap, максимум5000; self identity
пропускает второй lookup, hero проверяется до flags. Это не новый LOS/raycast.

Death использует signed WORD interval; cleanup после последнего callback.
Отрицательный последний interval (0x86a0, −31072) при раннем clock даёт
unsigned deadline после wrap и задерживает cleanup. Это проверено отдельно.
Visual height0 задаётся отдельным corpse bitset, definition не меняется.
Только текущая MMap-клетка очищается, corpse в TMap при class0, иначе MMap.
Flags OR16, bit400 не добавляется; последний кадр/slot остаются. Событие AI
отменяется; DEAD/text/sound-stop output однократный. Уже выстреленные снаряды
сохраняют существующего corpse owner; world clear уничтожает их и все services.
Мёртвый герой не принимает новые fire commands. Game Over/restart — N22,
здесь только typed HERO_DEAD output, без дискового checkpoint.

`dok_sprites_gather_live` принимает optional runtime flags/corpse bits;
прежний N12 gather вызывается сNULL и сохраняет свой результат. Scene lease
исключает параллельную симуляцию; UI/audio outputs не управляют gameplay timing.


## Проверки и воспроизведение

`$PY` — общий Python3.14.6: `C:/Users/dspev/AppData/Local/Python/pythoncore-3.14-64/python.exe`.

```powershell
& $PY -B -X utf8 ports/nds/tools/check_ai_combat.py
& $PY -B -X utf8 ports/nds/tools/build_gameplay.py n17
& $PY -B -X utf8 ports/nds/tools/run_gameplay_gate.py n17 ai_combat 0x17434b44
```

Host и ARM9:11503 checks,268 scenarios,2060 callbacks,trace3001418237,
13 семейств NDS-профиля/217 размещённых определений на язык;104 deaths,
104 melee,140 ranged attempts. Все четыре RU/EN×30/60 observation runs
одинаковы. Native oracle исполняет каждый callback active PKR/PKE с реальным
compiler memcpy и вложенным hero event/stat; сверяет33 поля transcript.
Дополнительно4094 burst/SUB vectors и14 sight scripts. Неизвестные far calls
отклоняются. Clock, lookup/move, allocation, animation accessor, audio/UI —
явные контролируемые границы; это не полная DOS-сессия. Свежий Ghidra compiler
export1000:5CB3, SHA EXE/RES до/после, ABI/manifests в research/evidence/nds/n17/.

Регрессии под новыми N17 outputs: AI94532 checks (trace2560806509 изменился
из-за исправленного shared cursor), combat313608/trace1888275799,
sprites24451 checks. Старые N14–N16 evidence pins проверены и сохранены.
Linux static linked, не исполнен. ARM9 ai9844 B, workspace1676228 B;
4113 frontend frames, FAT SHA неизменен. SDK BlocksDS1.24.0-1/GCC16.2.0,
Zig0.13.0, Unicorn2.1.4; melonDS DS1.4.0. ROM188416 B.
Полные hashes: analysis/nds-n17-ai-combat.json.

Совместный бюджет renderer проверяет N17P. Физическая DS/DS Lite — N24.
Меню Game Over/повтор мира — N22. Игровых сохранений нет.
