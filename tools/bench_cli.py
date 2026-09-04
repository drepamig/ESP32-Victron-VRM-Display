#!/usr/bin/env python3
"""Host Docker orchestration. Runtime containers never receive the Docker socket."""
import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import uuid
from pathlib import Path

import sim_artifacts

TOOLCHAIN = 'victron-cyd-virtual-bench:2026-09-03'
RUNTIME = 'victron-cyd-velxio:2026-09-03'


def execute(args, **kwargs):
    return subprocess.run([str(arg) for arg in args], check=True, **kwargs)


def read_json(path):
    return json.loads(path.read_text(encoding='utf-8'))


def write_json(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2, sort_keys=True) + '\n', encoding='utf-8')


def safe_name(value):
    if not isinstance(value, str) or not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9_-]*', value):
        raise ValueError('unsafe scenario, checkpoint or run name')
    return value


def image_id(image):
    return execute(['docker', 'image', 'inspect', '--format', '{{.Id}}', image], capture_output=True, text=True).stdout.strip()


def cache_identity(repo):
    return {'toolchainImageId': image_id(TOOLCHAIN), 'runtimeImageId': image_id(RUNTIME),
            'files': {name: sim_artifacts.sha256(repo / name) for name in
                      ('.devcontainer/Dockerfile', '.devcontainer/Velxio.Dockerfile', 'simulation/velxio/runtime-lock.json')}}


def verify_cache(repo):
    record = repo / '.tools/velxio/runtime.json'
    if not record.is_file() or read_json(record) != cache_identity(repo):
        raise ValueError('local runtime cache missing or stale; run tools/dev.ps1 setup')
    return read_json(record)


def workspace_command(repo, image, command, online=False):
    args = ['docker', 'run', '--rm', '--init']
    if not online:
        args += ['--network', 'none']
    else:
        args += ['--env', 'WOKWI_CLI_TOKEN']
    return args + ['--volume', f'{repo}:/workspace', '--workdir', '/workspace', '--entrypoint', command[0], image] + command[1:]


def runtime_command(repo, inputs, output, image, container_name=None):
    return ['docker', 'run', '--rm', '--init', '--network', 'none'] + (['--name', container_name] if container_name else []) + [
            '--volume', f'{repo / "tools/velxio"}:/runner:ro',
            '--volume', f'{inputs}:/inputs:ro', '--volume', f'{output}:/output:rw',
            '--entrypoint', 'python', image, '/runner/run.py']


def select_scenarios(repo, selected, backend):
    scenarios = read_json(repo / 'simulation/scenario-manifest.json')['scenarios']
    scenarios = [item for item in scenarios if backend in item.get('backends', ['velxio', 'wokwi'])]
    if backend == 'velxio':
        config = read_json(repo / 'simulation/velxio/scenarios.json')
        supported = config if isinstance(config, list) else config['supported']
        gaps = [item['name'] for item in scenarios if item['name'] not in supported]
        if selected and selected not in supported:
            raise ValueError(f'unsupported local scenario: {selected}; coverage gaps: {", ".join(gaps)}')
        scenarios = [item for item in scenarios if item['name'] in supported]
        if not selected:
            print('Local scenarios: ' + ', '.join(item['name'] for item in scenarios))
            print('Coverage gaps: ' + ', '.join(gaps))
    if selected:
        scenarios = [item for item in scenarios if item['name'] == selected]
    if not scenarios:
        raise ValueError('no matching scenarios')
    # Keep the original manifest intact: local records and promotion consistently
    # bind the selected backend file while retaining its override metadata.
    scenarios = [dict(item, file=item.get('backendFiles', {}).get(backend, item['file']))
                 for item in scenarios]
    for item in scenarios:
        safe_name(item['name'])
        for checkpoint in item['screenshots']:
            safe_name(checkpoint)
        source = repo / 'simulation' / item['file']
        if not source.is_file() or source.is_symlink() or not sim_artifacts._inside(source, repo / 'simulation'):
            raise ValueError('unsafe or missing scenario file')
    return scenarios


def build(repo, mode):
    image = verify_cache(repo)['toolchainImageId'] if mode == 'velxio' else TOOLCHAIN
    execute(workspace_command(repo, image, ['bash', 'tools/arduino-build.sh', mode]))


def local_test(repo, scenarios):
    cache = verify_cache(repo)
    try:
        attestation = sim_artifacts.verify_attestation(repo, 'velxio')
    except (ValueError, OSError):
        build(repo, 'velxio')
        attestation = sim_artifacts.verify_attestation(repo, 'velxio')
    run_id = uuid.uuid4().hex
    failed = False
    for scenario in scenarios:
        if sim_artifacts.verify_attestation(repo, 'velxio') != attestation:
            raise ValueError('attestation changed during run')
        output = repo / 'build/velxio/results' / run_id / scenario['name']
        inputs = output.parent / '_inputs' / scenario['name']
        inputs.mkdir(parents=True)
        output.mkdir(parents=True)
        write_json(inputs / 'request.json', {'scenario': scenario})
        for source, target in [(repo / 'simulation' / scenario['file'], 'scenario.yaml'),
                               (repo / 'build/velxio/firmware.bin', 'firmware.bin'),
                               (repo / 'build/velxio/attestation.json', 'attestation.json'),
                               (repo / 'simulation/velxio/runtime-lock.json', 'runtime-lock.json')]:
            shutil.copyfile(source, inputs / target)
        goldens = inputs / 'goldens'
        goldens.mkdir()
        for checkpoint in scenario['screenshots']:
            golden = repo / 'simulation/goldens' / scenario['name'] / f'{checkpoint}.png'
            if golden.is_file():
                shutil.copyfile(golden, goldens / golden.name)
        record = {'schema': 1, 'runId': run_id, 'scenario': scenario,
                  'scenarioSha256': sim_artifacts.sha256(inputs / 'scenario.yaml'),
                  'attestation': attestation, 'runtimeIdentity': sim_artifacts.runtime_identity(repo),
                  'runtimeImageId': cache['runtimeImageId'], 'captures': {}, 'complete': False}
        record['toolchainImageId'] = cache['toolchainImageId']
        write_json(output / 'run.json', record)
        container_name = f'cyd-velxio-{run_id}-{scenario["name"]}'
        try:
            execute(runtime_command(repo, inputs, output, cache['runtimeImageId'], container_name), timeout=630)
        except (subprocess.CalledProcessError, subprocess.TimeoutExpired):
            failed = True
        finally:
            # A timed-out Docker client does not stop its container. Explicitly reap
            # the uniquely named container, including on keyboard interruption.
            subprocess.run(['docker', 'rm', '--force', container_name], check=False,
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=20)
        if (output / 'result.json').is_file():
            result = read_json(output / 'result.json')
            record['resultSha256'] = sim_artifacts.sha256(output / 'result.json')
            for checkpoint in scenario['screenshots']:
                capture = output / f'{checkpoint}.png'
                if capture.is_file() and not capture.is_symlink():
                    record['captures'][checkpoint] = sim_artifacts.sha256(capture)
            record['complete'] = result.get('executionPassed') is True and len(record['captures']) == len(scenario['screenshots'])
            failed |= not record['complete'] or any(not item.get('match') for item in result.get('comparisons', []))
        else:
            failed = True
        write_json(output / 'run.json', record)
    print(f'Retained local run: {run_id}')
    return int(failed)


def validate_golden_destinations(repo, scenario_name, checkpoints):
    """Reject redirected destinations before any capture is copied."""
    root = repo / 'simulation/goldens'
    destinations = [root / scenario_name / f'{checkpoint}.png' for checkpoint in checkpoints]
    for destination in destinations:
        if not sim_artifacts._inside(destination, root):
            raise ValueError('unsafe golden destination')
        for path in (destination, *destination.parents):
            if path == repo.parent:
                break
            if path.is_symlink() or (hasattr(path, 'is_junction') and path.is_junction()):
                raise ValueError('golden destination contains a link')
    return destinations


def promote(repo, run_id, scenario_name):
    safe_name(run_id)
    safe_name(scenario_name)
    scenario = select_scenarios(repo, scenario_name, 'velxio')[0]
    output = repo / 'build/velxio/results' / run_id / scenario_name
    if not sim_artifacts._inside(output, repo / 'build/velxio/results') or output.is_symlink():
        raise ValueError('unsafe recorded run path')
    record = read_json(output / 'run.json')
    if (not record.get('complete') or record.get('runId') != run_id or record.get('scenario') != scenario
            or record.get('attestation') != sim_artifacts.verify_attestation(repo, 'velxio')
            or record.get('runtimeIdentity') != sim_artifacts.runtime_identity(repo)
            or record.get('runtimeImageId') != verify_cache(repo)['runtimeImageId']
            or record.get('toolchainImageId') != verify_cache(repo).get('toolchainImageId')
            or record.get('scenarioSha256') != sim_artifacts.sha256(repo / 'simulation' / scenario['file'])
            or record.get('resultSha256') != sim_artifacts.sha256(output / 'result.json')):
        raise ValueError('run is stale, incomplete or tampered')
    result = read_json(output / 'result.json')
    if result.get('executionPassed') is not True:
        raise ValueError('run execution failed')
    captures = []
    for checkpoint in scenario['screenshots']:
        source = output / f'{checkpoint}.png'
        if source.is_symlink() or not source.is_file() or record['captures'].get(checkpoint) != sim_artifacts.sha256(source):
            raise ValueError('recorded capture missing or tampered')
        captures.append(source)
    destinations = validate_golden_destinations(repo, scenario_name, scenario['screenshots'])
    target = repo / 'simulation/goldens' / scenario_name
    target.mkdir(parents=True, exist_ok=True)
    for source, destination in zip(captures, destinations):
        shutil.copyfile(source, destination)
    print(f'Promoted {len(captures)} recorded captures from {run_id}/{scenario_name}')


def doctor(repo):
    cache = verify_cache(repo)
    lock = read_json(repo / 'simulation/velxio/runtime-lock.json')
    code = ('import json,subprocess,PIL,yaml; print(json.dumps(dict(nodeVersion=subprocess.check_output(["node","--version"],text=True).strip().lstrip("v"),pillowVersion=PIL.__version__,pyyamlVersion=yaml.__version__)))')
    found = json.loads(execute(['docker', 'run', '--rm', '--network', 'none', '--entrypoint', 'python', cache['runtimeImageId'], '-c', code], capture_output=True, text=True).stdout)
    if any(found[key] != lock[key] for key in found):
        raise ValueError('runtime dependency versions do not match lock')
    print('Offline local runtime pins verified: ' + json.dumps(found))


def parse_args(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('command', nargs='?', default='doctor', choices=['setup', 'doctor', 'test', 'firmware-build', 'sim-build', 'sim-test', 'sim-update-goldens', 'all'])
    parser.add_argument('--backend', choices=['velxio', 'wokwi'], default='velxio')
    parser.add_argument('--scenario')
    parser.add_argument('--full-suite', action='store_true')
    parser.add_argument('--run')
    return parser.parse_args(argv)


def main(argv=None):
    args = parse_args(argv)
    repo = Path(__file__).resolve().parents[1]
    if args.command in {'sim-test', 'all'} and args.backend == 'wokwi' and not (args.scenario or args.full_suite):
        raise ValueError('Wokwi requires --backend wokwi and --scenario NAME or --full-suite')
    if args.command == 'sim-update-goldens':
        if not args.run or not args.scenario:
            raise ValueError('promotion requires --run RUN_ID and --scenario NAME')
        if args.backend != 'velxio':
            raise ValueError('recorded Wokwi promotion is not supported; use local recorded captures')
        promote(repo, args.run, args.scenario)
        return 0
    if args.command == 'setup':
        for image, dockerfile in [(TOOLCHAIN, '.devcontainer/Dockerfile'), (RUNTIME, '.devcontainer/Velxio.Dockerfile')]:
            context = repo / '.devcontainer' if image == RUNTIME else repo
            execute(['docker', 'build', '--tag', image, '--file', str(repo / dockerfile), str(context)])
        write_json(repo / '.tools/velxio/runtime.json', cache_identity(repo))
        doctor(repo)
        return 0
    if args.command == 'doctor':
        doctor(repo)
    if args.command in {'test', 'all'}:
        verify_cache(repo)
        execute(workspace_command(repo, TOOLCHAIN, ['bash', 'tools/run-host-tests.sh']))
        execute(workspace_command(repo, RUNTIME, ['python', '-m', 'unittest', 'discover', '-s', 'tests/tools', '-p', 'test_*.py', '-v']))
    if args.command in {'firmware-build', 'all'}:
        build(repo, 'production')
    if args.command == 'sim-build':
        build(repo, 'velxio' if args.backend == 'velxio' else 'simulation')
    if args.command in {'sim-test', 'all'}:
        scenarios = select_scenarios(repo, args.scenario, args.backend)
        if args.backend == 'velxio':
            status = local_test(repo, scenarios)
        else:
            if not os.environ.get('WOKWI_CLI_TOKEN'):
                raise ValueError('WOKWI_CLI_TOKEN is required for explicit Wokwi execution')
            print('Explicit Wokwi comparison scenarios: ' + ', '.join(item['name'] for item in scenarios), flush=True)
            build(repo, 'simulation')
            for scenario in scenarios:
                execute(workspace_command(repo, TOOLCHAIN, ['python3', 'tools/run_wokwi.py', 'test', '--scenario', scenario['name']], online=True))
            status = 0
        if args.command == 'all':
            execute(workspace_command(repo, TOOLCHAIN, ['bash', 'tools/check-isolation.sh', '--mode', 'velxio' if args.backend == 'velxio' else 'simulation']))
        return status
    return 0


if __name__ == '__main__':
    try:
        raise SystemExit(main())
    except (ValueError, OSError, subprocess.SubprocessError) as error:
        print(f'Bench failed: {error}', file=sys.stderr)
        raise SystemExit(1)
