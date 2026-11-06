"""Native AI/player callbacks composed with executed original SB backend.

The CPU bridge passes real arguments/results to NativeSound; it does not stub
sound algorithms. Device readiness, clocks and scheduling are fixture inputs.
"""
import argparse
import json
import struct
import subprocess

from check_monster_ai_oracle import AIOracle, vectors_for, far, linear, u32
from check_movement_oracle import RES, R
from check_projectile_native import BOUNDARIES
from native_sound import NativeSound, CONFIG, ROOT, OUT, sha

ROLES = {0x4c: 1, 0xf0: 2, 0xf4: 3, 0x100a6: 4, 999: 5}
ACTIONS = {'once': 0, 'loop': 1, 'stop': 2, 'stop_all': 3}


class SoundAI(AIOracle):
    def __init__(self, profile, model):
        self.backend = NativeSound(profile)
        super().__init__(profile, model)

    def setup(self, vector):
        op, actor, hero = super().setup(vector)
        self.backend.command('stop_all')
        self.sound_trace = []
        definition = self.defs[vector[1]]
        fields = {field[1]: field[2] for field in definition['fields']}
        hero_fields = {field[1]: field[2] for field in self.defs[self.hero]['fields']}
        self.role_resources = {0x4c: definition['sound'], 0xf0: fields[0xf0], 0xf4: fields[0xf4],
                               0x100a6: hero_fields[0xa6], 999: definition['sound']}
        self.sound_pointers = {far(0xf0000 + binding * 16): binding for binding in (0x4c, 0xf0, 0xf4)}
        self.sound_pointers[far(0xf2000)] = 0x100a6
        self.w32(hero + 0xa6, far(0xf2000))
        for role, identity in ROLES.items():
            key = self.role_resources[role]
            if not key:
                continue
            raw = self.resources[bytes.fromhex(key).decode('cp866')]
            self.backend.install(identity, raw[36:], struct.unpack_from('<I', raw, 16)[0])
        initial = {0: 0, 1: 0xf0, 2: 999, 3: 0x4c, 4: 0xf4, 5: 0x100a6}[self.busy]
        if initial and self.role_resources[initial]:
            self.backend.command('loop' if initial == 999 else 'once', ROLES[initial])
        return op, actor, hero

    def audio_state(self):
        active = self.backend.active()
        return next((role for role, identity in ROLES.items() if identity == active), 0), self.backend.loop()

    def run(self,vector):
        if vector[0]==6:
            _,actor,_=self.setup(vector)
            self.call({'ru':0x3747,'en':0x3eb0,'old-gold':0x320a}[self.profile],struct.pack('<I',far(actor)),0x5000)
            return []
        return super().run(vector)

    def hook(self, c, address, size, unused):
        if hasattr(self, 'sound_pointers'):
            cfg = CONFIG[self.profile]
            for action in ('playing', 'once', 'loop', 'stop', 'stop_all'):
                if address != cfg['wrapper'] * 16 + cfg[action]:
                    continue
                sp = c.reg_read(R.UC_X86_REG_SP)
                pointer = 0 if action == 'stop_all' else u32(c, 0x30000 + sp + 4)
                assert action == 'stop_all' or pointer in self.sound_pointers, (self.profile, action, hex(pointer))
                role = self.sound_pointers.get(pointer, 0)
                before = self.audio_state()
                result = self.backend.command(action, ROLES.get(role, 0))
                after = self.audio_state()
                self.sound_trace.append(dict(action=action, role=role, resource=self.role_resources.get(role, ''),
                    before=before, after=after, native_address=f'{cfg["wrapper"]:04x}:{cfg[action]:04x}'))
                self.ret(result['result'])
                return
            if address in (0x11e78,0x11adb,0x1fd2b,0x1fd42,0x1fd68,0x1fd7f):self.ret();return
            if address in (BOUNDARIES[self.profile]['cancel'], BOUNDARIES[self.profile]['schedule']):
                self.ret(1 if address == BOUNDARIES[self.profile]['schedule'] else 0)
                return
        super().hook(c, address, size, unused)


def normalize_native(trace):
    return [(ACTIONS[item['action']], item['role'], item['resource'] if item['action'] in ('once','loop') else '', item['before'][0], item['after'][0], int(item['after'][1]))
            for item in trace if item['action'] != 'playing' and item['before'] != item['after']]


def normalize_port(trace):
    result=[]
    for item in trace.split(';'):
        if not item:continue
        parts=item.split(',');action,role=int(parts[0]),int(parts[1]);resource=parts[2]
        before,after,loop=map(int,parts[3:6]);before_loop=int(parts[6])if len(parts)>6 else 0
        if action==4:
            if before==after:continue
            if before:result.append((3,0,'',before,0,0))
            if after:result.append((0,role,resource,0,after,loop))
        elif before!=after or (action in(0,1)and after and loop!=before_loop):
            result.append((action,role,resource,before,after,loop))
    return result


def main():
    parser = argparse.ArgumentParser();parser.add_argument('--profile', choices=['all', *RES], default='all')
    parser.add_argument('--quick', action='store_true');parser.add_argument('--wake-only',action='store_true');parser.add_argument('--record-differences',action='store_true');args = parser.parse_args()
    reports = []
    inventory = json.loads((ROOT / 'analysis/source-port/sp01-inventory.json').read_text())['profiles']
    originals = [p['sample'] for p in inventory] + list(RES.values())
    before = {p: sha(ROOT / p) for p in originals}
    for profile in RES if args.profile == 'all' else [args.profile]:
        models = json.loads(subprocess.check_output([str(ROOT / 'build/sdl2/windows/dok_inspect.exe'), '--json', str(ROOT / RES[profile])], text=True))['levels']
        for model in models[:1] if args.quick else models:
            oracle = SoundAI(profile, model)
            signatures = set(); glyphs = set()
            for obj in model['objects']:
                if obj['class'] != 9:continue
                signature = (obj['sound'], tuple((f[1], f[2]) for f in obj['fields'] if f[1] in (0xf0, 0xf4, 0xe8, 0xec, 0xa6, 0xa8)))
                if signature not in signatures:signatures.add(signature);glyphs.add(obj['glyph'])
            vectors = [v for v in vectors_for(oracle, args.quick) if v[0] == 0 and v[1] in glyphs]
            # Death entry and final stop are separate native callback branches.
            for glyph in sorted(glyphs):
                definition = oracle.defs[glyph]
                key = next(f[2] for f in definition['fields'] if f[1] == 0xec)
                count = struct.unpack_from('<H', oracle.resources[bytes.fromhex(key).decode('cp866')])[0]
                vectors += [[0, glyph, 0, 1, 0, 0, 0, 0, 10000, 0, 0, 3, 0],
                            [0, glyph, 0, 4, count, 0, 0, 0, 10000, 0, 0, 3, 0]]
            if args.wake_only:vectors=[]
            vectors += [[6,glyph,0,0,0,0,0,0,10000,0,0,3,100]for glyph in sorted(glyphs)]
            cases = [(vector, busy) for vector in vectors for busy in range(6)]
            payload = ''.join(' '.join(map(str, [*vector, busy])) + '\n' for vector, busy in cases)
            command = [str(ROOT / 'build/sdl2/windows/dok_sound_event_monster_probe.exe'), str(ROOT / RES[profile]), model['name']]
            result = subprocess.run(command, input=payload, capture_output=True, text=True, timeout=180)
            assert result.returncode == 0, result.stderr
            rows = result.stdout.splitlines()
            assert len(rows) == len(cases), (len(rows), len(cases))
            records = []
            for (vector, busy), row in zip(cases, rows):
                oracle.busy = busy
                try:
                    oracle.run(vector)
                except Exception:
                    print('NATIVE FAIL', profile, model['name'], vector, busy, flush=True);raise
                stats, final, raw_trace = row.split('|')
                final_port = tuple(map(int, final.split(',')))
                final_native = tuple(map(int, oracle.audio_state()))
                native_trace = normalize_native(oracle.sound_trace)
                port_trace = normalize_port(raw_trace)
                equal = final_native == final_port and native_trace == port_trace
                item = dict(vector=vector, busy=busy, native_state=final_native, port_state=final_port,
                            native=oracle.sound_trace, port=raw_trace, normalized_native=native_trace,
                            normalized_port=port_trace, equal=equal)
                records.append(item)
                if not equal:
                    print('DIFF', profile, model['name'], vector, busy, item, flush=True)
                    if not args.record_differences:break
            report = dict(profile=profile, level=model['name'], count=len(records), expected_cases=len(cases),
                          mismatches=sum(not r['equal'] for r in records), cases=records)
            (OUT / f'events-ai-{profile}-{model["name"]}.json').write_text(json.dumps(report, indent=2) + '\n')
            assert len(records) == len(cases) and (args.record_differences or not report['mismatches'])
            reports.append({k:v for k,v in report.items() if k != 'cases'})
            print(reports[-1], flush=True)
    after = {p: sha(ROOT / p) for p in before};assert before == after
    (OUT / 'events-ai-report.json').write_text(json.dumps(dict(reports=reports, sha256_before=before, sha256_after=after), indent=2) + '\n')


if __name__ == '__main__':main()
