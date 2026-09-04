"""Container entry point: execute one validated scenario, retain real pixels."""
import hashlib
import json
from pathlib import Path
import subprocess
import time

from PIL import Image
import yaml

from protocol import duration_ms, validate_steps
from session import WorkerSession


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def compare_captures(inputs, output, names):
    comparisons = []
    for name in names:
        raw = output / f'{name}.rgba'
        if not raw.is_file() or raw.stat().st_size != 320*240*4:
            raise ValueError(f'missing or invalid capture: {name}')
        actual = Image.frombytes('RGBA', (320, 240), raw.read_bytes())
        png = output / f'{name}.png'
        actual.save(png)
        expected = inputs / 'goldens' / f'{name}.png'
        match = False
        count = None
        if expected.is_file():
            with Image.open(expected) as baseline:
                baseline.load()
                if baseline.size != (320, 240) or baseline.mode != 'RGBA':
                    raise ValueError(f'invalid golden format: {name}')
                match = baseline.tobytes() == actual.tobytes()
                count = sum(a != b for a, b in zip(actual.getdata(), baseline.getdata()))
                if not match:
                    diff = actual.copy()
                    diff.putdata([(255, 0, 255, 255) if a != b else a
                                  for a, b in zip(actual.getdata(), baseline.getdata())])
                    diff.save(output / f'{name}.diff.png')
        comparisons.append({'name': name, 'match': match, 'mismatchedPixels': count,
                            'actualSha256': digest(png),
                            'expectedSha256': digest(expected) if expected.exists() else None})
    return comparisons


def main():
    inputs, output = Path('/inputs'), Path('/output')
    request = json.loads((inputs / 'request.json').read_text())
    scenario = request['scenario']
    names = scenario['screenshots']
    start = time.monotonic()
    result = {'executionPassed': False, 'error': None, 'comparisons': [],
              'warnings': [], 'segments': [], 'snapshots': []}
    session = None
    try:
        document = yaml.safe_load((inputs / 'scenario.yaml').read_text())
        steps = validate_steps(document['steps'], names)
        firmware = (inputs / 'firmware.bin').read_bytes()
        with (output / 'events.jsonl').open('w') as stream:
            segment = 0
            session = WorkerSession(output, stream, firmware, deadline=start+600)
            # Initial readiness can precede the last queued display flush.
            session.guest_wait(500)
            for index, step in enumerate(steps):
                op, value = next(iter(step.items()))
                print(f'{scenario["name"]}: step {index+1}/{len(steps)} {op}', flush=True)
                if op == 'wait-serial':
                    if value not in ('SIM READY', 'SIM OK'):
                        raise ValueError(f'unsupported serial assertion: {value}')
                    # fixture() already waited for its own new acknowledgement.
                    if value == 'SIM READY' and not session.guard.ready:
                        raise ValueError('missing readiness')
                    if value == 'SIM OK' and session.guard.ack_count == 0:
                        raise ValueError('missing fixture acknowledgement')
                elif op == 'write-serial':
                    session.fixture(value)
                elif op == 'delay':
                    session.guest_wait(duration_ms(value))
                elif op in ('touch', 'touch-press'):
                    session.touch(value['x'], value['y'])
                    if op == 'touch':
                        session.guest_wait(duration_ms(value['duration']))
                        session.touch()
                elif op == 'touch-release':
                    session.touch()
                elif op == 'take-screenshot':
                    session.capture(Path(value['save-to']).stem)
                elif op == 'reboot':
                    previous = hashlib.sha256(firmware).hexdigest()
                    result['warnings'].extend(session.guard.warnings)
                    result['segments'].append({'ready': session.guard.ready, 'guest_ns': session.last_clock})
                    firmware = session.stop(retain=True)
                    result['snapshots'].append({'parentSha256': previous,
                                                'sha256': hashlib.sha256(firmware).hexdigest()})
                    stream.write(json.dumps({'type': 'bench_restart'}) + '\n')
                    segment += 1
                    session = WorkerSession(output, stream, firmware, deadline=start+600, segment=segment)
                    session.guest_wait(500)
            result['warnings'].extend(session.guard.warnings)
            result['segments'].append({'ready': session.guard.ready, 'guest_ns': session.last_clock})
            session.stop()
        subprocess.run(['node', '/runner/replay.mts', str(output)], check=True, timeout=60)
        result['comparisons'] = compare_captures(inputs, output, names)
        result['executionPassed'] = True
    except Exception as exc:
        result['error'] = f'{type(exc).__name__}: {exc}'
    finally:
        if session:
            session.close()
        result['wallSeconds'] = round(time.monotonic() - start, 3)
        (output / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result), flush=True)
    return 0 if result['executionPassed'] and all(c['match'] for c in result['comparisons']) else 1


if __name__ == '__main__':
    raise SystemExit(main())
