import json
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools'))
import bench_cli


class BenchCliTests(unittest.TestCase):
    def test_defaults_local(self):
        self.assertEqual(bench_cli.parse_args(['sim-test']).backend, 'velxio')

    def test_cloud_requires_selection_before_docker(self):
        with patch.object(bench_cli, 'execute') as execute:
            with self.assertRaises(ValueError):
                bench_cli.main(['sim-test', '--backend', 'wokwi'])
            execute.assert_not_called()

    def test_local_container_offline_and_no_repository_mount(self):
        args = bench_cli.runtime_command(Path('/repo'), Path('/inputs'), Path('/output'), 'sha256:pinned')
        self.assertIn('none', args)
        self.assertIn(f'{Path("/repo") / "tools/velxio"}:/runner:ro', args)
        self.assertNotIn('/repo:/workspace', args)
        self.assertEqual(args[-2:], ['sha256:pinned', '/runner/run.py'])

    def test_promotion_requires_run_and_selection(self):
        with self.assertRaises(ValueError):
            bench_cli.main(['sim-update-goldens'])

    def test_unsupported_local_selection_fails(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'simulation/velxio').mkdir(parents=True)
            (root / 'simulation/scenario-manifest.json').write_text(json.dumps({'schema': 1, 'scenarios': [{'name': 'wifi'}]}))
            (root / 'simulation/velxio/scenarios.json').write_text(json.dumps({'supported': []}))
            with self.assertRaisesRegex(ValueError, 'unsupported'):
                bench_cli.select_scenarios(root, 'wifi', 'velxio')

    def test_sim_build_defaults_to_dio_without_cloud(self):
        with patch.object(bench_cli, 'build') as build:
            bench_cli.main(['sim-build'])
            self.assertEqual(build.call_args.args[1], 'velxio')

    def test_sim_build_explicit_cloud_is_separate(self):
        with patch.object(bench_cli, 'build') as build:
            bench_cli.main(['sim-build', '--backend', 'wokwi'])
            self.assertEqual(build.call_args.args[1], 'simulation')

    def test_velxio_build_uses_verified_image_id(self):
        with patch.object(bench_cli, 'verify_cache', return_value={'toolchainImageId': 'sha256:verified'}), patch.object(bench_cli, 'execute') as execute:
            bench_cli.build(Path('/repo'), 'velxio')
            self.assertIn('sha256:verified', execute.call_args.args[0])
            self.assertNotIn(bench_cli.TOOLCHAIN, execute.call_args.args[0])

    def test_golden_destination_rejects_directory_and_file_links(self):
        with tempfile.TemporaryDirectory() as directory:
            repo = Path(directory)
            golden = repo / 'simulation/goldens/boot'
            golden.mkdir(parents=True)
            outside = repo / 'outside'
            outside.mkdir()
            try:
                (golden / 'frame.png').symlink_to(outside / 'frame.png')
            except OSError:
                self.skipTest('host cannot create symlinks')
            with self.assertRaises(ValueError):
                bench_cli.validate_golden_destinations(repo, 'boot', ['frame'])
            (golden / 'frame.png').unlink()
            golden.rmdir()
            golden.symlink_to(outside, target_is_directory=True)
            with self.assertRaises(ValueError):
                bench_cli.validate_golden_destinations(repo, 'boot', ['frame'])

    def test_promotion_validates_all_before_copying_and_never_executes(self):
        with tempfile.TemporaryDirectory() as directory:
            repo = Path(directory)
            output = repo / 'build/velxio/results/run/boot'
            output.mkdir(parents=True)
            scenario = {'name': 'boot', 'file': 'boot.yaml', 'screenshots': ['first', 'second']}
            (repo / 'simulation').mkdir()
            (repo / 'simulation/boot.yaml').write_text('scenario')
            (output / 'result.json').write_text('{"executionPassed":true}')
            for name in scenario['screenshots']:
                (output / f'{name}.png').write_bytes(b'capture')
            record = {'complete': True, 'runId': 'run', 'scenario': scenario,
                      'attestation': {'verified': True}, 'runtimeIdentity': {'id': 1},
                      'runtimeImageId': 'image',
                      'scenarioSha256': bench_cli.sim_artifacts.sha256(repo / 'simulation/boot.yaml'),
                      'resultSha256': bench_cli.sim_artifacts.sha256(output / 'result.json'),
                      'captures': {name: bench_cli.sim_artifacts.sha256(output / f'{name}.png') for name in scenario['screenshots']}}
            bench_cli.write_json(output / 'run.json', record)
            with patch.object(bench_cli, 'select_scenarios', return_value=[scenario]), patch.object(bench_cli, 'verify_cache', return_value={'runtimeImageId': 'image'}), patch.object(bench_cli.sim_artifacts, 'verify_attestation', return_value={'verified': True}), patch.object(bench_cli.sim_artifacts, 'runtime_identity', return_value={'id': 1}), patch.object(bench_cli, 'execute') as execute:
                (output / 'second.png').write_bytes(b'tampered')
                with self.assertRaisesRegex(ValueError, 'tampered'):
                    bench_cli.promote(repo, 'run', 'boot')
                self.assertFalse((repo / 'simulation/goldens').exists())
                (output / 'second.png').write_bytes(b'capture')
                bench_cli.promote(repo, 'run', 'boot')
                self.assertEqual((repo / 'simulation/goldens/boot/first.png').read_bytes(), b'capture')
                execute.assert_not_called()

    def test_runtime_cache_rejects_modified_image_or_dockerfile(self):
        with tempfile.TemporaryDirectory() as directory:
            repo = Path(directory)
            bench_cli.write_json(repo / '.tools/velxio/runtime.json', {'runtimeImageId': 'old'})
            with patch.object(bench_cli, 'cache_identity', return_value={'runtimeImageId': 'new'}):
                with self.assertRaisesRegex(ValueError, 'stale'):
                    bench_cli.verify_cache(repo)

    def test_cloud_excludes_local_only_scenarios(self):
        with tempfile.TemporaryDirectory() as directory:
            repo = Path(directory)
            (repo / 'simulation').mkdir()
            (repo / 'simulation/common.yaml').write_text('')
            bench_cli.write_json(repo / 'simulation/scenario-manifest.json', {'scenarios': [
                {'name': 'common', 'file': 'common.yaml', 'screenshots': []},
                {'name': 'reboot', 'file': 'reboot.yaml', 'screenshots': [], 'backends': ['velxio']}]})
            self.assertEqual([s['name'] for s in bench_cli.select_scenarios(repo, None, 'wokwi')], ['common'])
            with self.assertRaises(ValueError):
                bench_cli.select_scenarios(repo, 'reboot', 'wokwi')

    def test_backend_file_override_preserves_cloud_and_checks_path(self):
        with tempfile.TemporaryDirectory() as directory:
            repo = Path(directory)
            (repo / 'simulation/velxio').mkdir(parents=True)
            (repo / 'simulation/common.yaml').write_text('cloud')
            (repo / 'simulation/velxio/common.yaml').write_text('local')
            scenario = {'name': 'common', 'file': 'common.yaml', 'screenshots': [],
                        'backendFiles': {'velxio': 'velxio/common.yaml'}}
            manifest = {'scenarios': [scenario]}
            bench_cli.write_json(repo / 'simulation/velxio/scenarios.json', {'supported': ['common']})
            bench_cli.write_json(repo / 'simulation/scenario-manifest.json', manifest)
            with patch.object(bench_cli, 'read_json', side_effect=lambda path: manifest if path.name == 'scenario-manifest.json' else {'supported': ['common']}):
                local = bench_cli.select_scenarios(repo, 'common', 'velxio')[0]
                self.assertEqual(local['file'], 'velxio/common.yaml')
                self.assertEqual(local['backendFiles'], scenario['backendFiles'])
                self.assertEqual(scenario['file'], 'common.yaml')
                self.assertEqual(bench_cli.select_scenarios(repo, 'common', 'wokwi')[0]['file'], 'common.yaml')
                (repo / 'outside.yaml').write_text('outside')
                scenario['backendFiles']['velxio'] = '../outside.yaml'
                with self.assertRaisesRegex(ValueError, 'unsafe'):
                    bench_cli.select_scenarios(repo, 'common', 'velxio')


if __name__ == '__main__':
    unittest.main()
