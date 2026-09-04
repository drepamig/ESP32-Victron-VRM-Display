"""Validated scenario and serial boundaries, independent of the emulator."""
import re
from pathlib import PurePosixPath


def duration_ms(value):
    if not isinstance(value, str) or not re.fullmatch(r'\d+(?:\.\d+)?(?:ms|s)', value):
        raise ValueError(f'invalid guest duration: {value!r}')
    scale = 1 if value.endswith('ms') else 1000
    result = float(value[:-2] if scale == 1 else value[:-1]) * scale
    if result > 600_000:
        raise ValueError('guest duration exceeds scenario limit')
    return result


def touch_registers(x=None, y=None):
    registers = bytearray(256)
    registers[0xA8], registers[0xA3], registers[0x80] = 0x11, 0x06, 40
    if x is not None:
        if type(x) is not int or type(y) is not int or not (0 <= x <= 239 and 0 <= y <= 319):
            raise ValueError('invalid FT6206 panel coordinate')
        registers[2:7] = bytes([1, x >> 8, x & 255, y >> 8, y & 255])
    return registers


def validate_steps(steps, captures):
    if not isinstance(steps, list) or not steps:
        raise ValueError('scenario steps must be a nonempty list')
    supported = {'wait-serial', 'write-serial', 'delay', 'touch', 'touch-press',
                 'touch-release', 'take-screenshot', 'reboot'}
    seen = []
    for step in steps:
        if not isinstance(step, dict) or len(step) != 1:
            raise ValueError('each step must have one operation')
        op, value = next(iter(step.items()))
        if op not in supported:
            raise ValueError(f'unsupported local step: {op}')
        if op == 'delay':
            duration_ms(value)
        elif op in ('wait-serial', 'write-serial'):
            if not isinstance(value, str) or not value:
                raise ValueError('serial step requires text')
            if op == 'write-serial' and not re.fullmatch(r'SIM (?:reset|(?:clock|scan|connect|modbus|wan)=[a-z]+)\n', value):
                raise ValueError('only dummy SIM fixtures may be submitted')
        elif op == 'reboot':
            if value != 'preserve-flash':
                raise ValueError('unsupported reboot mode')
        else:
            if not isinstance(value, dict) or value.get('part-id') != 'lcd1':
                raise ValueError('only lcd1 is supported')
            if op in ('touch', 'touch-press'):
                touch_registers(value.get('x'), value.get('y'))
                if value.get('x') is None:
                    raise ValueError('touch needs coordinates')
            if op == 'touch':
                duration_ms(value.get('duration'))
                if value.get('wait') is not True:
                    raise ValueError('asynchronous touch is unsupported')
            if op == 'take-screenshot':
                path = PurePosixPath(value.get('save-to', ''))
                # Existing Wokwi paths are logical checkpoint identifiers only.
                # The local runner never writes to the requested path.
                if (len(path.parts) != 7 or path.parts[:5] != ('..', '..', 'build', 'simulation', 'results')
                        or path.suffix != '.png' or path.stem not in captures):
                    raise ValueError('invalid capture path or checkpoint')
                seen.append(path.stem)
    if seen != list(captures) or len(set(seen)) != len(seen):
        raise ValueError('capture sequence does not match manifest')
    return steps


class SerialGuard:
    def __init__(self):
        self.data = bytearray()
        self.ack_count = 0
        self.ready = False
        self.warnings = []
        self._pending = bytearray()

    def feed(self, data):
        self.data.extend(data)
        self._pending.extend(data)
        tail = bytes(self.data[-2048:]).lower()
        for marker in (b'assert failed:', b'guru meditation', b'backtrace:', b'rebooting...',
                       b'watchdog got triggered', b'interrupt wdt', b'abort() was called'):
            if marker in tail:
                raise ValueError(f'firmware failure: {marker.decode()}')
        while b'\n' in self._pending:
            line, _, remaining = self._pending.partition(b'\n')
            self._pending = bytearray(remaining)
            text = line.decode(errors='replace').strip()
            if text == 'SIM READY':
                if self.ready:
                    raise ValueError('unexpected firmware reboot')
                self.ready = True
            if text == 'SIM OK':
                self.ack_count += 1
            if text == 'SIM ERROR':
                raise ValueError('firmware rejected fixture')
            if 'task_wdt:' in text:
                if 'esp_task_wdt_init(517): TWDT already initialized' in text and not self.ready:
                    self.warnings.append(text)
                else:
                    raise ValueError('firmware watchdog error')

    def ack_after(self, cursor):
        return self.ack_count > cursor


def validate_snapshot(event, actual_hash):
    if event.get('clean_shutdown') is not True:
        raise ValueError('flash requires a clean shutdown')
    if event.get('sha256') != actual_hash:
        raise ValueError('retained flash hash mismatch')
