"""N20 supplemental real attack/drag/pause gate on the frozen gameplay ROM.

The main RU/EN map runs prove native pixels. This separate fixture supplies
the accepted combat route's real weapon/charge pickups and tests concurrency.
"""
from pathlib import Path
import json
import sys
import run_map_emulator as map_run

BaseMapFrontend=map_run.MapFrontend

class CombatFrontend(BaseMapFrontend):
    combat_proven=False
    def check_stage(self,name,p):
        super().check_stage(name,p)
        if name in ("live firing while dragging","live drag with firing"):
            before=self.map_report["stages"][-1]["probe"]
            assert p[39]>before[39],"Actual attack callbacks did not advance during the touch gesture"
        if name=="postresume clock and combat":
            paused=next(s["probe"] for s in self.map_report["stages"] if s["name"]=="paused movement ignored")
            assert p[39]>paused[39],"Actual attack callbacks did not resume"
            self.combat_proven=True

def main():
    original=map_run.MapFrontend
    try:
        map_run.MapFrontend=CombatFrontend
        if "--input-only" not in sys.argv:sys.argv.append("--input-only")
        code=map_run.main()
        frontend=CombatFrontend.active_instance
        audio=json.loads((frontend.target/"audio/result.json").read_text("utf8"))
        passed=code==0 and frontend.combat_proven and audio["passed"]
        report=dict(task="N20",passed=passed,scope="Supplemental actual combat callbacks during touch, genuine pause and resume; native map pixel proof belongs to the main RU/EN runs",
            combat_proven=frontend.combat_proven,rom_sha256=frontend.map_report["rom_sha256"],
            observed_stages=len(frontend.map_report["stages"]),input_report=str((frontend.target/"map/result.json").relative_to(map_run.ROOT)).replace("\\","/"))
        (frontend.target/"map/combat-result.json").write_text(json.dumps(report,indent=2)+"\n","utf8")
        print(json.dumps(report))
        return 0 if passed else 1
    finally:map_run.MapFrontend=original

if __name__=="__main__":raise SystemExit(main())
