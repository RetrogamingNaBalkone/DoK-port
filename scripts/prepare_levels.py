"""Resolve class reader entries from NE vtable relocations (not raw pointers)."""
import json
from pathlib import Path
from inventory import EXPECTED, parse_ne, sha

ROOT = Path(__file__).resolve().parents[1]
VTABLES = {
    "ru": [0x775, 0x66d, 0x75d, 0x741, 0x729, 0x685, 0x63d,
           0x625, 0x655, 0x60d, 0x70d, None, 0x6c9, 0x69d],
    "en": [0x7e6, 0x696, 0x7c6, 0x7a2, 0x782, 0x6b6, 0x656,
           0x636, 0x676, 0x616, 0x75e, None, 0x70a, 0x6d6],
}


def main():
    report = {}
    for lang, (relative, expected) in EXPECTED.items():
        data = (ROOT / "originals/pkr" / relative).read_bytes()
        assert sha(data) == expected
        segments = parse_ne(data)["segments"]
        rows = []
        roles = {("1048:004c" if lang == "ru" else "1018:0152"): "level_read_resource",
                 ("1068:0000" if lang == "ru" else "1020:018c"): "level_create_object"}
        if lang == "ru":
            roles.update({"1048:092e": "level_read_line", "1048:0a2c": "level_assignment_value",
                          "1048:0a8f": "level_assignment_integer", "1048:0ae2": "level_assignment_string",
                          "1000:20d0": "ascii_case_prefix_compare", "1158:26d4": "stream_read_character"})
        for type_id, vtable in enumerate(VTABLES[lang]):
            if vtable is None:
                continue
            matches = [r for r in segments[-1]["relocations"] if vtable + 4 in r["chain"]]
            assert len(matches) == 1 and matches[0]["kind"] == 3
            relocation = matches[0]
            # Ghidra NE loader selectors: segment 1 = 1000h, stride 8.
            selector = 0x1000 + 8 * (relocation["target"] - 1)
            address = f"{selector:04x}:{relocation['target_offset']:04x}"
            rows.append(dict(type=type_id, vtable_data_offset=vtable, reader=address,
                             target_ne_segment=relocation["target"]))
            roles.setdefault(address, f"level_read_class_{type_id}")
        directory = ROOT / "research/evidence/stage5" / lang
        directory.mkdir(parents=True, exist_ok=True)
        (directory / "requested-roles.tsv").write_text(
            "address\trole\n" + "".join(f"{a}\t{r}\n" for a, r in roles.items()), encoding="utf-8")
        report[lang] = dict(exe_sha256=expected, classes=rows)
    (ROOT / "analysis/level-native-readers.json").write_text(
        json.dumps(report, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
