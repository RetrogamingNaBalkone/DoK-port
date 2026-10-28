"""Generate complete enemy/weapon parameter tables from verified original RES."""

import sys as _layout_sys
from pathlib import Path as _LayoutPath
_layout_root = _LayoutPath(__file__).resolve().parents[2]
for _layout_dir in ("scripts", "tools/originals", "tools/reports", "ports/gba/scripts"):
    _layout_sys.path.append(str(_layout_root / _layout_dir))

import collections
import json
import subprocess
from pathlib import Path

from analyze_levels import reference
from analyze_resources import parse, sha

ROOT = Path(__file__).resolve().parents[2]
NAMES = {"БОЯР_И": "Боярин", "БАБА_А": "Баба-яга", "МЫШЬ_Л": "Мышь", "ТОПОР_И": "Палач"}


def decode(value):
    return bytes.fromhex(value).decode("cp866")


def metadata(obj):
    out = {k: v for k, v in obj.items() if k not in ("glyph", "fields")}
    for k in ("sprite", "animation", "sound"):
        out[k] = decode(out[k])
    out["fields"] = {f"0x{off:02X}": decode(v) if kind in (3, 4, 5) else v
                     for kind, off, v in obj["fields"] if kind != 6}
    return out


def glyphs(values):
    values = sorted(set(values))
    spans = []
    for v in values:
        if spans and v == spans[-1][-1] + 1:
            spans[-1].append(v)
        else:
            spans.append([v])
    return ", ".join(f"{s[0]:02X}–{s[-1]:02X}" if len(s) > 2 else ", ".join(f"{v:02X}" for v in s) for s in spans)


def table(headers, rows):
    return "\n".join(["| " + " | ".join(headers) + " |", "|" + "---|" * len(headers)] +
                     ["| " + " | ".join(map(str, row)) + " |" for row in rows]) + "\n"


def main():
    models, sources, catalogs = {}, {}, {}
    for lang in ("ru", "en"):
        inv = json.loads((ROOT / f"analysis/originals/inventory-{lang}.json").read_text(encoding="utf-8"))
        path = ROOT / inv["resource"]["file"]
        archive = path.read_bytes()
        assert sha(archive) == inv["resource"]["sha256"]
        sources[lang] = dict(file=inv["resource"]["file"], sha256=sha(archive), levels=[])
        entries = parse(archive)
        catalogs[lang] = {e["name"]: e for e in entries}
        for entry in entries[18:21]:
            payload = archive[entry["offset"]:entry["offset"] + entry["size"]]
            model = reference(payload)
            extracted = ROOT / f"research/evidence/resources/{lang}/{entry['index']:04d}.bin"
            assert extracted.read_bytes() == payload
            actual = json.loads(subprocess.check_output([str(ROOT / "build/gba/host/dok-level.exe"), str(extracted)]))
            assert actual == model
            models[lang, entry["index"] - 17] = model
            sources[lang]["levels"].append({k: entry[k] for k in ("name", "index", "offset", "size", "sha256")})
        assert sha(path.read_bytes()) == sha(archive)

    groups, individual, projectiles, weapons = [], [], [], []
    for level in (1, 2, 3):
        pairs = collections.OrderedDict()
        a, b = models["ru", level], models["en", level]
        assert len(a["objects"]) == len(b["objects"])
        for x, y in zip(a["objects"], b["objects"]):
            assert (x["glyph"], x["class"]) == (y["glyph"], y["class"])
            if x["class"] not in (8, 9, 10):
                continue
            item = dict(level=level, glyph=x["glyph"], ru=metadata(x), en=metadata(y))
            if x["class"] == 9:
                for lang in ("ru", "en"):
                    mmap = bytes.fromhex(models[lang, level]["maps"][1])
                    item[lang]["positions"] = [[i % 64, i // 64] for i, v in enumerate(mmap) if v == x["glyph"]]
                individual.append(item)
                key = json.dumps([metadata(x), metadata(y)], sort_keys=True)
                if key not in pairs:
                    pairs[key] = dict(level=level, glyphs=[], ru=metadata(x), en=metadata(y), count_ru=0, count_en=0)
                g = pairs[key]
                g["glyphs"].append(x["glyph"])
                for lang in ("ru", "en"):
                    g[f"count_{lang}"] += len(item[lang]["positions"])
            elif x["class"] == 8:
                assert item["ru"] == item["en"], (level, x["glyph"])
                projectiles.append(item)
            else:
                weapons.append(item)
        groups.extend(pairs.values())

    animation_intervals = {}
    for lang in ("ru", "en"):
        animations = json.loads((ROOT / f"analysis/originals/animations-{lang}.json").read_text(encoding="utf-8"))
        assert animations["archive_sha256"] == sources[lang]["sha256"]
        for name in ("fРУЖЬЕ", "fЛАЗЕР", "PULKA1", "PULKA2"):
            entry = next(e for e in animations["entries"] if e["name"] == name)
            assert entry["sha256"] == catalogs[lang][name]["sha256"]
            assert catalogs["ru"][name]["sha256"] == catalogs["en"][name]["sha256"]
            values = [f["interval_ticks"] for f in entry["frames"]]
            if lang == "ru":
                animation_intervals[name] = values
            else:
                assert values == animation_intervals[name]
    report = dict(sources=sources, grouping="all non-text definition fields; glyph/positions excluded from group key",
                  weapon_animation_intervals=animation_intervals,
                  enemy_groups=groups, enemy_definitions=individual, projectiles=projectiles, weapons=weapons)
    (ROOT / "analysis/originals/balance-tables.json").write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    rows, ai_rows, common_rows, resource_rows = [], [], [], []
    for n, g in enumerate(groups, 1):
        def pair(key, field=False):
            vals = [g[l]["fields"][key] if field else g[l][key] for l in ("ru", "en")]
            return str(vals[0]) if vals[0] == vals[1] else f"**{vals[0]} → {vals[1]}**"
        label = f"E{n:02d}"
        rows.append([label, g["level"], NAMES[g["ru"]["animation"]], glyphs(g["glyphs"]), len(g["glyphs"]),
                     g["count_ru"] if g["count_ru"] == g["count_en"] else f"{g['count_ru']} → {g['count_en']}",
                     pair("0xA4", True), pair("0x38", True), pair("0xBA", True), pair("width")])
        ai_rows.append([label, pair("0x32", True), pair("0x34", True), pair("0xE7", True),
                        pair("0xA6", True), pair("0xA8", True), pair("0xAE", True)])
        common_rows.append([label, pair("flags"), pair("height"), pair("marker"), pair("sprite_flags")])
        o = g["ru"]
        assert all(o[k] == g["en"][k] for k in ("sprite", "animation", "sound"))
        assert all(o["fields"][k] == g["en"]["fields"][k] for k in ("0xE8", "0xEC", "0xF0", "0xF4"))
        resource_rows.append([label, o["sprite"], o["animation"], o["sound"], o["fields"]["0xE8"],
                              o["fields"]["0xEC"], o["fields"]["0xF0"], o["fields"]["0xF4"]])
    doc = """# Сводные параметры врагов: RUS и ENG

Таблицы получены из всех шести LEVEL исходных PKR.RES/PKE.RES.
Одинаковое значение показано один раз; **RUS → ENG** выделяет различие версий.
Каждая строка объединяет только определения с полностью одинаковыми
нетекстовыми параметрами в каждой версии. Переведённые реплики не влияют
на группировку. Включены и определения, не размещённые на карте.
«Опред.» — число объявлений; «На карте» — число размещений в MMap каждой версии.
Glyph указан шестнадцатеричными байтами CP866, а не номерами врагов.
ID E01 и далее — ссылки между таблицами этого отчёта.

Назначения полей и доказательства: [enemy-parameters.md](enemy-parameters.md).
Период курса — исходное N; после сброса курс пересчитывается через N+1
проходов ветви движения. Снаряд указан десятичным значением байта:
51 = `3`, 52 = `4`. Нулевая серия блокирует ветвь стрельбы снарядами.

## Здоровье, скорость и активация

"""
    doc += table(["ID", "Ур.", "Вид", "Glyph (hex)", "Опред.", "На карте", "Здоровье", "Скорость", "Пробуждение", "Масштаб"], rows)
    doc += "\n## Направление и атаки\n\n"
    doc += table(["ID", "Угол", "Шаг поворота", "Снаряд (байт)", "Серия", "Задержка серии", "Период курса N"], ai_rows)
    doc += "\n## Общие поля\n\nFlags и marker приведены десятичными числами; height — исходный параметр LEVEL.\n\n"
    doc += table(["ID", "Flags", "Height", "Marker", "Sprite flags"], common_rows)
    doc += "\n## Ресурсы\n\nРесурсные ссылки соответствующих определений RUS/ENG совпадают.\n\n"
    doc += table(["ID", "Sprite", "Flic", "Sound", "Атака: Flic", "Смерть: Flic", "Атака: Sound", "Смерть: Sound"], resource_rows)
    doc += """
## Проверка и воспроизведение

```powershell
C:/Source/wasdify/.venv/Scripts/python.exe tools/originals/summarize_balance.py
```

Скрипт проверяет SHA-256 исходных RES до и после чтения и полное совпадение
моделей Python/C для всех шести LEVEL. Полные нетекстовые определения,
координаты каждого экземпляра, группировка и контрольные суммы:
[balance-tables.json](../analysis/originals/balance-tables.json).
Координаты от нуля, x — столбец, y — строка, без native-рамки.
Отдельное сравнение оружия: [weapon-parameters.md](weapon-parameters.md).
"""
    (ROOT / "docs/originals/enemy-tables.md").write_text(doc, encoding="utf-8")
    # Short generated tables for the weapon research document.
    prows = []
    for p in projectiles:
        o = p["ru"]; f = o["fields"]
        prows.append([p["level"], chr(p["glyph"]), o["animation"], f["0x3C"], f["0x5C"],
                      f["0x5A"], o["width"], o["height"], o["flags"], o["marker"], f["0x77"], f["0x7B"]])
    wrows = []
    for w in weapons:
        a, b = w["ru"], w["en"]
        assert {k:v for k,v in a.items() if k != "width"} == {k:v for k,v in b.items() if k != "width"}
        f = a["fields"]
        wrows.append([w["level"], chr(w["glyph"]), f["0x81"], a["animation"], a["width"], b["width"],
                      f["0x54"], f["0x56"], a["flags"], a["height"], a["marker"], a["sprite_flags"]])
    generated = "## Снаряды из LEVEL\n\nВсе значения в этой таблице совпадают между RUS и ENG. Скорость — положительное значение в файле; reader меняет его знак.\n\n"
    generated += table(["Ур.", "Glyph", "Flic", "Скорость", "Нач. урон", "+0x5A", "Масштаб", "Height", "Flags", "Marker", "Попадание", "Промах"], prows)
    generated += "\n## Предметы оружия из LEVEL\n\nЧисловые отличия RUS/ENG ограничены масштабом; перевод сообщений исключён из сравнения.\n\n"
    generated += table(["Ур.", "Glyph", "Слот", "Flic", "Масштаб RU", "Масштаб EN", "Стат", "Величина", "Flags", "Height", "Marker", "Sprite flags"], wrows)
    (ROOT / "research/evidence/weapon-fields").mkdir(parents=True, exist_ok=True)
    (ROOT / "research/evidence/weapon-fields/tables.md").write_text(generated, encoding="utf-8")
    weapon_doc = ROOT / "docs/originals/weapon-parameters.md"
    text = weapon_doc.read_text(encoding="utf-8")
    start, end = "<!-- BALANCE_TABLES_START -->", "<!-- BALANCE_TABLES_END -->"
    assert text.count(start) == text.count(end) == 1
    text = text.split(start)[0] + start + "\n\n" + generated + "\n" + end + text.split(end)[1]
    weapon_doc.write_text(text, encoding="utf-8")
    print(f"Verified 6 LEVEL; {len(individual)} enemy definitions paired into {len(groups)} groups; {len(projectiles)} projectile and {len(weapons)} weapon pairs.")


if __name__ == "__main__":
    main()
