import importlib
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools'))


class ProtocolTests(unittest.TestCase):
    def setUp(self):
        self.assertIsNotNone(importlib.util.find_spec('velxio'), 'maintained local runner is missing')
        self.p = importlib.import_module('velxio.protocol')

    def test_serial_crash_split_across_events_fails(self):
        guard = self.p.SerialGuard()
        guard.feed(b'SIM READY\nassert fai')
        with self.assertRaisesRegex(ValueError, 'firmware'):
            guard.feed(b'led: task.c\n')

    def test_fresh_ack_required_and_repeated_boot_rejected(self):
        guard = self.p.SerialGuard()
        guard.feed(b'SIM READY\nSIM OK\n')
        cursor = guard.ack_count
        self.assertFalse(guard.ack_after(cursor))
        guard.feed(b'SIM OK\n')
        self.assertTrue(guard.ack_after(cursor))
        with self.assertRaisesRegex(ValueError, 'reboot'):
            guard.feed(b'SIM READY\n')

    def test_known_startup_warning_is_reported_but_other_watchdogs_fail(self):
        guard = self.p.SerialGuard()
        guard.feed(b'E (540) task_wdt: esp_task_wdt_init(517): TWDT already initialized\nSIM READY\n')
        self.assertEqual(len(guard.warnings), 1)
        with self.assertRaisesRegex(ValueError, 'firmware'):
            guard.feed(b'Task watchdog got triggered\n')

    def test_steps_reject_unsupported_operations_and_capture_escape(self):
        with self.assertRaisesRegex(ValueError, 'unsupported'):
            self.p.validate_steps([{'exec': 'bad'}], ['boot'])
        with self.assertRaisesRegex(ValueError, 'capture'):
            self.p.validate_steps([{'take-screenshot': {'part-id': 'lcd1', 'save-to': '../secrets.png'}}], ['boot'])

    def test_duration_is_guest_milliseconds_and_invalid_values_fail(self):
        self.assertEqual(self.p.duration_ms('3.3s'), 3300)
        self.assertEqual(self.p.duration_ms('120ms'), 120)
        for invalid in ('-1ms', 'NaNs', '1 minute', 1, 'infms'):
            with self.subTest(invalid=invalid), self.assertRaises(ValueError):
                self.p.duration_ms(invalid)

    def test_contact_release_clears_the_coordinate_frame(self):
        pressed = self.p.touch_registers(239, 319)
        self.assertEqual(pressed[2:7], bytes([1, 0, 239, 1, 63]))
        released = self.p.touch_registers()
        self.assertEqual(released[2:7], bytes(5))
        self.assertEqual(released[0xA8], 0x11)

    def test_worker_shutdown_timeout_cannot_authorize_flash(self):
        with self.assertRaisesRegex(ValueError, 'shutdown'):
            self.p.validate_snapshot({'clean_shutdown': False, 'sha256': 'a'*64}, 'a'*64)
        with self.assertRaisesRegex(ValueError, 'hash'):
            self.p.validate_snapshot({'clean_shutdown': True, 'sha256': 'a'*64}, 'b'*64)


if __name__ == '__main__':
    unittest.main()
