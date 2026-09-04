"""Bounded worker process and ordered virtual-clock/event transport."""
import base64
import hashlib
import json
from pathlib import Path
import queue
import subprocess
import sys
import threading
import time

try:
    from .protocol import SerialGuard, touch_registers, validate_snapshot
except ImportError:
    from protocol import SerialGuard, touch_registers, validate_snapshot


class WorkerSession:
    def __init__(self, output, event_stream, firmware, *, command=None,
                 ready_timeout=30, deadline=None, segment=0):
        self.output = output
        self.stream = event_stream
        self.deadline = deadline or time.monotonic() + 600
        self.guard = SerialGuard()
        self.events = queue.Queue()
        self.sequence = 0
        self.responses = {}
        self.snapshot = None
        self.transport_complete = False
        self.eof = False
        self.closed = False
        self.last_clock = 0
        self.segment = segment
        self.stderr = (output / f'worker-{segment}.stderr.log').open('w')
        self.serial = (output / f'serial-{segment}.log').open('wb')
        self.process = subprocess.Popen(command or [sys.executable, str(Path(__file__).with_name('worker.py'))],
                                        stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                        stderr=self.stderr, text=True, bufsize=1)
        def reader():
            try:
                for line in self.process.stdout:
                    self.events.put(line)
            finally:
                self.events.put(None)
        self.reader = threading.Thread(target=reader, daemon=True)
        self.reader.start()
        try:
            self.send({'lib_path': '/app/lib/libqemu-xtensa.so',
                       'firmware_b64': base64.b64encode(firmware).decode(),
                       'machine': 'esp32-picsimlab', 'wifi_enabled': False, 'sensors': [],
                       'i2c_proxies': [{'addr': 0x38, 'regs_b64': base64.b64encode(touch_registers()).decode()}]})
            self.wait_until(lambda: self.guard.ready, ready_timeout, 'SIM READY')
        except BaseException:
            self.close()
            raise

    def send(self, command):
        if self.process.poll() is not None:
            raise RuntimeError(f'worker exited {self.process.returncode}')
        self.process.stdin.write(json.dumps(command) + '\n')
        self.process.stdin.flush()

    def pump(self, timeout=.05, *, stopping=False):
        if time.monotonic() > self.deadline and not stopping:
            raise TimeoutError('scenario wall-clock deadline exceeded')
        try:
            line = self.events.get(timeout=timeout)
        except queue.Empty:
            return
        if line is None:
            self.eof = True
            if not stopping:
                raise RuntimeError(f'worker output closed (exit {self.process.poll()})')
            return
        try:
            event = json.loads(line)
            kind = event['type']
            if not isinstance(kind, str):
                raise ValueError()
        except (ValueError, TypeError, KeyError):
            raise ValueError('malformed worker event') from None
        if kind in ('uart_tx', 'spi_batch', 'gpio_change', 'bench_clock', 'bench_barrier',
                    'bench_snapshot', 'bench_complete', 'error', 'crash', 'system'):
            self.stream.write(line)
        if kind == 'uart_tx' and event.get('uart') == 0:
            byte = event.get('byte')
            if type(byte) is not int or not 0 <= byte <= 255:
                raise ValueError('malformed UART event')
            data = bytes([byte])
            self.serial.write(data)
            self.guard.feed(data)
        if kind in ('bench_clock', 'bench_barrier'):
            if event.get('dropped', 0) != 0:
                raise ValueError('worker lost transport events')
            if type(event.get('ns')) is not int or event['ns'] < self.last_clock:
                raise ValueError('invalid or backwards guest clock event')
            self.last_clock = event['ns']
            self.responses[event.get('id')] = event
        if kind == 'bench_snapshot':
            self.snapshot = event
        if kind == 'bench_complete':
            if event.get('dropped') != 0:
                raise ValueError('worker lost transport events')
            self.transport_complete = True
        if kind in ('error', 'crash'):
            raise ValueError(f'worker {kind}: {event.get("message", "unknown")}')

    def wait_until(self, predicate, seconds=5, label='worker response'):
        end = min(self.deadline, time.monotonic() + seconds)
        while not predicate():
            if time.monotonic() >= end:
                raise TimeoutError(f'timed out waiting for {label}')
            self.pump(min(.05, max(.001, end - time.monotonic())))

    def request(self, cmd):
        self.sequence += 1
        identifier = self.sequence
        self.send({'cmd': cmd, 'id': identifier})
        self.wait_until(lambda: identifier in self.responses, label=cmd)
        return self.responses.pop(identifier)

    def guest_wait(self, milliseconds, *, stall_timeout=15):
        previous = self.request('bench_clock')['ns']
        target = previous + int(milliseconds * 1_000_000)
        last_progress = time.monotonic()
        while previous < target:
            self.pump(.025)
            current = self.request('bench_clock')['ns']
            if current > previous:
                last_progress = time.monotonic()
            elif time.monotonic() - last_progress > stall_timeout:
                raise TimeoutError('guest clock stalled')
            previous = current

    def touch(self, x=None, y=None):
        self.send({'cmd': 'proxy_i2c_update', 'addr': 0x38,
                   'regs_b64': base64.b64encode(touch_registers(x, y)).decode()})

    def fixture(self, text):
        cursor = self.guard.ack_count
        self.send({'cmd': 'uart_send', 'uart': 0, 'data': base64.b64encode(text.encode()).decode()})
        self.wait_until(lambda: self.guard.ack_after(cursor), label='fresh fixture acknowledgement')

    def capture(self, name):
        barrier = self.request('bench_barrier')
        self.stream.write(json.dumps({'type': 'bench_capture', 'name': name,
                                     'guest_ns': barrier['ns'], 'segment': self.segment}) + '\n')
        self.stream.flush()
        self.serial.flush()

    def stop(self, *, retain=False):
        path = self.output / 'retained-flash.bin'
        if retain:
            path.unlink(missing_ok=True)
        self.send({'cmd': 'stop', 'retain_flash': retain})
        end = time.monotonic() + 10
        while not self.eof:
            if time.monotonic() > end:
                raise TimeoutError('worker shutdown timeout; flash invalid')
            self.pump(.05, stopping=True)
        self.process.wait(timeout=1)
        if self.process.returncode != 0:
            raise RuntimeError(f'worker shutdown failed: exit {self.process.returncode}')
        if not self.transport_complete:
            raise ValueError('worker transport completion record missing')
        data = None
        if retain:
            if not path.is_file() or path.stat().st_size != 4_194_304:
                raise ValueError('retained flash missing or wrong size')
            data = path.read_bytes()
            validate_snapshot(self.snapshot or {}, hashlib.sha256(data).hexdigest())
        self.close()
        return data

    def close(self):
        if self.closed:
            return
        self.closed = True
        if self.process.poll() is None:
            self.process.kill()
        self.process.wait(timeout=5)
        self.reader.join(timeout=1)
        self.process.stdin.close()
        self.process.stdout.close()
        self.serial.close()
        self.stderr.close()
