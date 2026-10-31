"""Extract actual ARM9 AI offsets from a pinned candidate ELF's DWARF."""
import argparse,json,os,re,subprocess,sys
from pathlib import Path
from check_host import ROOT,pin
sys.path.insert(0,str(ROOT/'ports/nds/tools'))
from run_diag_emulator import elf_symbol

def parse(text):
    nodes={};stack=[];current=None
    for line in text.splitlines():
        match=re.match(r'\s*<(\d+)><([0-9a-f]+)>:.*(?:\((DW_TAG_\w+)\))?',line)
        if match:
            depth,offset=int(match[1]),int(match[2],16)
            tag=re.search(r'\((DW_TAG_\w+)\)',line)
            if not tag:current=None;continue
            node=dict(offset=offset,tag=tag[1],attrs={},children=[]);nodes[offset]=node
            while len(stack)>depth:stack.pop()
            if stack:stack[-1]['children'].append(offset)
            stack.append(node);current=node;continue
        attr=re.search(r'DW_AT_(\w+)\s*:\s*(.*)',line)
        if attr and current:
            value=attr[2].strip()
            if attr[1]=='name':value=value.rsplit(': ',1)[-1]
            current['attrs'][attr[1]]=value
    return nodes

def integer(value):return int(value.split()[0],0)
def ref(value):return int(re.search(r'<0x([0-9a-f]+)>',value)[1],16)
def concrete(nodes,node):
    while node['tag'] in ('DW_TAG_typedef','DW_TAG_const_type','DW_TAG_volatile_type'):
        node=nodes[ref(node['attrs']['type'])]
    return node
def member(nodes,node,name):
    node=concrete(nodes,node)
    child=next(nodes[i]for i in node['children']if nodes[i]['tag']=='DW_TAG_member' and nodes[i]['attrs'].get('name')==name)
    return integer(child['attrs']['data_member_location']),nodes[ref(child['attrs']['type'])]
def describe(nodes,node,names):
    node=concrete(nodes,node)
    return dict(bytes=integer(node['attrs']['byte_size']),members={name:member(nodes,node,name)[0]for name in names})

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--elf',type=Path,default=ROOT/'build/nds/n22/candidate-partial01-ru/dok_nds_gameplay.elf')
    parser.add_argument('--dump',type=Path);parser.add_argument('--output',type=Path,default=ROOT/'research/evidence/nds/n22/candidate01-ai-layout.json')
    args=parser.parse_args();elf=args.elf.resolve()
    tool=Path.home()/'.local/msys64/opt/wonderful/toolchain/gcc-arm-none-eabi/bin/arm-none-eabi-readelf.exe'
    command=[str(tool),'--debug-dump=info',str(elf)]
    if args.dump:text=args.dump.read_text(encoding='utf-8')
    else:
        env=os.environ.copy();env['PATH']=str(Path.home()/'.local/msys64/opt/wonderful/bin')+';'+env['PATH']
        result=subprocess.run(command,env=env,capture_output=True,text=True,encoding='utf-8',check=True);text=result.stdout
    nodes=parse(text)
    sprite=next(n for n in nodes.values()if n['tag']=='DW_TAG_structure_type' and n['attrs'].get('name')=='SpriteWorld')
    game_offset,game=member(nodes,sprite,'game');ai_offset,ai=member(nodes,game,'ai')
    _,enemies=member(nodes,ai,'enemies');enemy=nodes[ref(concrete(nodes,enemies)['attrs']['type'])]
    _,motion=member(nodes,enemy,'motion');_,worldptr=member(nodes,ai,'world')
    world=nodes[ref(concrete(nodes,worldptr)['attrs']['type'])]
    _,objects=member(nodes,world,'objects');seed=nodes[ref(concrete(nodes,objects)['attrs']['type'])]
    _,scheduler_pointer=member(nodes,ai,'scheduler');scheduler=nodes[ref(concrete(nodes,scheduler_pointer)['attrs']['type'])]
    _,interactions=member(nodes,game,'interactions');_,doors=member(nodes,interactions,'doors')
    door=nodes[ref(concrete(nodes,doors)['attrs']['type'])]
    _,definition_pointer=member(nodes,world,'definition');definition=nodes[ref(concrete(nodes,definition_pointer)['attrs']['type'])]
    _,definitions=member(nodes,definition,'objects');object_definition=nodes[ref(concrete(nodes,definitions)['attrs']['type'])]
    storage_symbol=elf_symbol(elf,'storage');assert storage_symbol[1]==4
    result=dict(elf=pin(elf),command=command,address_system='ARM9 32-bit pointer and actual ELF DWARF field offsets; no DOS layout',
       storage_symbol=dict(address=storage_symbol[0],bytes=storage_symbol[1]),
       sprite_world=describe(nodes,sprite,['game']),game=describe(nodes,game,['ai','hero','interactions','c2']),
       interactions=describe(nodes,interactions,['doors']),door=describe(nodes,door,['phase','extent','state','ready']),
       ai=describe(nodes,ai,['world','collision','scheduler','enemies','glyph_index','corpses','epoch','count','callbacks','revision','fault','busy']),
       enemy=describe(nodes,enemy,['motion','last_motion_ticks','burst_started','health','glyph','state','corpse','next_cursor','displayed_cursor','displayed_kind']),
       motion=describe(nodes,motion,['x','y','z','heading','angle_parameter','speed','distance']),
       world=describe(nodes,world,['epoch','hero','object_count','glyph_index','objects','maps']),
       seed=describe(nodes,seed,['reference','x','y','z','heading','upper_y','lower_y','definition_index','class_id','pose_known']),
       scheduler=describe(nodes,scheduler,['_dispatching','_in_callback']),
       definition=describe(nodes,definition,['objects']),
       object_definition=describe(nodes,object_definition,['class_id','flags','height_parameter','glyph']),
       ai_address_formula=f'*(uint32_t *)0x{storage_symbol[0]:08x}+{game_offset}+{ai_offset}',
       source_evidence=dict(sprite_die=hex(sprite['offset']),game_die=hex(concrete(nodes,game)['offset']),ai_die=hex(concrete(nodes,ai)['offset']),enemy_die=hex(concrete(nodes,enemy)['offset'])),
       constraints=['Pinned ELF only; derive again for other builds','Read-only model RAM; do not inject cache flushes or state changes',
                    'Use stable published flow/world epoch and compare callbacks; reject inconsistent or out-of-range pointers'])
    args.output.parent.mkdir(parents=True,exist_ok=True);args.output.write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(result,indent=2))

if __name__=='__main__':main()
