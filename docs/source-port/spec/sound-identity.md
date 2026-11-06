# SP13: identity звука — 6 ноября 2026

**Identity — отдельный изменяемый Sound wrapper, а не имя RES и не адрес PCM.** Два actor binding с одинаковым ключом RES получают разные wrappers. `owner/binding` соответствует одновременно живому wrapper поля; равенство RES key не даёт права объединять `get`, `stop(id)` или same-ID gate. При создании нового объекта-клона его wrapper тоже создаётся заново: lifetime нужно отличать от имени ресурса.

## Доказательства и ABI

Все адреса ниже — адреса Ghidra оригиналов (NE/MZ), не файловые смещения и не DPMI-селекторы. Свежий `-readOnly -noanalysis` экспорт: `research/evidence/source-port/sp13-native-complete/sound-identity/ghidra/<profile>/`. SHA-256 EXE перед/после совпал с SP01 и отдельно повторно проверен executable probe. Никакой production-код этот аудит не меняет.

| Операция | PKR | PKE | Old Gold |
|---|---|---|---|
| Getter по имени | 1060:0145 | 1018:18d9 | 19b1:0157 |
| Lookup cached raw | 10e0:01f0 | 1058:0e0a | 276a:01e9 |
| Реализация raw-cache | 10e0:046b | 1058:1085 | 276a:040b |
| New / malloc wrapper | 10a8:0065 | 1038:08b0 | 199e:0080 |
| Sound copy/bind constructor | 1108:00ec | 1060:1632 | 18a6:00f6 |
| Wrapper destructor | 1108:009a | 1060:15da | 18a6:00bb |
| Stop / get active | 1108:026d / 02b9 | 1060:17b3 / 17ff | 18a6:025d / 029d |

Getter принимает far pointer имени (4 байта), возвращает far Sound* в DX:AX. `cdecl16far`, аргументы снимает caller. RU1060:015d вызывает raw-cache, затем **безусловный** `PUSH 24h / CALLF new` в016b/016d и constructor в018d. В cache нет возврата wrapper: он находится выше, отдельно от cache lookup.

PKE сначала выделяет36 байт в1018:18f3/18f5, lookup в1917, constructor в1965; между ними есть compiler exception-registration path. OG19b1:0168 получает raw, в0172/0174 передаёт this=0 фабрике19b1:0182; та в018c/018e выделяет36 байт и в01b2 вызывает18a6:00f6. Ghidra отображает некоторые OG targets как физически эквивалентные1000:xxxx; это алиасы того же MZ кода, а не другой путь.

Copy constructor принимает `(far this, far rawSource)`, 8 байт аргументов. Он копирует header, присваивает `this.flags7 = raw.flags7 | 20h`, а PCM+18 либо берёт из raw+18, либо, при inline data и ненулевом length+14, ставит pointer на `raw+24h`. Таким образом PCM общий, изменяемые header/flags принадлежат wrapper. RU destructor сначала вызывает stop(this), затем при deleting flag освобождает **сам wrapper**; PCM не освобождается. То же видно в PKE/OG destructors. Raw cache хранит resource entry с шагом1eh, loaded flag+18 и allocation pointer+1a; повторное имя возвращает тот же загруженный raw block. Его память освобождает resource/cache lifecycle (например RU10e0:0105), а не actor Sound destructor.

## Связь с actor fields

RU class9 reader вызывает getter в1068:4088 и сохраняет DX:AX вactor+f0, затем снова в40c7 и сохраняет в+f4. PKE аналогично1020:47f1/4830. OG reader1ae1:3a42 вызывает19b1:0157 по отдельности для+f0 и+f4. Generic/base binding+4c и hero+a6 также являются Sound* полями, а не строками RES. Для героя это прямо видно в RU1068:1853 (getter),185e/1863 (DX:AX → actor+a6), сохранённых также в полном SP12 actor export. Имена разбираются reader перед каждым getter-вызовом.

Важно для копий: EN actor clone в1020:2af8..2b84 выделяет новый36-byte wrapper, вызывает1060:1632 с prototype+4c и сохраняет новый pointer в clone+4c. Поэтому совпадение prototype resource не означает равную identity его копий. Этот аудит подтверждает семантику живых wrappers; повторное использование логического projectile slot требует отдельно учитывать создание/уничтожение объекта, если старый voice ещё существует. Он не обосновывает автоматическое склеивание разных lifetime по одному owner ID.

В исходных LEVEL1 обоих Кремлей glyph128/129 используют один f0-ключ (сводка `sound-identity/same-key-definitions.json`; там же аналогичная пара OG). Это конкретные разные определения с одним именем, к которым reader применяет раздельные вызовы getter. Это анализ исходных данных и кода создания, не заявка на natural-RAM наблюдение этих actors.

## Исполнение исходного getter

`check_sound_identity_oracle.py` исполняет экспортированный getter и **настоящий Sound copy constructor** дважды с одним far pointer имени и одним cached raw Sound. На каждом native malloc call проверяется размер24h; allocator отдаёт разные одновременно живые блоки, как требует malloc. Wrapper addresses не назначаются actor bindings в fixture: их возвращает сам getter после своих allocation и constructor calls.

Все3 профиля PASS: по2 lookup, по2 allocation/constructor, разные returned wrapper pointers, одинаковый PCM+18, одинаковые скопированные headers; изменение flags первого wrapper не меняет второй. Проверены native return CS:IP, SP, instruction budget и SHA образцов. Полный trace выполненных адресов и результаты — `sound-identity/report.json`.

Контролируемые границы: name-keyed raw cache возвращает один уже загруженный raw block (его cache-ветвь отдельно доказана ASM); стандартная fresh-allocation семантика malloc; PKE exception-registration helper. Это проверка allocation/identity/binding, а не heap allocator или звукового устройства. Аудио admission/get/stop/IRQ исполняется отдельным SP13 стендом.

```powershell
py -3.14 -B ports/sdl2/tools/export_sound_identity_audit.py
py -3.14 -B ports/sdl2/tools/check_sound_identity_oracle.py
```

Практический вывод: same-ID gate должен сопоставлять текущий wrapper поля, `stop(id)` останавливает именно его; одна PCM/RES-запись может обслуживать несколько независимых identities. Это обосновано созданием wrappers, а не произвольным выбором far pointers в старом audio fixture.
