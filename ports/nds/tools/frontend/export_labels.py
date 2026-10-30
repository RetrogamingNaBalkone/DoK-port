"""Encode the NDS-specific menu labels in original-font CP866."""
from pathlib import Path
import json

ROWS = [
    ["Подземелья Кремля", "Главное меню", "Начать игру", "Язык", "Управление", "Предыстория", "Выход", "Пауза", "Продолжить", "Язык", "Управление", "Главное меню", "Выбор уровня", "Русский", "A: Да  B: Назад", "A: Далее  B: Назад", "Выключите Nintendo DS", "Начало уровня", "Уровень завершён", "Конец игры"],
    ["Kremlin Dungeons", "Main menu", "Start game", "Language", "Controls", "Story", "Exit", "Paused", "Continue", "Language", "Controls", "Main menu", "Select level", "English", "A: OK  B: Back", "A: Next  B: Back", "Turn off Nintendo DS", "Level introduction", "Level completed", "The end"],
]
CONTROLS = [
    ["Вверх/вниз: вперёд/назад", "Влево/вправо: стрейф", "Y / A: поворот влево/вправо", "R: стрелять", "L: бег (удерживать)", "X / B: двери, действие", "Select: следующее оружие", "Start: пауза / продолжить", "Карта: перетащите стилусом", "- / +: масштаб карты", "Центр: следить за героем"],
    ["Up/down: move forward/back", "Left/right: strafe", "Y / A: turn left/right", "R: fire", "Hold L: run", "X / B: door / use", "Select: next weapon", "Start: pause / continue", "Map: drag with stylus", "- / +: map zoom", "Center: follow hero"],
]

def generate():
    target = Path(__file__).resolve().parents[2] / 'platform/frontend'
    def literal(value):
        return '"' + ''.join('\\x%02x' % c for c in value.encode('cp866')) + '"'
    lines = ['/* N21 NDS menu labels, CP866; original font is unmodified. */']
    for name, rows in [('labels', ROWS), ('controls', CONTROLS)]:
        lines.append(f'static const unsigned char *const {name}[2][{len(rows[0])}]={{')
        for row in rows:
            lines.append('{' + ','.join('(const unsigned char *)' + literal(s) for s in row) + '},')
        lines.append('};')
    (target/'labels.inc').write_text('\n'.join(lines)+'\n', 'utf8')
    (target/'labels.json').write_text(json.dumps(dict(labels=ROWS, controls=CONTROLS), ensure_ascii=False, indent=2)+'\n', 'utf8')

if __name__ == '__main__':
    generate()
