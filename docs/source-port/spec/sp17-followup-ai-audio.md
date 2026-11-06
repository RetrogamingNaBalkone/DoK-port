# Проверки SP12/SP13 после SP17 — 5 ноября 2026

Проверка отдельным субагентом по запросу пользователя. Производственные
алгоритмы AI и игровые параметры не менялись. Доступные автоматические
проверки выполнены; непроверенные native-ветви не объявлены закрытыми.

## Результаты

| Область | Фактический результат |
|---|---|
| AI damage/wake | 630 x86/C++ векторов, 0 различий |
| Death-height | 96 x86/C++ векторов, 0 различий |
| AI arenas | 47 групп собственных параметров OG/PKR/PKE, 42 FPS/Draw повтора |
| Прозрачный труп | 24 сценария движения, MMap, двери и сохранения длительности/audio |
| Настоящий renderer | software anchor 92→107, GL 189→219 при старой/исправленной базе OG |
| Audio native | 48 wrapper/admission/IRQ и 42 DSP/PIT rate вектора, 0 различий |
| PCM | Все 56 исходных звуков, 0 цифровых различий |
| SDL | pause/identity/gain/restore и фактические музыкальные экраны трёх игр PASS |
| Дополнительная event fixture | 7 уровней, 11 448 команд, переданных игровыми callbacks до реального SDL voice |
| Negotiated rate | 9 успешных преобразований (22 050/44 100/96 000 × 3 набора), 3 отказа переполнения без изменения голоса |

Доказательства: `research/evidence/source-port/sp17-followup-ai-audio/`.
`report.json` содержит исходные SHA-256 до/после, хеш fixture и производственного
audio-кода, счётчики и ограничения. Все исходные EXE/RES неизменны. Ghidra-проекты
не открывались заново: использованы ранее экспортированные исходные инструкции,
проверяемые manifest/hash в существующих oracle. Адресная система и внешние stubs
описаны в [monster-audit](monster-audit.md) и [audio-audit](audio-audit.md).

## Новая матрица игровых событий

Локальный `followup_probe.cpp` скомпилирован общим Zig и слинкован с текущими
`libdok_core.a`/`libdok_sdl_input.a`. Он использует исходные LEVEL-определения,
но контролируемые карты/позы/характеристики героя для изоляции каждого потребителя.
Это не прохождение исходных карт и не новый DOS-эталон порядка игровых событий.

Проверены 129 дверей, 27 запертых дверей, 79 подборов, 530 определений монстров,
14 оружий и 25 движущихся projectile-шаблонов всех семи уровней. `World::audio`
перехватывает фактические запросы игровых методов. Непустые ресурсы декодируются,
каждый once/loop передаётся в реально открытый SDL dummy device; сверяются
identity, ключ ресурса и loop. Stop/global stop также выполняются. Уже отдельная
основная audio-проверка покрывает busy-admission и отказ несовместимого ресурса.

Проверяются wake/attack/death/stop монстров, hurt/death героя, pickup, weapon fire,
locked-door и door loop/stop, projectile contact/stop. Неподвижные class8-шаблоны
не выдаются за движущиеся снаряды. Выход OG с собранными деньгами не обязан
издавать pickup-звук: fixture проверяет звуковую ветвь отказа при нехватке денег.

## Исправление переполнения фазы

SP17 преобразует сохранённую фазу между negotiated output rates. Найден
пограничный случай: saved rate 44 100, current rate 48 000,
phase `16947946117720680299`. Проверка только целой части пропускала сумму
`18446744073709583998`, которая оборачивалась в `32382` и принималась как
допустимая короткая фаза. Добавлен guard `fraction > UINT64_MAX - base`.

Новый тест в `audio_probe.cpp` сначала фактически упал на прежнем коде:
`rate-regression-before.log`, «Cross-rate addition overflow admitted».
После точечной правки `sdl_audio.cpp` он проходит на всех трёх наборах:
`rate-regression-after.log`. Проверяются также успешное рациональное
преобразование и сохранность playhead при отказе. Производственный AI не менялся.

`check_audio.py` адаптирован к новому маршруту SP17:
заставка → меню датасета → новая игра → вступление/уровень. Полная команда
повторно прошла после исправления фазы. Старое описание audio-audit о безопасном
отказе при другом output rate заменено для SP17 этим проверенным преобразованием.

## Воспроизведение

После актуальной Release-сборки из корня:

```powershell
py -3.14 -X utf8 -B ports/sdl2/tools/check_monster_oracle.py
build/sdl2/windows/dok_monster_probe.exe originals/Old_Gold_3D/OLD_GOLD.RES originals/pkr/RUS/PKR.RES originals/pkr/ENG/PKE.RES
build/sdl2/windows/dok_invisible_corpse_probe.exe originals/pkr/RUS/PKR.RES originals/pkr/ENG/PKE.RES
build/sdl2/windows/dok_monster_scene_probe.exe originals/Old_Gold_3D/OLD_GOLD.RES research/evidence/source-port/sp17-followup-ai-audio/render
py -3.14 -X utf8 -B ports/sdl2/tools/check_audio.py
```

Воспроизводимая локальная event fixture:

```powershell
$taskZig=Join-Path $env:LOCALAPPDATA 'Programs/Zig/0.13.0/zig.exe'
$taskSdk=Join-Path $env:LOCALAPPDATA 'Programs/SDL2/SDL2-2.32.10/x86_64-w64-mingw32'
& $taskZig c++ -target x86_64-windows-gnu -O2 -std=c++17 -DSDL_MAIN_HANDLED '-Iports/sdl2/include' "-I$taskSdk/include/SDL2" research/evidence/source-port/sp17-followup-ai-audio/followup_probe.cpp build/sdl2/windows/libdok_sdl_input.a build/sdl2/windows/libdok_core.a "$taskSdk/lib/libSDL2.dll.a" -lshell32 -o build/sdl2/windows/dok_audio_followup_probe.exe
build/sdl2/windows/dok_audio_followup_probe.exe originals/Old_Gold_3D/OLD_GOLD.RES originals/pkr/RUS/PKR.RES originals/pkr/ENG/PKE.RES
```

## Оставшаяся граница

Программное исследование SP12: полный native AI callback, включая LOS,
обход препятствий, смену курса и связанные ветви, не доказан hit/death oracle.
Программное исследование SP13: точная последовательность всех игровых sound
events относительно DOS и точная mixer/clock-эквивалентность не установлены.
Новые арены и event fixture этих утверждений не заменяют. Сохранённые DOS title
записи использованы повторно; новая DOS audio capture не выполнялась.

Ручные и аппаратные пункты передаются в SP18: полная субъективная игровая
приёмка AI/боя и слышимости игровых событий на реальном устройстве вывода,
включая pause/load/resume. Аналоговая Sound Blaster-карта и точная аппаратная
latency остаются границей заявляемой эквивалентности, а не результатом dummy SDL.
