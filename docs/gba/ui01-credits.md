# UI01: «Об авторах» вместо «Выхода»

29 октября 2026 года, уточнение G20B по пользовательскому плейтесту G21.
Главное меню: «Начать игру», «Язык», «Управление», «Предыстория»,
«Об авторах» / Credits. Раздел содержит ровно два экрана:

1. Исходный ENDTXT: копирайт NewCom, Москва, 1995; авторы трёхмерной
   технологии Александр и Андрей Разбаковы; художник Дмитрий Цирухин.
   Английские имена и формулировки берутся из PKE без перевода заново.
2. `GBA-порт` и `Ретрогейминг на балконе`; в английской локали
   `GBA port by` и `Retrogaming Na Balkone` с указанным пользователем написанием.

A/Start открывают следующую страницу, со второй возвращают в меню.
B со второй страницы возвращает на первую, с первой — в меню.
Select меняет локаль, сохраняя номер страницы. Фокус возвращается на
«Об авторах». Музыка меню не останавливается и не начинается заново.
Раздел больше не закрывает сессию и не очищает мир, события или снаряды.
Это намеренная GBA-адаптация по запросу пользователя, а не восстановление
действия выхода из DOS. Новый мир по-прежнему создаётся через «Начать игру».

## Источники и реализация

ENDTXT — запись 13 исходного RES. Ниже указаны **файловые смещения**
ресурса, а не адреса Ghidra, номера NE-сегментов или DPMI-селекторы:

| Образец | Смещение | Размер | SHA-256 ресурса |
|---|---:|---:|---|
| PKR.RES, RUS | 390111 | 227 | `12c2b6e8f4f284f3d33e7add89450f7dfb00e90f30d08c7cc6eba8ad014c4fb1` |
| PKE.RES, ENG | 390252 | 219 | `19438eb8ceab0925cc85d9287acca7301e2e944b79a08c5de38ee096f379dc52` |

Каталоги — `analysis/originals/resources-{ru,en}.json`, исходные распаковки —
`research/evidence/resources/{ru,en}/0013.bin`. Оригиналы не изменялись;
нового исследования EXE не проводилось. Первая страница читает прежний
заимствованный ROM view, вторая — статическую локализованную строку CP866.
Обе используют исходные FONT8/FONT16, существующие рамку и палитру,
поля 8 px и перенос целых слов. Текст и размер букв не уменьшаются.

В `platform/flow_backend.c` сохранён диагностический ID 5; теперь это
двухстраничный раздел авторов. Удалены прежние сброс сессии и pending=5
с вызовом `gba_flow_quiet`. UI сохраняет состояние TITLE для регистрации
коротких нажатий через IRQ; flow-экраны не заполняют кеш статического меню.
`src/ui.c` разделяет номер страницы для вывода текста и счётчик 1/2, 2/2.
`ui_copy.py` генерирует пункт меню и подпись порта в `src/ui_text.inc`.
Дополнительного framebuffer или изменяемого текстового буфера нет.

## Сборка и проверки

Текущая ROM: `artifacts/gba/DoK-port_UI01.gba`, 26645528 байт, SHA-256
`cf30cf8ab1e21c35b2da1f13d3fb5cda8224b27efe32041ac9893fce5727df57`.
ROM/ELF/map, журналы, PNG и полные отчёты — `build/gba/ui01-credits/`.
Компактный итог — `analysis/gba/ui01-credits-20261029.json`.

```powershell
py -3.14 -X utf8 ports/gba/scripts/ui_copy.py
py -3.14 -X utf8 ports/gba/scripts/build_gba_frontend.py --out build/gba/ui01-credits
py -3.14 -X utf8 ports/gba/scripts/build_gba_frontend.py --out build/gba/ui01-credits-repeat
$gbaCreditsBuild = 'build/gba/ui01-credits/hardware/hw_v128_entity_rect'
py -3.14 -X utf8 ports/gba/scripts/verify_credits_gba.py --build-dir $gbaCreditsBuild --report build/gba/ui01-credits/credits.json
py -3.14 -X utf8 ports/gba/scripts/verify_flow_gba.py --build-dir $gbaCreditsBuild --report build/gba/ui01-credits/flow.json
py -3.14 -X utf8 ports/gba/scripts/verify_story_gba.py --build-dir $gbaCreditsBuild --report build/gba/ui01-credits/story.json
py -3.14 -X utf8 ports/gba/scripts/verify_ui_gba.py --build-dir $gbaCreditsBuild --report build/gba/ui01-credits/ui.json
py -3.14 -X utf8 ports/gba/scripts/verify_frontend_gba.py --build-dir $gbaCreditsBuild --report build/gba/ui01-credits/frontend.json
py -3.14 -X utf8 ports/gba/scripts/verify_frontend_latency.py --build-dir $gbaCreditsBuild --report build/gba/ui01-credits/frontend_latency.json
```

Windows host UI выполнен; Linux/Thumb скомпилированы. В mGBA проверены
обе страницы RU/EN по пикселям исходного FONT8, подпись порта, счётчик,
перелистывание, смена локали на каждой странице и 54 выхода через B/A/Start:
однокадровые нажатия в восьми фазах и удержание. После каждого выхода
сравниваются все пиксели и палитра меню. Мир, session и resets сохраняются;
музыка не прерывается. PNG обоих языков просмотрены.

Flow прошёл все шесть миров, исходные вступления/финалы, смерть, retry,
Game Over и создание нового мира. Отдельно проверено открытие авторов
поверх приостановленного мира с событиями и снарядами: они сохраняются.
Предыстория и UI прошли; 16 фазовых проверок коротких A/Start:
boot 10 VBlank, максимальная задержка UI 5 кадров.
Frontend проверил исходную заставку/палитру, выбор уровней, удержание кнопок
и два полных повтора музыки без underrun. Повторная сборка в отдельном
каталоге даёт ту же ROM побайтно.
Повторные `ui01-credits-repeat/base` и `hardware` после сравнения удалены;
манифест и журналы repeat сохранены. Текущие ROM/ELF/map остаются в основной сборке.

Физическая GBA и полное прохождение не проверены. G20B готово, G21 в работе,
бюджет G20 открыт; следующий ID — **G21**, затем G22. NDS/SDL2 не затронуты.
