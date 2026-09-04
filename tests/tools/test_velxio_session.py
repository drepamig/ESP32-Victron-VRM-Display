import importlib
import io
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools'))

FAKE_WORKER = '''
import json,sys
json.loads(sys.stdin.readline())
for b in b'SIM READY\\n': print(json.dumps({'type':'uart_tx','uart':0,'byte':b}),flush=True)
for line in sys.stdin:
 c=json.loads(line)
 if c['cmd'] in ('bench_clock','bench_barrier'):
  print(json.dumps({'type':c['cmd'],'id':c['id'],'ns':0}),flush=True)
 if c['cmd']=='stop': break
'''


class SessionTests(unittest.TestCase):
    def session(self, temporary, script=FAKE_WORKER):
        module = importlib.util.find_spec('velxio.session')
        self.assertIsNotNone(module, 'maintained process session missing')
        cls = importlib.import_module('velxio.session').WorkerSession
        return cls(Path(temporary), io.StringIO(), b'firmware',
                   command=[sys.executable, '-u', '-c', script], ready_timeout=.5)

    def test_stalled_guest_clock_fails_and_worker_is_reaped(self):
        with tempfile.TemporaryDirectory() as d:
            session = self.session(d)
            try:
                with self.assertRaisesRegex(TimeoutError, 'clock'):
                    session.guest_wait(1, stall_timeout=.1)
            finally:
                session.close()
            self.assertIsNotNone(session.process.poll())

    def test_exit_without_ready_is_a_failure(self):
        with tempfile.TemporaryDirectory() as d:
            with self.assertRaisesRegex((ValueError, RuntimeError), 'closed|exit'):
                self.session(d, 'import sys; sys.stdin.readline()')

    def test_malformed_worker_output_is_failure(self):
        with tempfile.TemporaryDirectory() as d:
            with self.assertRaisesRegex(ValueError, 'event'):
                self.session(d, "import sys; sys.stdin.readline(); print('not json',flush=True)")

    def test_shutdown_without_final_transport_record_fails(self):
        with tempfile.TemporaryDirectory() as d:
            session = self.session(d)
            try:
                with self.assertRaisesRegex(ValueError, 'transport'):
                    session.stop()
            finally:
                session.close()

    def test_dropped_events_cannot_be_accepted_as_clean_shutdown(self):
        script = FAKE_WORKER.replace("if c['cmd']=='stop': break", "if c['cmd']=='stop':\n  print(json.dumps({'type':'bench_complete','dropped':1}),flush=True)\n  break")
        with tempfile.TemporaryDirectory() as d:
            session = self.session(d, script)
            try:
                with self.assertRaisesRegex(ValueError, 'lost'):
                    session.stop()
            finally:
                session.close()


if __name__ == '__main__':
    unittest.main()
