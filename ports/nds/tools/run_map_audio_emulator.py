"""N20 focused title-stream regression, with the real map/discovery workload."""
from pathlib import Path
import json
import make_assets_fixture as fat
import run_audio_emulator as audio

ROOT=Path(__file__).resolve().parents[3]

def analyze_title(target):
    target=Path(target)
    captured=json.loads((target/"audio/result.json").read_text("utf8"))
    live=captured["live_title"];probe=live["end_probe"]
    clip=audio.original_clip(captured["language"],110)
    waveform=audio.compare_capture(ROOT/live["wav"],clip,33513982/(2*probe[12]),
        output_rate=captured["output_sample_rate"])
    passed=(captured["passed"] and probe[7]==len(clip["samples"]) and waveform["complete_source_covered"]
        and waveform["whole_coefficient"] is not None and waveform["whole_coefficient"]>=.95)
    report=dict(task="N20",passed=passed,scope="Actual complete title110 PCM under the frozen map/discovery runtime; no HUD/map input acceptance claim",
        rom_sha256=captured["rom_sha256"],waveform=waveform,
        audio_report=str((target/"audio/result.json").relative_to(ROOT)).replace("\\","/"))
    (target/"audio/title-gate.json").write_text(json.dumps(report,indent=2)+"\n","utf8")
    return report

def main():
    writer=fat.write_image
    def with_ui(path,files):
        return writer(path,[*files,("ui.dat",b"UI      DAT",(ROOT/"build/nds/n19/ui.dat").read_bytes())])
    try:
        fat.write_image=with_ui
        code=audio.main()
        frontend=audio.AudioFrontend.active_instance
        report=analyze_title(frontend.target)
        print(json.dumps(dict(passed=report["passed"],whole_coefficient=report["waveform"]["whole_coefficient"])))
        return 0 if code==0 and report["passed"] else 1
    finally:fat.write_image=writer

if __name__=="__main__":raise SystemExit(main())
