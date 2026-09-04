import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools'))
import sim_artifacts


class VelxioArtifactsTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.repo = Path(self.temporary.name)
        for folder in ('simulation/velxio', 'tools/velxio', '.tools/velxio', '.devcontainer', 'VictronCYD_Modbus', 'build/velxio', 'build/simulation'):
            (self.repo / folder).mkdir(parents=True)
        (self.repo / 'simulation/source-allowlist.txt').write_text('VictronCYD_Modbus/App.ino\n')
        (self.repo / 'VictronCYD_Modbus/App.ino').write_text('// dummy\n')
        (self.repo / 'simulation/velxio/runtime-lock.json').write_text(json.dumps({'revision': 'a' * 40, 'image': 'sha256:' + 'b' * 64}))
        (self.repo / 'tools/velxio/worker.mts').write_text('// maintained adapter\n')
        (self.repo / 'tools/arduino-build.sh').write_text('# pinned build script\n')
        (self.repo / '.devcontainer/Dockerfile').write_text('# pinned compiler and libraries\n')
        (self.repo / '.tools/velxio/runtime.json').write_text(json.dumps({'toolchainImageId': 'sha256:' + 'c' * 64}))
        for mode in ('simulation', 'velxio'):
            for ext in ('bin', 'elf'):
                (self.repo / f'build/{mode}/firmware.{ext}').write_bytes(mode.encode())
        (self.repo / 'build/velxio/firmware.bin').write_bytes(b'\xff' * 0x1000 + b'\xe9\x03\x02\x2f')

    def test_non_dio_or_invalid_merged_image_cannot_be_attested(self):
        for header in (b'\xe9\x03\x00\x2f', b'\x00\x03\x02\x2f', b''):
            sim_artifacts.stage_sources(self.repo, 'velxio')
            (self.repo / 'build/velxio/firmware.bin').write_bytes(b'\xff' * 0x1000 + header)
            with self.assertRaisesRegex(ValueError, 'DIO'):
                sim_artifacts.create_attestation(self.repo, mode='velxio')

    def test_build_configuration_edits_invalidate_attestation(self):
        for relative in ('tools/arduino-build.sh', '.devcontainer/Dockerfile'):
            self.attest()
            path = self.repo / relative
            path.write_text(path.read_text() + '# drift\n')
            with self.assertRaisesRegex(ValueError, 'build configuration'):
                sim_artifacts.verify_attestation(self.repo, mode='velxio')

    def test_build_configuration_edit_during_build_rejected(self):
        sim_artifacts.stage_sources(self.repo, 'velxio')
        (self.repo / 'tools/arduino-build.sh').write_text('# no longer DIO')
        with self.assertRaisesRegex(ValueError, 'build configuration'):
            sim_artifacts.create_attestation(self.repo, mode='velxio')

    def test_replaced_toolchain_image_invalidates_artifacts(self):
        self.attest()
        (self.repo / '.tools/velxio/runtime.json').write_text(json.dumps({'toolchainImageId': 'sha256:' + 'd' * 64}))
        with self.assertRaisesRegex(ValueError, 'build configuration'):
            sim_artifacts.verify_attestation(self.repo, mode='velxio')

    def test_missing_or_unpinned_toolchain_requires_setup(self):
        cache = self.repo / '.tools/velxio/runtime.json'
        cache.unlink()
        with self.assertRaisesRegex(ValueError, 'setup'):
            sim_artifacts.stage_sources(self.repo, 'velxio')
        for image in ('latest', 'sha256:123', '', None):
            cache.write_text(json.dumps({'toolchainImageId': image}))
            with self.assertRaisesRegex(ValueError, 'setup'):
                sim_artifacts.stage_sources(self.repo, 'velxio')

    def attest(self):
        sim_artifacts.stage_sources(self.repo, 'velxio')
        return sim_artifacts.create_attestation(self.repo, mode='velxio')

    def test_separate_dio_identity_and_outputs(self):
        document = self.attest()
        self.assertEqual(document['backend'], 'velxio')
        self.assertEqual(document['fqbn'], 'esp32:esp32:esp32:FlashMode=dio')
        self.assertEqual(document['runtimeIdentity']['lock']['revision'], 'a' * 40)
        self.assertEqual(document['artifacts'][0]['path'], 'build/velxio/firmware.bin')
        self.assertFalse((self.repo / 'build/staging/velxio/VictronCYD_Modbus/secrets.h').exists())
        sim_artifacts.verify_attestation(self.repo, mode='velxio')

    def test_runtime_and_adapter_edits_invalidate_attestation(self):
        for relative in ('simulation/velxio/runtime-lock.json', 'tools/velxio/worker.mts'):
            self.attest()
            path = self.repo / relative
            path.write_text(path.read_text() + ' ')
            with self.assertRaisesRegex(ValueError, 'runtime|adapter'):
                sim_artifacts.verify_attestation(self.repo, mode='velxio')

    def test_runtime_edit_during_build_rejected(self):
        sim_artifacts.stage_sources(self.repo, 'velxio')
        (self.repo / 'tools/velxio/worker.mts').write_text('// changed')
        with self.assertRaisesRegex(ValueError, 'runtime|adapter'):
            sim_artifacts.create_attestation(self.repo, mode='velxio')

    def test_cross_backend_document_rejected(self):
        self.attest()
        sim_artifacts.stage_sources(self.repo, 'simulation')
        legacy = sim_artifacts.create_attestation(self.repo)
        (self.repo / 'build/velxio/attestation.json').write_text(json.dumps(legacy))
        with self.assertRaises(ValueError):
            sim_artifacts.verify_attestation(self.repo, mode='velxio')

    def test_cache_files_do_not_change_identity(self):
        self.attest()
        cache = self.repo / 'tools/velxio/__pycache__'
        cache.mkdir()
        (cache / 'ignored.py').write_text('ignored')
        sim_artifacts.verify_attestation(self.repo, mode='velxio')

    def test_default_wokwi_api_remains_independent_of_runtime(self):
        sim_artifacts.stage_sources(self.repo, 'simulation')
        document = sim_artifacts.create_attestation(self.repo)
        self.assertEqual(document['artifacts'][0]['path'], 'build/simulation/firmware.bin')
        (self.repo / 'simulation/velxio/runtime-lock.json').unlink()
        sim_artifacts.verify_attestation(self.repo)

    def test_velxio_cannot_attest_missing_own_output(self):
        sim_artifacts.stage_sources(self.repo, 'velxio')
        (self.repo / 'build/velxio/firmware.bin').unlink()
        with self.assertRaisesRegex(ValueError, 'missing or unsafe'):
            sim_artifacts.create_attestation(self.repo, mode='velxio')


if __name__ == '__main__':
    unittest.main()
