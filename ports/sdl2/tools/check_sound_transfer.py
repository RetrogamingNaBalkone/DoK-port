"""Independent DOS mixer transfer/clock anchors through unchanged native SB code.

The isolated title binding supplies a synthetic staircase; no original file,
sound algorithm or IRQ is edited. Plateaus and cycle edges give amplitude/DC and
period measurements independently of short, periodic original waveform matches.
"""
import json,struct,statistics
from check_assets import read_archive
from capture_sound_matrix import PROFILES,capture,TITLE
from check_audio_reference import read
from native_sound import ROOT,OUT,sha

def main():
 rows=[];before={}
 for profile,(folder,exe,res)in PROFILES.items():
  source=ROOT/'originals'/folder/res;before[str(source.relative_to(ROOT))]=sha(source)
  raw=dict(read_archive(source.read_bytes()))[TITLE];header=bytearray(raw[:36]);samples=b''.join(bytes([value])*512 for value in(16,48,80,112,144,176,208,240));payload=bytes(header)+samples+(b'\0'if profile=='old-gold'else b'')
  payload=bytearray(payload);struct.pack_into('<I',payload,16,16000);struct.pack_into('<I',payload,20,len(payload)-36)
  result=capture(profile,98,b'SP13_STAIRCASE',bytes(payload),28841);actual=read(ROOT/result['wav']['file'])
  # Low-going cycle boundary, distinct from every +32 interior step.
  mean=[sum(actual[i:i+48])/48 for i in range(0,len(actual)-47,48)]
  threshold=(max(mean)-min(mean))*.5;edges=[]
  for i in range(2,len(mean)-1):
   if mean[i-2]-mean[i+1]>threshold and(not edges or i-edges[-1]>100):edges.append(i)
  # A boundary can straddle two 1ms averages; refine the same negative peak
  # in the original 48kHz samples rather than dropping that cycle.
  peaks=[min(range(max(0,(i-3)*48),min(len(actual),(i+6)*48)),key=lambda at:actual[at])for i in edges]
  edges=[x/48 for x in peaks]
  periods=[(b-a)/1000 for a,b in zip(edges,edges[1:])];assert len(periods)>=4,(profile,edges)
  nominal_period=(74*4097/1193182)if profile=='old-gold'else(63*4096/1000000)
  # Use center of eight plateaus; discard the first/last millisecond.
  cycle=statistics.median(periods)*1000;levels=[]
  for step in range(8):
   values=[]
   for edge in edges[:-1]:
    a=int(edge+(step+.3)*cycle/8);b=int(edge+(step+.7)*cycle/8);values+=mean[a:b]
   levels.append(statistics.median(values))
  levels_in=[(v-128)*256 for v in(16,48,80,112,144,176,208,240)];mx=statistics.mean(levels_in);my=statistics.mean(levels);gain=sum((x-mx)*(y-my)for x,y in zip(levels_in,levels))/sum((x-mx)**2 for x in levels_in);dc=my-gain*mx
  residual=[p-nominal_period for p in periods]
  # One output frame uncertainty for DMA; direct DAC includes measured IRQ /
  # mixer-clock jitter. The port intentionally uses a uniform logical clock.
  bound=.015 if profile=='old-gold'else 2/48000
  assert max(abs(x)for x in residual)<=bound,(profile,residual)
  item=dict(profile=profile,capture=result,edges_seconds=[x/1000 for x in edges],cycle_period_seconds=periods,median_cycle_seconds=statistics.median(periods),nominal_cycle_seconds=nominal_period,actual_per_nominal_clock_ratio=nominal_period/statistics.median(periods),cycle_residual_seconds=residual,cycle_tolerance_seconds=bound,plateau_input_s16=levels_in,plateau_output_s16=levels,fitted_gain=gain,fitted_dc=dc,linear_transfer_claim=False,source_fixture='Synthetic native SB staircase, original header with 16000Hz and updated count; original PCM capture matrix is separate')
  rows.append(item);print(profile,item['actual_per_nominal_clock_ratio'],gain,dc,flush=True)
 after={p:sha(ROOT/p)for p in before};assert before==after
 (OUT/'transfer-report.json').write_text(json.dumps(dict(cases=rows,sha256_before=before,sha256_after=after),indent=2)+'\n')
if __name__=='__main__':main()
