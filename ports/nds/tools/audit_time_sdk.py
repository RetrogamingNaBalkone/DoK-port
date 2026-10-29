"""Preserve installed SDK counter objects and primary declarations for N08."""
from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[3]
EVIDENCE = ROOT / "research/evidence/nds/n08"
WONDERFUL = Path(os.environ.get("USERPROFILE", "C:/Users/dspev")) / ".local/msys64/opt/wonderful"
SDK = WONDERFUL / "thirdparty/blocksds/core"
BIN = WONDERFUL / "toolchain/gcc-arm-none-eabi/bin"
HEADERS = (
    "libs/libnds/include/nds/system_counter.h",
    "libs/libnds/include/nds/timers.h",
    "libs/libnds/include/nds/interrupts.h",
    "libs/libnds/include/nds/ndstypes.h",
)
MEMBERS = ("system_counter.c.o", "timers.c.o")


def artifact(path: Path) -> dict:
    data = path.read_bytes()
    name = path.relative_to(ROOT).as_posix() if path.is_relative_to(ROOT) else path.as_posix()
    return dict(path=name, bytes=len(data), sha256=hashlib.sha256(data).hexdigest())


def guard(path: Path) -> Path:
    if not path.is_relative_to(EVIDENCE) or ".." in path.parts:
        raise ValueError("SDK evidence output escapes N08")
    for item in (path, *path.parents):
        if item == ROOT:
            break
        if item.is_symlink() or item.is_junction():
            raise ValueError("SDK evidence output crosses an alias")
    if path.exists() and (not path.is_file() or path.stat().st_nlink != 1):
        raise ValueError("SDK evidence output is not an unaliased regular file")
    return path


def main() -> None:
    outputs = [guard(EVIDENCE / "sdk-counter.json")]
    for member in MEMBERS:
        outputs.extend((guard(EVIDENCE / "sdk" / member),
                        guard(EVIDENCE / "sdk" / (member + ".disassembly.txt"))))
    for path in outputs:
        path.parent.mkdir(parents=True, exist_ok=True)
    archive = SDK / "libs/libnds/lib/libnds9.a"
    sources = [artifact(SDK / name) for name in HEADERS] + [artifact(archive)]
    env = dict(os.environ, PATH=str(WONDERFUL / "bin") + os.pathsep + os.environ.get("PATH", ""))
    report = dict(task="N08", passed=False, scope="Installed primary SDK counter declarations and ELF object disassembly; not original EXE analysis or physical timing proof", sources=sources, objects=[], commands=[])
    outputs[0].write_text(json.dumps(report, indent=2) + "\n", encoding="utf8")
    for index, member in enumerate(MEMBERS):
        target = outputs[1 + index * 2]
        # The Windows ar stdout path translates newlines. Extract the single
        # named member directly so ELF bytes never pass through a text stream.
        command = [str(BIN / "arm-none-eabi-ar.exe"), "x", str(archive), member]
        subprocess.run(command, cwd=target.parent, env=env, check=True, capture_output=True)
        data = target.read_bytes()
        if not data.startswith(b"\x7fELF"):
            raise ValueError("Archive member is not ELF")
        report["commands"].append(command)
        command = [str(BIN / "arm-none-eabi-objdump.exe"), "-dr", str(target)]
        dump = subprocess.run(command, env=env, check=True, capture_output=True).stdout
        outputs[2 + index * 2].write_bytes(dump)
        report["commands"].append(command)
        report["objects"].append(dict(member=member, object=artifact(target), disassembly=artifact(outputs[2 + index * 2])))
    counter = outputs[2].read_text(encoding="utf8")
    declarations = (SDK / HEADERS[0]).read_text(encoding="utf8")
    report["checks"] = dict(
        declaration_uint64="uint64_t systemCounterGetTicks(void);" in declarations,
        counter_get_symbol="<systemCounterGetTicks>:" in counter,
        counter_setup_symbol="<systemCounterSetup>:" in counter,
        irq_counter_uses_timer_start="timerStart" in counter,
        timer2_selected="movs\tr0, #2" in counter,
        divider64_selected="movs\tr1, #1" in counter,
        cpsr_restored="msr\tCPSR_fc, lr" in counter,
        pending_and_carry_64bit="umlal\tr0, r1, r3, ip" in counter,
    )
    report["application_contract"] = "Application calls systemCounterSetup once before creating services; subsequent initialization must not reset the shared counter. Timer2 /64, one IRQ per 65536 counts. No IRQ masking for a full 125ms rollover period."
    for record in sources:
        if artifact(Path(record["path"])) != record:
            raise ValueError("SDK input changed during audit")
    report["passed"] = all(report["checks"].values())
    outputs[0].write_text(json.dumps(report, indent=2) + "\n", encoding="utf8")
    print(f"N08 SDK counter audit passed={report['passed']}; {outputs[0]}")
    if not report["passed"]:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
