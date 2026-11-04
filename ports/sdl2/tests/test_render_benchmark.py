"""Failure-path tests for exact comparison; no original resources required."""
import argparse
import importlib.util
import json
from pathlib import Path
import shutil
import uuid
import zlib
import unittest

spec = importlib.util.spec_from_file_location("bench", Path(__file__).resolve().parents[1] / "tools/render_benchmark.py")
bench = importlib.util.module_from_spec(spec)
spec.loader.exec_module(bench)


class ComparisonTests(unittest.TestCase):
    def setUp(self):
        parent = (bench.ROOT / "build/sdl2/benchmark-tests").resolve()
        self.root = parent / uuid.uuid4().hex
        self.root.mkdir(parents=True)
        def cleanup():
            if not self.root.resolve().is_relative_to(parent):
                raise ValueError("Test cleanup escapes build directory")
            shutil.rmtree(self.root)
        self.addCleanup(cleanup)
        self.a, self.b = self.root / "a", self.root / "b"
        self.make_run(self.a)
        self.make_run(self.b)

    def make_run(self, root):
        folder = root / "job"
        folder.mkdir(parents=True)
        raw = bytes([1, 2, 3, 255] * 2)
        (folder / "case.rgba.zlib").write_bytes(zlib.compress(raw))
        (folder / "case.idx.zlib").write_bytes(zlib.compress(bytes([1, 1])))
        bench.png(folder / "case.png", 2, 1, raw)
        timings = {"total": {"p50": 1, "p95": 2}}
        native = {"schema": 1, "fixture_version": 1, "dataset": "test", "backend": "software", "width": 2,
                  "height": 1, "filter": "nearest", "suite": "quick", "cases": [
                      {"id": "case", "pose": [0, 0, 0, 0], "offset": [0] * 4, "ticks": 0, "door": -1,
                       "phase": 0, "volley": False, "arena": [0]*4, "shooters": 0,
                       "active_projectiles": 0, "impacts": 0, "presentation_state_fnv64": "test", "timings_ms": timings}]}
        data = {"schema": 1, "complete": True, "host": {}, "jobs": [{"key": "test", "folder": "job",
                "resource_sha256": "resource", "native": native,
                "captures": {p.name: bench.digest(p) for p in folder.iterdir()}}]}
        bench.write_json(root / "manifest.json", data)

    def edit(self, change):
        path = self.b / "manifest.json"
        data = json.loads(path.read_text())
        change(data)
        bench.write_json(path, data)

    def compare(self):
        return bench.compare(argparse.Namespace(baseline=self.a, current=self.b, output=self.root / "comparison"))

    def test_equal(self):
        self.assertEqual(self.compare(), 0)

    def test_single_channel_change(self):
        path = self.b / "job/case.rgba.zlib"
        data = bytearray(zlib.decompress(path.read_bytes()))
        data[4] = 2
        path.write_bytes(zlib.compress(data))
        self.edit(lambda d: d["jobs"][0]["captures"].update({path.name: bench.digest(path)}))
        self.assertEqual(self.compare(), 1)
        report = json.loads((self.root / "comparison/comparison.json").read_text())
        result = report["cases"][0]["pixels"]["rgba"]
        self.assertEqual(result["changed_pixels"], 1)
        self.assertEqual(result["bbox_inclusive"], [1, 0, 1, 0])
        self.assertEqual(result["max_channel_delta"], 1)
        self.assertTrue((self.root / "comparison" / result["diff"]).is_file())

    def test_index_change_even_with_identical_colors(self):
        path = self.b / "job/case.idx.zlib"
        path.write_bytes(zlib.compress(bytes([2, 1])))
        self.edit(lambda d: d["jobs"][0]["captures"].update({path.name: bench.digest(path)}))
        self.assertEqual(self.compare(), 1)

    def test_different_compression_same_pixels(self):
        path = self.b / "job/case.rgba.zlib"
        path.write_bytes(zlib.compress(zlib.decompress(path.read_bytes()), level=0))
        self.edit(lambda d: d["jobs"][0]["captures"].update({path.name: bench.digest(path)}))
        self.assertEqual(self.compare(), 0)
        report = json.loads((self.root / "comparison/comparison.json").read_text())
        self.assertTrue(report["pixel_exact"])

    def test_missing_capture(self):
        (self.b / "job/case.idx.zlib").unlink()
        with self.assertRaisesRegex(ValueError, "Missing/modified"):
            self.compare()

    def test_tamper(self):
        (self.b / "job/case.idx.zlib").write_bytes(b"xx")
        with self.assertRaisesRegex(ValueError, "Missing/modified"):
            self.compare()

    def test_resource_mismatch(self):
        self.edit(lambda d: d["jobs"][0].update(resource_sha256="different"))
        with self.assertRaisesRegex(ValueError, "Resource mismatch"):
            self.compare()

    def test_incomplete(self):
        self.edit(lambda d: d.update(complete=False))
        with self.assertRaisesRegex(ValueError, "Incomplete"):
            self.compare()

    def test_fixture_mismatch(self):
        self.edit(lambda d: d["jobs"][0]["native"]["cases"][0].update(ticks=334))
        with self.assertRaisesRegex(ValueError, "Fixture state"):
            self.compare()

    def test_case_removed(self):
        self.edit(lambda d: d["jobs"][0]["native"].update(cases=[]))
        with self.assertRaisesRegex(ValueError, "Empty"):
            self.compare()

    def test_dimensions(self):
        self.edit(lambda d: d["jobs"][0]["native"].update(width=3))
        with self.assertRaisesRegex(ValueError, "dimensions"):
            self.compare()

    def test_output_preserved(self):
        (self.root / "comparison").mkdir()
        with self.assertRaisesRegex(ValueError, "already exists"):
            self.compare()

    def test_diagnostic_timers_not_mixed(self):
        self.edit(lambda d: d["jobs"][0]["native"].update(profile_submission=True))
        with self.assertRaisesRegex(ValueError, "Timing instrumentation differs"):
            self.compare()

    def test_gl_driver_mismatch(self):
        for root, renderer in ((self.a, "GPU A"), (self.b, "GPU B")):
            path = root / "manifest.json"
            data = json.loads(path.read_text())
            job = data["jobs"][0]
            job["native"].update(backend="gl", gl_vendor="vendor", gl_renderer=renderer, gl_version="2.0")
            del job["captures"]["case.idx.zlib"]
            bench.write_json(path, data)
        with self.assertRaisesRegex(ValueError, "GL environment"):
            self.compare()


if __name__ == "__main__":
    unittest.main()
