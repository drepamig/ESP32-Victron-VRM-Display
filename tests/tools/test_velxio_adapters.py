import importlib
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))


class AdapterTests(unittest.TestCase):
    def test_worker_refuses_changed_upstream_source(self):
        self.assertTrue((ROOT / 'tools/velxio/worker.py').is_file(), 'guarded worker adapter missing')
        worker = importlib.import_module('velxio.worker')
        with self.assertRaisesRegex(ValueError, 'hash'):
            worker.adapt_worker('unrecognized upstream source', '0'*64)

    def test_decoder_inversion_and_repeated_window_cursor(self):
        self.assertTrue((ROOT / 'tools/velxio/display.mts').is_file(), 'maintained display decoder missing')
        import shutil
        import subprocess
        node = shutil.which('node')
        if not node:
            self.skipTest('Node decoder assertions run in the pinned Velxio tooling image')
        result = subprocess.run([node, str(ROOT / 'tests/tools/velxio_display_test.mts')], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == '__main__':
    unittest.main()
