"""Build the three SP13 consumer probes against the current Release core."""
import argparse,os,subprocess
from pathlib import Path
from concurrent.futures import ThreadPoolExecutor
from compiler_cache import compiler_cache

ROOT=Path(__file__).resolve().parents[3]
def main():
 parser=argparse.ArgumentParser();parser.add_argument('--zig',type=Path,default=Path(os.environ['LOCALAPPDATA'])/'Programs/Zig/0.13.0/zig.exe');args=parser.parse_args()
 core=ROOT/'build/sdl2/windows/libdok_core.a';assert core.is_file(),'Build current Release dok_core first'
 def run(name):
  command=[str(args.zig),'c++','-O2','-std=c++17','-I'+str(ROOT/'ports/sdl2/include'),str(ROOT/f'ports/sdl2/tests/{name}_probe.cpp'),str(core),'-o',str(ROOT/f'build/sdl2/windows/dok_{name}_probe.exe')]
  result=subprocess.run(command,capture_output=True,text=True);assert result.returncode==0,result.stderr
  print(name,'built',flush=True)
 with compiler_cache():
  with ThreadPoolExecutor(max_workers=2)as pool:list(pool.map(run,('sound_sequence','sound_event_monster','sound_consumer')))
if __name__=='__main__':main()
