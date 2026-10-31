"""Controlled GPU-sticky injection verifies the fatal screen, not a gameplay fix."""
import argparse
import ctypes as C
import json
from pathlib import Path
import struct
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import run_frontend_emulator as front


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('target', type=Path)
    target = parser.parse_args().target.resolve()
    directory = target / 'crash-diagnostic'
    directory.mkdir(exist_ok=True)
    f = front.FrontendObserver(front.CORE, target, directory)
    f.options['melonds_homebrew_sdcard'] = 'disabled'
    report = dict(passed=False, scope='Stationary LEVEL1; synthetic GPU sticky flag, no routes')
    elf = target / 'dok_nds_gameplay.elf'
    try:
        f.load(target / 'dok_nds_gameplay.nds')
        f.wait_for(1)
        f.tap(front.A)
        f.wait_for(2)
        f.tap(front.A)
        f.wait_for(3)
        f.tap(front.A)
        f.text_to(7, 8)
        f.wait_for(8)
        f.tick(30)
        obs, _ = front.transport.elf_symbol(elf, 'obs')
        pointer = f.core.retro_get_memory_data(2)
        size = f.core.retro_get_memory_size(2)
        offset = obs - 0x02000000 + 4 * f.render_fields.index('disp3d_sticky')
        assert 0 <= offset <= size - 4
        # Explicit fault injection into the private CPU observation copy.
        # Bypass FrontendObserver.tick, which correctly rejects failed frames.
        C.memmove(pointer + offset, struct.pack('<I', 1), 4)
        for _ in range(60):
            f.core.retro_run()
        assert not f.callback_error and not f.shutdown
        renderer = front.render.observe(f, f.render_address, f.render_fields, 0x37524b44)
        assert renderer['error_bits'] == 65536 and renderer['status'] == 3
        line_at, _ = front.transport.elf_symbol(elf, 'failure_line')
        function_at, _ = front.transport.elf_symbol(elf, 'failure_function')
        line = struct.unpack('<I', front.render.ram(f, line_at, 4))[0]
        address = struct.unpack('<I', front.render.ram(f, function_at, 4))[0]
        function = front.render.ram(f, address, 32).split(b'\0')[0].decode('ascii')
        source = (front.ROOT / 'ports/nds/platform/gameplay_probe/main.c').read_text('utf8').splitlines()
        assert function == 'render_one' and 'if(obs.disp3d_sticky || obs.gxstat_sticky)' in source[line - 1]
        report.update(passed=True, function=function, line=line, renderer=renderer,
                      captures=f.capture('fatal-screen'))
    finally:
        f.close()
        front.transport.flush_native_stdio()
        report['rom_sha256'] = front.transport.sha(target / 'dok_nds_gameplay.nds')
        (directory / 'result.json').write_text(json.dumps(report, indent=2) + '\n', 'utf8')
    print(json.dumps(report))


if __name__ == '__main__':
    main()
