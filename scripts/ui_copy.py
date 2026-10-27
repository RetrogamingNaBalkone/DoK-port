"""Generate reviewed GBA menu labels as CP866 C literals (not game messages)."""
from pathlib import Path

COPY = {
 "ui_title": ["ПОДЗЕМЕЛЬЯ КРЕМЛЯ", "KREMLIN DUNGEONS"],
 "ui_start": ["НАЧАТЬ ИГРУ", "START GAME"],
 "ui_choose": ["ВЫБОР УРОВНЯ", "SELECT LEVEL"],
 "ui_level_hint": ["B: НАЗАД  SELECT: ЯЗЫК", "B: BACK  SELECT: LANG"],
 "ui_levels": [["ХОРОМЫ НЕЧИСТИ", "ПОДЗЕМНАЯ РЕКА", "ПОДВАЛЫ КРЕМЛЯ"],
               ["PALACE OF EVIL", "SACRED RIVER", "UNDERGROUND KREMLIN"]],
 "ui_pause": ["ПАУЗА", "PAUSED"],
 "ui_back": ["B / SELECT: НАЗАД", "B / SELECT: BACK"],
 "ui_hint": ["A/START: OK  SELECT: ЯЗЫК", "A/START: OK  SELECT: LANG"],
 "ui_keyhint": ["K:1М 2Ж 4З 8Секрет", "K:1Cu 2Fe 4Au 8Secret"],
 "ui_menu": [["ПРОДОЛЖИТЬ", "ЯЗЫК", "УПРАВЛЕНИЕ", "ГЛАВНОЕ МЕНЮ"],
             ["CONTINUE", "LANGUAGE", "CONTROLS", "TITLE MENU"]],
 "ui_help": [["УПРАВЛЕНИЕ", "D-PAD: ИДТИ / ПОВОРОТ", "A: ОГОНЬ  B: БЕГ", "L / R: ШАГ ВБОК",
              "L+R: СМЕНИТЬ ОРУЖИЕ", "START: ДЕЙСТВИЕ", "SELECT: КАРТА", "START+SELECT: ПАУЗА", "B / SELECT: НАЗАД"],
             ["CONTROLS", "D-PAD: MOVE / TURN", "A: FIRE   B: RUN", "L / R: STRAFE",
              "L+R: CYCLE WEAPON", "START: USE", "SELECT: MAP", "START+SELECT: PAUSE", "B / SELECT: BACK"]],
 "ui_status": [["ПОКАЗАТЕЛИ", "МАКС. ЖИЗНЬ", "МАКС. ЗАРЯД", "ОЧКИ", "КАРТА"],
               ["STATISTICS", "MAX HEALTH", "MAX ENERGY", "SCORE", "MAP"]],
}

def cstr(s):
    return '"' + ''.join(chr(b) if 32 <= b < 127 and b not in (34, 92) else f'\\{b:03o}' for b in s.encode('cp866')) + '"'

def main():
    out = ["/* GBA menu copy, CP866. Original notification copy stays in ROM textpool. */"]
    for name, values in COPY.items():
        if isinstance(values[0], list):
            assert len(values[0]) == len(values[1])
            body = ','.join('{' + ','.join(map(cstr, row)) + '}' for row in values)
            out.append(f'static const char *const {name}[2][{len(values[0])}]={{{body}}};')
        else:
            out.append(f'static const char *const {name}[2]={{{",".join(map(cstr, values))}}};')
    Path('src/ui_text.inc').write_text('\n'.join(out) + '\n', encoding='ascii')

if __name__ == '__main__':
    main()
