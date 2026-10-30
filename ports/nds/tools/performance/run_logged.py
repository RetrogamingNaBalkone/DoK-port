"""Keep native libretro output in a UTF-8/raw log and return its real exit code."""
from pathlib import Path
import subprocess
import sys
log=Path(sys.argv[1]);log.parent.mkdir(parents=True,exist_ok=True)
with log.open('wb')as stream:
    result=subprocess.run([sys.executable,'-B','-X','utf8',*sys.argv[2:]],stdout=stream,stderr=subprocess.STDOUT)
print(f'{log}: exit {result.returncode}')
raise SystemExit(result.returncode)
