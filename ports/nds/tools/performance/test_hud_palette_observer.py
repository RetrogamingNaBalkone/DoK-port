"""HUD scanout regression: one-VBlank palette changes and real negative controls."""
import importlib.util
from pathlib import Path
import struct
import sys
from types import SimpleNamespace
import unittest

sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import joint_hud_oracle as oracle

class Hud:
    def __init__(self):
        self.ui_address=0x02000000;self.palette=0;self.display_palette=0;self.bad_index=False
        self.hud_report=dict(rows=[],pixel_checks=0,hardware_pixel_checks=0)
        self.hud_stable=None;self.hud_stability=0;self.hud_seen=set();self.hud_capture_seen=set()
        self.messages={};self.art=None
    def capture(self,*args):return []
    def rgb(self):
        v=self.display_palette
        rgb=bytes(((v&31)*8,((v>>5)&31)*8,((v>>10)&31)*8))
        return 256,384,bytes(154*256*3)+rgb*(38*256)+bytes(192*256*3)
    def read(self,address,size):
        if address==self.ui_address:
            p=[0]*64;p[0:7]=[0x19504b44,1,2,1,1,1,1]
            p[9:15]=[100,100,100,100,3,0];p[20]=1;p[23]=1
            p[53]=0x02010000;p[55]=0x02020000
            return struct.pack('<64I',*p)
        if address==0x02010000:return bytes([2 if self.bad_index else 1])*9728
        if address==0x02020000:return struct.pack('<256H',0,self.palette,*([0]*254))
        raise AssertionError((address,size))

def run(observe,hud):
    old_read=oracle.render.ram;old_draw=oracle.render_hud
    # A separately loaded historical module shares the transport module only.
    module=sys.modules.get(observe.__module__)
    if module is None:raise AssertionError(observe.__module__)
    old_module_draw=module.render_hud
    oracle.render.ram=lambda owner,address,size:owner.read(address,size)
    module.render_hud=lambda *args:(bytes([1])*9728,{})
    try:observe(hud)
    finally:
        oracle.render.ram=old_read;oracle.render_hud=old_draw;module.render_hud=old_module_draw

class TestHudPalette(unittest.TestCase):
    def test_fade_then_stable_lcd(self):
        h=Hud()
        for palette,display in [(0,0),(0,0),(0x421,0),(0x842,0x421),(0x842,0x842),(0x842,0x842)]:
            h.palette=palette;h.display_palette=display;run(oracle.observe_joint_hud,h)
        self.assertEqual(h.hud_report['hardware_pixel_checks'],9728)
    def test_wrong_stable_lcd_is_rejected(self):
        h=Hud();h.palette=0x421
        run(oracle.observe_joint_hud,h);run(oracle.observe_joint_hud,h)
        with self.assertRaisesRegex(AssertionError,'scanned HUD RGB'):run(oracle.observe_joint_hud,h)
    def test_wrong_index8_is_rejected(self):
        h=Hud();h.bad_index=True
        run(oracle.observe_joint_hud,h);run(oracle.observe_joint_hud,h)
        with self.assertRaisesRegex(AssertionError,'original-resource pixel mismatch'):run(oracle.observe_joint_hud,h)
    def test_historical_observer_reproduces_false_failure(self):
        root=Path(__file__).resolve().parents[4]
        paths=list((root/'research/evidence/nds/n23').glob('*-joint_hud_oracle-before.py'))
        self.assertEqual(len(paths),1)
        spec=importlib.util.spec_from_file_location('historical_hud_palette_oracle',paths[0])
        module=importlib.util.module_from_spec(spec);sys.modules[spec.name]=module;spec.loader.exec_module(module)
        h=Hud();run(module.observe_joint_hud,h);run(module.observe_joint_hud,h)
        h.palette=0x421
        with self.assertRaisesRegex(AssertionError,'scanned HUD RGB'):run(module.observe_joint_hud,h)

if __name__=='__main__':unittest.main()
