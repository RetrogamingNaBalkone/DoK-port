# N23: разбор загрузки DraStic, diagnostic19

Указание пользователя: прекратить повторные прогоны уровней и сосредоточиться
на анализе загрузки. До локализации данного отказа выполнять только относящиеся
к загрузке проверки. Предыдущие all3 PASS в melonDS не доказывали устранение
отказа в DraStic и не являлись основанием для уверенных выводов о его причине.

## Последнее наблюдение и исправление прежних выводов

Фото playtest18b, RG DS/ROCKNIX/DraStic/ES-DE: Stage58/4000,ACK0/1,Ready0,
canonical ACK0,Mirror02C52440,FIFO C400; A7 boot9,Loop68,RX0,At4,VB167,Handler2,
IME1,IE60051,Init1. ROM SHA
`dc2aa5c242e2a0d6c54e0016b66a8c6f47cd0250f07d832917ee0e5e7608b087`.

**Прежний вывод «At4 доказывает зависание BIOS Halt» неверен для этого ROM.**
В playtest18b ARM7_IDLE_BIOS=0: At4 предшествует обычному конечному256-counter
loop. Там нет SWI/Halt. Также прежний At1 сам по себе не доказывал неисправность
FIFO parser: marker мог быть записан до прерывания, после которого main не
продолжился. Конкретные неисправности FIFO/BIOS/DraStic пока не установлены.
Нужен реальный контекст прерванного исполнения, а не новый обход по одному marker.

## Проверка фактических ELF

Новый `ports/nds/tools/performance/inspect_boot_elf.py` читает ELF32LE symbols,
секции и ARM mapping symbols `$a/$t/$d`; literal pools выводятся как DATA,
не принимаются за ARM/Thumb instructions. Списки функций и хеши сохраняются.
Адреса ниже — **runtime virtual addresses custom ARM7/ARM9**, не файловые
смещения и не адреса оригинальных DOS EXE/Ghidra. Оригиналы не анализировались.

Playtest18b ARM7 main0x038002A5,size1680:

- 0x03800374…0x03800384: повторное чтение serial/magic control line;
- 0x0380049A…0x038004AC: seen_serial и вход командного обработчика;
- 0x0380045C…0x03800462: At4 и установка счётчика256 в `[sp+0x3C]`;
- 0x03800464…0x03800478: конечный decrement loop, NOP/MOV r8,r8;
- 0x0380047A: At5, затем возврат к outer loop;
- 0x038029A4: SDK IntrMain; меняет IRQ→SYS для C callback, затем возвращается
  через BIOS. По одному счётчику VB нельзя доказать возобновление main.

ARM9 command.part.0 @0x02020895,size212:

- mailbox copy и flush выполняются до публикации control;
- 0x020208D2/0x020208D6: magic/address, затем flush;
- 0x020208E0: serial, затем второй flush;
- 0x020208FE…0x02020906: чтение ACK из shared status;
- fallback ждёт до8VBlank и возвращает отказ при отсутствии serial.

Это подтверждает выбор RAM transport в собранном ROM и фактический порядок
публикации, но не доказывает, что на устройстве ARM7 увидел эти байты.

## Целевая диагностика контекста

Новый **диагностический**, не объявленный исправлением, ROM `bootdiag19-ru`.
В ARM7 перед SDK IntrMain установлен ARM naked trampoline: до смены stack/mode
сохраняются `[sp_irq+20]` (BIOS-saved LR_irq), `[sp_irq+12]` (saved r3), SPSR_irq.
Затем tail branch в прежний IntrMain; BIOS return LR и stack не меняются,
используются только BIOS-saved r0/r1. Основа ожидаемого frame —
[GBATEK BIOS interrupt handling](https://mgba-emu.github.io/gbatek/#bios-interrupt-handling).
Реальная ABI именно DraStic не предполагается доказанной: на экране выводятся
**raw IRQ LR/R3/PSR**, а значение LR−4 интерпретируется как PC только при
согласованном frame. Эти поля расположены в прежних свободных boot[13…15].

Экран также показывает control magic/serial/address и явный заголовок
`DoK boot diagnostic 19`. Getter читает96bytes boot/control, инвалидируя
только первые64 ARM7-owned bytes. Runtime transport и idle policy не исправлялись
новыми догадками. Снимок нескольких слов не является атомарным snapshot всего CPU.

## Выполнено — только анализ и загрузка

Общие BlocksDS1.24.0/GCC16.2.0, Python3.14 с установленным Capstone. MSYS2 вне
песочницы, `-Werror`. Ни уровни, ни full title waveform в этой итерации не запускались.

```powershell
py -3.14 -B -X utf8 ports/nds/tools/performance/inspect_boot_elf.py build/nds/n23/playtest18b-ru
py -3.14 -B -X utf8 -c "import json,sys; from pathlib import Path; sys.path.insert(0,'ports/nds/tools/performance'); from build import build; f=json.loads(Path('build/nds/n23/playtest18b-ru/build-manifest.json').read_text())['feature_selection']; build('bootdiag19-ru',f)"
py -3.14 -B -X utf8 ports/nds/tools/performance/inspect_boot_elf.py build/nds/n23/bootdiag19-ru
py -3.14 -B -X utf8 ports/nds/tools/performance/run_logged.py build/nds/n23/bootdiag19-ru/boot.log.gz ports/nds/tools/performance/check_audio_boot_steps.py build/nds/n23/bootdiag19-ru
```

Короткая загрузка melonDS DS1.4.0 PASS: ROM доходит до title, IRQ capture имеет
saved LR0x038004A6 → PC0x038004A2, r3=0x59, SPSR0x2000003F (SYS/Thumb).
PC соответствует действительной инструкции этого ELF. Control magic43424D41,
serial2, pointer aligned, RX/ACK наблюдаются. Это проверяет instrumentation
на локальном core; не устанавливает PC или первопричину на DraStic.
В diagnostic19 main0x038002C9,size1692, probe0x038000E8,size32,
IntrMain0x03800D68. На устройстве использовать именно его ELF для адресов.

Попытка проверить доступность локального Linux/DraStic: WSL не установлен,
qemu user runner не найден. Систему и toolchain ради этого не устанавливал.

## Поставка и следующий шаг

Отдельный файл `artifacts/nds/dok-bootdiag19.nds`,10165760bytes, SHA
`ba8e7d7e6c2a3b291799a1b899104776f437a5c19198e85c1bc331bf0e1cf338`.
ARM9 ELF SHA `8c2e14749a568989262b0469db20b8934b0eaa4d61a734d96519c01fef3dd8bb`;
ARM7 ELF SHA `af6fcf79b4bf85f63c8c099fd698c034b710552173ad543acbf91c1eace9daa2`.
Все5NitroFS файлов byte-identical playtest18b; NDS разобран независимо, SHA
совпал с build manifest. `artifacts/nds/dok.nds` оставлен прежним playtest18b,
его запуск в DraStic по последнему фото всё ещё неисправен.
Compiled evidence: `research/evidence/nds/n23/candidates/bootdiag19-ru/`;
analysis/boot evidence: `research/evidence/nds/n23/drastic-loading19/`;
summary: `analysis/nds-drastic-loading19-20261031.json`.

Следующий шаг: получить с устройства IRQ LR/R3/PSR и Ctl/Cmd этой диагностики,
сопоставить с машинным кодом **до следующей функциональной правки**. Альтернатива
для полноценного offline register/RAM анализа — savestate DraStic с экрана отказа.
Не повторять прогоны уровней вместо анализа boot. Следующий ID N23; N23 не
принята, N24 не начата; GBA/SDL2 не менялись.
