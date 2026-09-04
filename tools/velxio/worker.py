"""Small, hash-guarded additions to the pinned upstream Velxio worker.

The upstream worker is loaded from the offline Docker image, never downloaded
at execution time. See THIRD_PARTY.md for provenance and licensing.
"""
import hashlib
import json
from pathlib import Path
import sys


def adapt_worker(source, expected_hash):
    if hashlib.sha256(source.encode()).hexdigest() != expected_hash:
        raise ValueError('upstream worker hash mismatch')

    def replace(old, new):
        nonlocal source
        if source.count(old) != 1:
            raise ValueError('upstream worker adapter anchor mismatch')
        source = source.replace(old, new)

    anchor = '    # Pre-register initial sensors before letting QEMU execute firmware.\n'
    replace(anchor, '''    for proxy in cfg.get('i2c_proxies', []):
        addr = int(proxy['addr'])
        _i2c_slaves[addr] = _ProxySlave(addr, base64.b64decode(proxy['regs_b64']), emit_fn=_emit)
''' + anchor)
    replace('    argc = len(args_list)\n', "    args_list.extend([b'-icount', b'3'])\n    argc = len(args_list)\n")
    replace("        if c == 'set_pin':\n", '''        if c in ('bench_clock', 'bench_barrier'):
            clock_fn = lib.qemu_clock_get_ns
            clock_fn.restype = ctypes.c_int64
            clock_fn.argtypes = [ctypes.c_int]
            if not _lock_iothread or not _unlock_iothread:
                _emit({'type': 'error', 'message': 'QEMU clock lock unavailable'})
                continue
            _lock_iothread(b'cyd_bench:clock', 0)
            try:
                ns = int(clock_fn(1))
                # Preserve byte/GPIO order and flush before acknowledging capture.
                if c == 'bench_barrier':
                    with _spi_buf_lock:
                        _flush_spi_batch_locked()
                _emit({'type': c, 'ns': ns, 'id': cmd.get('id'), 'dropped': _emit_dropped})
            finally:
                _unlock_iothread()
            continue
        if c == 'set_pin':
''')
    # Cleanup on the QEMU thread (not the JSON-command thread) flushes block I/O.
    replace('        lib.qemu_main_loop()\n', '        lib.qemu_main_loop()\n        lib.qemu_cleanup()\n')
    replace('            # Clean up temp firmware file\n', '''            if qemu_t.is_alive():
                _emit({'type': 'error', 'message': 'QEMU shutdown timeout; flash invalid'})
                import time as _bench_time
                _bench_time.sleep(0.1)
                os._exit(1)
            if cmd.get('retain_flash'):
                import hashlib as _bench_hashlib
                import shutil as _bench_shutil
                _bench_shutil.copyfile(firmware_path, '/output/retained-flash.bin')
                _emit({'type': 'bench_snapshot', 'clean_shutdown': True,
                       'sha256': _bench_hashlib.sha256(open('/output/retained-flash.bin', 'rb').read()).hexdigest()})
            # Clean up temp firmware file
''')
    replace('''            try:
                _emit_q.put_nowait(None)
                for t in threading.enumerate():
                    if t.name == 'emit-writer':
                        t.join(timeout=2.0)
            except Exception:
                pass
            os._exit(0)
''', '''            try:
                # QEMU has stopped. Blocking here cannot stall guest callbacks.
                _emit_q.put({'type': 'bench_complete', 'dropped': _emit_dropped}, timeout=5)
                _emit_q.put(None, timeout=5)
                writers = [t for t in threading.enumerate() if t.name == 'emit-writer']
                for t in writers:
                    t.join(timeout=5)
                if any(t.is_alive() for t in writers):
                    os._exit(1)
            except Exception:
                os._exit(1)
            os._exit(1 if _emit_dropped else 0)
''')
    return source


def main():
    original = Path('/app/app/services/esp32_worker.py')
    lock = json.loads(Path('/inputs/runtime-lock.json').read_text())
    source = adapt_worker(original.read_text(), lock['workerSha256'])
    sys.path.insert(0, '/app')
    exec(compile(source, str(original), 'exec'), {'__name__': '__main__', '__file__': str(original)})


if __name__ == '__main__':
    main()
