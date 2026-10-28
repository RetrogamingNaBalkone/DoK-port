"""Prepare verified world/constructor Ghidra entry lists; check original hashes."""

import sys as _layout_sys
from pathlib import Path as _LayoutPath
_layout_root = _LayoutPath(__file__).resolve().parents[3]
for _layout_dir in ("scripts", "tools/originals", "tools/reports", "ports/gba/scripts"):
    _layout_sys.path.append(str(_layout_root / _layout_dir))

from pathlib import Path
from inventory import EXPECTED, sha

ROOT = Path(__file__).resolve().parents[3]
ROLES = {
    "ru": {
        "1098:004f": "world_initialize", "1098:0ef7": "world_prepare_maps_and_actors",
        "1098:017d": "world_find_reusable_slot", "1098:0205": "world_remove_slot",
        "1098:0278": "world_set_slot", "1068:02f5": "object_initialize_base",
        "1068:09c5": "object_initialize_actor", "1068:1657": "object_initialize_player",
        "1068:36b1": "object_initialize_monster", "1068:22b6": "object_initialize_projectile",
        "1068:30ff": "object_initialize_door", "1068:30a2": "object_initialize_switch",
        "1068:06d6": "object_initialize_effect", "1038:0000": "object_initialize_event_owner",
        "1130:01f5": "object_initialize_list10", "1050:0054": "animation_next_native",
    },
    "en": {
        "1030:0079": "world_initialize", "1030:0fdb": "world_prepare_maps_and_actors",
        "1020:07cf": "object_initialize_base", "1020:0ec9": "object_initialize_actor",
        "1020:1b61": "object_initialize_player", "1020:3e14": "object_initialize_monster",
        "1020:2866": "object_initialize_projectile", "1020:37ae": "object_initialize_door",
        "1020:374b": "object_initialize_switch", "1020:0bbc": "object_initialize_effect",
    },
}


def main():
    for lang, (relative, expected) in EXPECTED.items():
        assert sha((ROOT / "originals/pkr" / relative).read_bytes()) == expected
        directory = ROOT / f"research/evidence/stage6/{lang}"
        directory.mkdir(parents=True, exist_ok=True)
        (directory / "requested-roles.tsv").write_text("address\trole\n" +
            "".join(f"{address}\t{role}\n" for address, role in ROLES[lang].items()), encoding="utf-8")


if __name__ == "__main__":
    main()
