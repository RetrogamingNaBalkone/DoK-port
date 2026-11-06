"""Controlled GPU/audio injections verify frozen two-screen diagnostics."""
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
    parser.add_argument('--fault', choices=('gpu', 'audio', 'plan'), default='gpu')
    args = parser.parse_args()
    target = args.target.resolve()
    directory = target / ('crash-diagnostic-' + args.fault)
    directory.mkdir(exist_ok=True)
    f = front.FrontendObserver(front.CORE, target, directory)
    f.options['melonds_homebrew_sdcard'] = 'disabled'
    report = dict(passed=False, scope='Stationary LEVEL1; explicit RAM fault injection, no routes', fault=args.fault)
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
        if args.fault == 'gpu':
            offset = obs - 0x02000000 + 4 * f.render_fields.index('disp3d_sticky')
            injected = 1
        elif args.fault == 'audio':
            audio_fault, _ = front.transport.elf_symbol(elf, 'fault')
            offset = audio_fault - 0x02000000
            injected = 51
        else:
            palette, _ = front.transport.elf_symbol(elf, 'stream_palette')
            offset = palette - 0x02000000
            injected = 0  # Actual stream_plan must return ARGUMENT, not a fabricated status.
        assert 0 <= offset <= size - 4
        # Explicit fault injection into the private CPU observation copy.
        # Bypass FrontendObserver.tick, which correctly rejects failed frames.
        C.memmove(pointer + offset, struct.pack('<I', injected), 4)
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
        if args.fault == 'gpu':
            assert function == 'render_one' and 'DIAG_CHECK(DIAG_GPU' in source[line - 1]
        elif args.fault == 'plan':
            source = (front.ROOT / 'ports/nds/platform/gameplay_probe/effects_prepare.inc').read_text('utf8').splitlines()
            assert function == 'effects_prepare' and 'DIAG_CHECK(DIAG_PLAN' in source[line - 1]
        diagnostic_at, diagnostic_bytes = front.transport.elf_symbol(elf, 'crash_diagnostic')
        assert diagnostic_bytes == 188
        frozen = front.render.ram(f, diagnostic_at, diagnostic_bytes)
        count, stopped, valid, operation, status, a, b = struct.unpack_from('<7I', frozen)
        assert count >= 8 and stopped == valid == 1
        if args.fault == 'plan':
            assert (operation, status) == (8, 1) and a > 0
        else:
            assert (operation, status, a) == ((13, 1, 1) if args.fault == 'gpu' else (2, 0, 51))
        history = [struct.unpack_from('<5I', frozen, 28 + 20 * ((count - 8 + i) & 7)) for i in range(8)]
        assert history[-1][2] == operation
        assert all(history[i][0] <= history[i + 1][0] for i in range(7))
        for _ in range(60):
            f.core.retro_run()
        assert front.render.ram(f, diagnostic_at, diagnostic_bytes) == frozen, 'Fatal history changed'
        assert target.name.encode() in (target / 'dok_nds_gameplay.nds').read_bytes()
        report.update(passed=True, function=function, line=line, renderer=renderer,
                      diagnostic=dict(count=count, operation=operation, status=status, a=a, b=b, history=history),
                      captures=f.capture('fatal-screen'))
    finally:
        f.close()
        front.transport.flush_native_stdio()
        report['rom_sha256'] = front.transport.sha(target / 'dok_nds_gameplay.nds')
        (directory / 'result.json').write_text(json.dumps(report, indent=2) + '\n', 'utf8')
    print(json.dumps(report))


if __name__ == '__main__':
    main()
