# SP15 R5 — компактная компоновка, карта OG и течение

4 ноября 2026 года. Прямое уточнение SP15; SP22 целиком не запускалась.

## Направление и аудит

- Продукт: меню трёх игр для клавиатуры, мыши и геймпада.
- Характер: сдержанный, плотный, игровой.
- Мир: камень, тёмные панели и исходная пиксельная графика.
- Акцентные места: оригинальная иллюстрация и выбранное действие.
- Отличительная деталь: существующий жёлтый указатель фокуса.

Аудит: фиксированная полная высота панелей разносила сообщение и действия;
footer резервировал пустую подсказку; единственная страница имела Next page/1/1.
Методика: Unslop, Damir Sokolovsky (@heydamir), CC BY 4.0,
https://heydamir.design/unslop. Атрибуция уже включена в NOTICES.txt.

## Изменения

Высота меню, диалогов, справки, титров и вступлений измеряется по содержимому.
Полученная группа центрируется; подсказка управления находится рядом.
Пустая панель помощи удалена. Длинные списки сохраняют прокрутку, короткие
экраны — FONT8. Узкая ширина обычных меню, шрифты и modern HUD сохранены.

Вступления и концовки измеряются по тексту и иллюстрации. Next page и счётчик
показываются только при нескольких страницах. После вычисления пагинации
перестраиваются кнопки и hit rects. Back больше не маскируется подписью Next page.
Многостраничное вступление циклически переключается; одностраничное имеет Start/Back.
У Old Gold удалена приписка «Оригинальное издание» / Original edition.

Карта OG теперь непрозрачная полноэкранная, квадрат карты центрируется и
вписывается без искажения клеток. Native automap не менялась: сохранены цвета,
visited, зеркальная X и точка героя. Кремль сохраняет VGA-коррекцию 1,2.
Это новая адаптация представления по запросу пользователя вместо маленького
прозрачного DOS-оверлея r3. Подсказок нет; мир работает, Tab — по отпусканию.

## Течение: факты и границы

Образец `originals/Old_Gold_3D/OG.EXE`, SHA-256 до/после:
`7965e1d67128f196fbcf2bc449d97197f355c6b81cf043608fc9e01103ea043f`.
Свежий read-only экспорт Ghidra: `export_interaction_audit.py`,
`research/evidence/source-port/sp10-interactions/ghidra/old-gold/`.
Адреса — Ghidra MZ segment:offset, не файловые смещения и не DPMI-селекторы.

- `1ae1:1321` передаёт scheduler интервал 0x1a0 (416); descriptor
  DS `3255:0486` содержит callback `1ae1:1e16`, parameter 0.
- `1ae1:1f51…1f7e`: class12 передаёт signed WORD+68 напрямую, без деления
  на FPS; категория — WORD+66. Far pointers через LES, аргументы словами, RETF.
- `1ae1:1a6c…1af0`: категории 100/101 прибавляют signed значение к X,
  102 — к Z и вызывают исходный resolver. Значение 100 — 100 единиц за callback.
  Размер клетки LEVEL4 — 336. Номинально 24,103 вызова/с, **7,173 клетки/с**.

`check_old_gold_current.py` проверяет байты interval и descriptor, исполняет
surface/getters и исходную цепочку stat/resolver в Unicorn. Движение/resolver
не подменены; свободная геометрия явно синтетическая. 12 native/host векторов
(100/101/102, ±100, 1/24 шага) и 18 секундных повторов при 30/60/144 FPS совпали.
Повторная проверка interactions: 720 callback без расхождений.

Это подтверждает смещение и номинальный интервал, **не полное совпадение
скорости DOS по настенным часам**. DOS dispatch зависит от кадров и назначает
следующий срок относительно фактического времени, порт убирает запаздывание.
На медленном DOS течение могло ощущаться слабее. Существующая защита от
поперечного snap у берега — отдельная адаптация move_actor_current, не изменена.
Скорость течения и анимация воды в runtime не менялись.

## Проверки и воспроизведение

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File ports/sdl2/tools/build.ps1 -Configuration Release
$env:SDL_AUDIODRIVER='dummy'
build/sdl2/windows/dok_menu_font_probe.exe . research/evidence/source-port/sp15-ui/r5-final --readback
py -3.14 -B -X utf8 ports/sdl2/tools/check_menu_ux.py research/evidence/source-port/sp15-ui/r5-final-process
py -3.14 -B -X utf8 ports/sdl2/tools/check_frontend.py
py -3.14 -B -X utf8 ports/sdl2/tools/check_automap.py
py -3.14 -B -X utf8 ports/sdl2/tools/export_interaction_audit.py
py -3.14 -B -X utf8 ports/sdl2/tools/check_old_gold_current.py
py -3.14 -B -X utf8 ports/sdl2/tools/check_interaction_oracle.py
py -3.14 -B -X utf8 ports/sdl2/tools/check_package.py artifacts/sdl2/windows-playtest-20261104-sp15-r5 --hidden
```

Release CTest17/17; 572 CPU-кадра и 336 GL/software readback; 17 menu UX,
шесть frontend, 12 map, 30 current-векторов и восемь packaged запусков PASS.
Окна скрытые, аудио dummy, пользовательские настройки не менялись.
Пакет: `artifacts/sdl2/windows-playtest-20261104-sp15-r5.zip`.
Следующий основной ID — SP17; SP21/SP22 остаются в своей очереди.
