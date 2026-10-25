"""Validate G06 targeted Ghidra exports and summarize unmodified DOS observations."""
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    report = dict(task='G06', address_system='Ghidra NE loader addresses; not file offsets or runtime selectors', samples={})
    for lang in ('ru', 'en'):
        folder = ROOT/'build/time'/lang
        manifest = json.loads((folder/'analysis-manifest.json').read_text(encoding='utf-8-sig'))
        assert sha(ROOT/manifest['sample']) == manifest['sha256_before'] == manifest['sha256_after']
        assert sha(ROOT/manifest['script']) == manifest['script_sha256']
        instructions = dict(line.split('\t', 1) for line in (folder/'evidence.asm').read_text().splitlines())
        needles = {
            '1130:0014':'INC dword ptr [0xe70]',
            '1130:0199':'MOV EBX,0x77',
            '1130:018d':'NEG CX',
            '1130:01a7':'MOVZX EAX,CX',
            '1130:01c3':'CMP dword ptr [0xe70],0x2710',
            '1038:011f':'JLE 0x1038:020e',
            '1038:032b':'JC 0x1038:0361',
            '1038:0321':'ADD BX,word ptr [BP + -0xa]',
            '1038:0352':'ADD BX,word ptr [BP + -0xa]',
            '1038:0279':'MOV dword ptr ES:[BX + 0x4],EAX',
            '1000:5cb9':'LDS SI,[BP + 0x6]',
            '1000:5cbc':'LES DI,[BP + 0xa]',
            '1000:5ccc':'RETF 0x8',
            '1068:0482':'MOVSX EAX,word ptr ES:[BX + 0x52]',
        } if lang == 'ru' else {
            '1078:169e':'INC dword ptr [0xf20]',
            '1078:182f':'MOV EBX,0x77',
            '1010:16e1':'JC 0x1010:1717',
            '1010:16d7':'ADD BX,word ptr [BP + -0xa]',
            '1010:1708':'ADD BX,word ptr [BP + -0xa]',
            '1010:1738':'JG 0x1010:169b',
            '1000:5ccc':'RETF 0x8',
            '1020:0962':'MOVSX EAX,word ptr ES:[BX + 0x52]',
        }
        for address, expected in needles.items():
            assert instructions.get(address) == expected, (lang, address, instructions.get(address), expected)
        dos = json.loads((ROOT/'build/time'/f'dos-{lang}'/'report.json').read_text())
        assert dos['status'] == 'complete' and dos['originals_unchanged'] and dos['runtime_copies_unchanged']
        assert dos['exe_before'] == dos['exe_after'] == manifest['sha256_before']
        samples = dos['time_probe']
        assert len(samples) == 21 and all(s['pit_period'] == 65536 for s in samples)
        seconds = samples[-1]['wall_seconds']
        ticks = samples[-1]['scheduler_elapsed']-samples[0]['scheduler_elapsed']
        irq = samples[-1]['irq_count']-samples[0]['irq_count']
        assert 7000 < ticks/seconds < 13000 and 12 < irq/seconds < 25
        report['samples'][lang] = dict(ghidra=manifest, asm_sha256=sha(folder/'evidence.asm'),
            instruction_checks=needles, dos_report=f'build/time/dos-{lang}/report.json',
            dos_report_sha256=sha(ROOT/'build/time'/f'dos-{lang}'/'report.json'),
            runtime_linear_data_base=dos['runtime_linear_data_base'], samples=samples,
            ticks_delta=ticks, irq_delta=irq, wall_seconds=seconds,
            observed_ticks_per_wall_second=ticks/seconds, observed_irq_per_wall_second=irq/seconds,
            limitations='HTTP requests are asynchronous, not atomic; wall time is not a native execution oracle; no forced rollover or callback mutation in DOS')
    (ROOT/'analysis/g06-time-evidence.json').write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
    print('G06: both Ghidra/ASM evidence sets and DOS source hashes/time observations passed')


if __name__ == '__main__':
    main()
