"""Diagnostic ordinary-input route with extra cache/callback timers; not product FPS."""
import sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'flow'))
from fine_observer import install_fine_observer
install_fine_observer()
import ordinary
if __name__=='__main__':raise SystemExit(ordinary.main())
