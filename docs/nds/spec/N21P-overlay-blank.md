# N21P: неизменный пустой слой оружия

В тяжёлом пакете C12 все154 weapon view были hidden, однако каждый кадр всё равно
очищал, flush-ил и передавал39424 B слоя. Независимое исследование:
`N21P-C12-resource-findings.md`. C15 устраняет только повтор hidden→hidden при
подтверждённо пустом VRAM. Видимое оружие всегда проходит прежний полный путь;
visible→hidden делает одну очистку и DMA. После успешной отправки hidden-слоя
ставится blank-valid. Загрузка уровня и `frontend_restore_game` сбрасывают флаг,
поскольку экран мог быть перезаписан оболочкой. Scratch между кадрами не хранится.

Флаг занимает последние2 B прежнего padding `NdsUiRuntime`: ARM sizeof12548 B
не изменился. При `SPRITE_AUDIT` или `N21P_AUDIT` остаётся полный reference-путь.
Публичные функции overlay, их validation и aspect sampling не менялись.

Команда `py -3.14 -B -X utf8 ports/nds/tools/check_overlay_blank.py` извлекает
фактические старый/новый блоки из main, связывает с настоящими двумя overlay
renderer и подменяет только синхронные flush/DMA.4096 кадров включают480 visible
полных проходов,136 внешних invalidation и816 normal skip. Все49152 B VRAM,
включая HUD-tail, совпадают с исходным путём; все audit-режимы передают каждый
кадр. Полное обратное преобразование main/header/frontend ограничивает source
delta этим флагом, условием и двумя точками invalidation. SHA и ARM layout:
`overlay-blank-equivalence.json`, `overlay-blank-ARM-layout.json` в N21P evidence.

Root C15 short-heavy подтвердил functional/audio/native/HUD и DMA/GPU gates.
Средняя overlay-стадия case14 упала1.374→0.0789 мс, включая первую очистку.
CPU case14:31.002→29.906 мс. При этом строгий cadence пока не пройден:
151 интервал по2 VB,2 по3 VB, худшее секундное окно29.42267 FPS. Это измеренный
частичный выигрыш, а не итоговая приёмка N21P.
