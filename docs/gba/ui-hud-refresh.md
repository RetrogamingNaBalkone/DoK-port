# Компактный HUD GBA — 26 октября 2026 года

**Актуальное дополнение после тестирования LEVEL1:** [level1-fixes.md](level1-fixes.md).
Иконки теперь 12×8/8×8/10×12 и уменьшены offline в ресурсы ROM;
сообщения центрированы в одну строку с адаптивной шириной шрифта.
Готовый ROM обновлён в `artifacts/gba/DoK-port.gba` из `build/gba/level1-fixes/`.
Ниже сохранены размеры и артефакты первого варианта HUD до этого уточнения.

Дополнение G17 по запросу пользователя. HP и заряд заменены числами с
оригинальными иконками. W и L удалены с игрового экрана. Ключи показываются
только при наличии, без букв и hex-маски. Полноценная заставка/главное меню
поставлены в [бэклог UI01](gba-backlog.md); текущая перекраска их не закрывает.

## Ресурсы и степень доказанности

Используются неизменённые payload из PKR.RES/PKE.RES, уже упакованные в общий
ROM. Источник — каталоги `analysis/resources-{ru,en}.json` и проверенный
18-байтовый формат `dok_image_open`. Адреса ниже не используются: это имена
ресурсов и координаты пикселей, а не NE/Ghidra/DPMI-адреса. Новый анализ EXE
не выполнялся; прежние Ghidra-доказательства G17 остаются в
[ui-localization.md](ui-localization.md).

| Ресурс | Размер | Использование |
|---|---|---|
| ПАНЕЛЬ | 320×39 | Исходная рамка толщиной 3 пикселя; внутренний цвет из (10,5), индекс 86 |
| ПЖИЗНЬ | 25×17 | Сердце → 15×10 |
| ПМАГИЯ | 17×18 | Заряд → 10×11 |
| ПКЛЮЧМ / ПКЛЮЧЖ / ПКЛЮЧЗ | 26×30 | Медный / железный / золотой ключ → 12×14 |

Масштабирование — nearest neighbour, исходный индекс 0 у иконок прозрачен.
GAMEP1/2/3 сохраняют сине-серую шкалу панели и зелёный индекс 2:
VGA RGB6 `(0,63,0)` у GAMEP1/2, `(0,62,0)` у GAMEP3. Выбор индекса 2 и
новая геометрия — решения интерфейса GBA, а не утверждение об эквивалентности
всех DOS-экранов. Иконки и их цвета взяты из самих ресурсов.
Обычные ключи соответствуют битам 1/2/4. Для неиспользуемого исходными
предметами бита 8 отдельной исходной иконки нет; новая не выдумывалась.

## Геометрия и реализация

Сцена 240×144, сообщения 240×16 сохранены. Верхние панели имеют высоту 14,
ширина зависит от числа символов: HP `23+8N`, заряд `18+8N`.
При HP=100/заряде=150 это 47/42 пикселя вместо 80/80.
Ключи занимают по 16×16 справа снизу сцены, y=128…143; пустые слоты не рисуются.
Шрифты FONT8/FONT16 неизменны. Меню, заставки текста и сообщения используют
ту же рамку и зелёный шрифт.

`dok_ui_art` хранит заимствованные views, `dok_ui_set_art` выбирает локализацию.
На загрузке уровня проверяются имена, формат и размеры всех шести изображений.
Полный RES, декодированные копии и новые палитры в RAM не добавляются.
Внутренняя заливка использует прежний word fill; отдельно рисуются лишь края.

## Артефакты и проверки

ROM/ELF/map: `build/gba/g17-ui/`; прежние G18/G20 не перезаписаны.
ROM 32 890 948 байт, SHA-256
`e9895ed2037fc970529111ff7a22897b94451a5543bcba7193312e3631bda573`.
EWRAM 253668, свободно 8476; IWRAM 24308, свободно 268 до отдельного
8 KiB резерва стеков. Наблюдённый main stack 2368, margin 5568.

- Windows host UI tests прошли; Linux static compiled, не запускался.
- ARM UI: кнопки/пауза/локализация/очередь/крайние signed16 значения/границы;
  исходные пиксели иконок и 8 сочетаний ключей × RU/EN проверены независимо
  по RES. Полностью пустые участки нижней сцены сохраняются при отсутствии ключей.
- Flow: все шесть миров/локализаций, 14 текстовых ресурсов, retry/gameover/Continue.
- Renderer: RU/EN, guards/reset; clock: rational steps/callbacks/reset.
- Фактические экраны `build/gba/g17-ui/ui-check/` просмотрены: игровой HUD,
  три ключа и главное меню. Отчёты `analysis/g17-ui-*.json`.

Сборка и выполненные ARM-проверки:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File ports/gba/scripts/build-gba.ps1 -RendererProbe -HalfColumns -OutDir build/gba/g17-ui
$env:DOK_PROBE_FAST_VIDEO='1'
C:/Source/wasdify/.venv/Scripts/python.exe ports/gba/scripts/verify_ui_gba.py --build-dir build/gba/g17-ui --report analysis/gba/g17-ui-refresh.json
C:/Source/wasdify/.venv/Scripts/python.exe ports/gba/scripts/verify_flow_gba.py --build-dir build/gba/g17-ui --report analysis/gba/g17-ui-flow-regression.json
C:/Source/wasdify/.venv/Scripts/python.exe ports/gba/scripts/verify_renderer_gba.py --build-dir build/gba/g17-ui --report analysis/gba/g17-ui-renderer-regression.json
C:/Source/wasdify/.venv/Scripts/python.exe ports/gba/scripts/verify_time_gba.py --build-dir build/gba/g17-ui --report analysis/gba/g17-ui-time-regression.json
```

Физическая GBA, полная DOS-сессия и новый полный performance/audio/AI проход
не проверялись. Бюджет 30 FPS этой задачей не закрывается; следующий основной
шаг по плану — G20A.
