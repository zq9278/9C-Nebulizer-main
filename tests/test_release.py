"""Release safety tests. No network, credentials or hardware required."""
import io
import json
from pathlib import Path
import sys
import tarfile
import tempfile
import unittest
from unittest.mock import patch, Mock

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import release


class ReleaseTests(unittest.TestCase):
    def test_version_conflicts_and_component_isolation(self):
        old = [{'name': release.PREFIX + 'V0.0.2.20261016'},
               {'name': 'SND100-AT32app-V9.0.0.20261016'}]
        release.validate_version('V0.0.3.20261016', old)
        for value in ('V0.0.2.20261016', 'V0.0.1.20261017', '../bad', 'V0.0.3.20261301'):
            with self.subTest(value=value), self.assertRaises(ValueError):
                release.validate_version(value, old)

    def test_existing_output_never_overwritten(self):
        with tempfile.TemporaryDirectory() as directory:
            with self.assertRaises(FileExistsError):
                release.fresh_output(directory)

    def test_server_directory_cannot_be_output(self):
        with self.assertRaisesRegex(ValueError, 'read-only'):
            release.fresh_output(release.READ_ONLY_SERVER / 'forbidden-release')

    def test_failed_build_stops_before_artifact_checks(self):
        with tempfile.TemporaryDirectory() as directory:
            with patch('release.run', side_effect=RuntimeError('compile failed')), \
                 patch('release.Firmware.load') as load, self.assertRaises(RuntimeError):
                release.build('upgrade-release', Path(directory))
            load.assert_not_called()

    def make_artifact(self, path, data, name='nebulizer.nfu'):
        raw = io.BytesIO()
        with tarfile.open(fileobj=raw, mode='w:gz') as tar:
            entry = tarfile.TarInfo(name)
            entry.size = len(data)
            tar.addfile(entry, io.BytesIO(data))
        content = raw.getvalue()
        with tarfile.open(path, mode='w') as tar:
            entry = tarfile.TarInfo('data/0000.tar.gz')
            entry.size = len(content)
            tar.addfile(entry, io.BytesIO(content))

    def test_payload_must_be_exact_app_nfu(self):
        with tempfile.TemporaryDirectory() as directory:
            folder = Path(directory)
            nfu, artifact = folder / 'nebulizer.nfu', folder / 'test.mender'
            nfu.write_bytes(b'expected')
            self.make_artifact(artifact, b'expected')
            release.inspect_payload(artifact, nfu)
            for data, name in ((b'wrong', 'nebulizer.nfu'), (b'expected', '../nebulizer.nfu'),
                               (b'expected', 'nebulizer_factory.hex')):
                self.make_artifact(artifact, data, name)
                with self.assertRaises(ValueError):
                    release.inspect_payload(artifact, nfu)

    def test_uploaded_identity_and_hash_checked(self):
        with tempfile.TemporaryDirectory() as directory:
            nfu = Path(directory) / 'nebulizer.nfu'
            nfu.write_bytes(b'app')
            item = {'name': 'test', 'device_types_compatible': [release.DEVICE_TYPE],
                    'updates': [{'type_info': {'type': release.MODULE},
                                 'files': [{'name': 'nebulizer.nfu', 'checksum': release.digest(nfu)}]}]}
            release.check_uploaded(item, 'test', nfu)
            item['updates'][0]['files'][0]['checksum'] = 'wrong'
            with self.assertRaises(ValueError):
                release.check_uploaded(item, 'test', nfu)

    def test_upload_only_releases_not_deployments(self):
        api = release.MenderAPI.__new__(release.MenderAPI)
        api.request = Mock(return_value=(201, {}, None))
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'test.mender'
            path.write_bytes(b'signed payload')
            api.upload(path)
        args = api.request.call_args.args
        self.assertEqual(args[:2], ('/api/management/v1/deployments/artifacts', 'POST'))
        self.assertIn(b'name="artifact"', args[2])
        self.assertNotIn('deployments/deployments', args[0])

    def test_wrong_public_key_blocks_upload(self):
        with tempfile.TemporaryDirectory() as directory:
            folder = Path(directory)
            key, public = folder / 'key.pem', folder / 'pub.pem'
            key.write_text('not printed')
            public.write_text('public')
            args = Mock(version='V0.0.3.20261016', key=key, public_key=public,
                        upload=True, output=folder / 'delivery', preset='upgrade-release')
            api, tool = Mock(), Mock()
            api.artifacts.return_value = []
            tool.call.side_effect = ['', RuntimeError('signature verification failed')]
            with patch('release.ArtifactTool', return_value=tool), \
                 patch('release.MenderAPI', return_value=api), \
                 patch('release.build', return_value=folder), patch('release.shutil.copy2'), \
                 self.assertRaises(RuntimeError):
                release.stm32_release(args)
            api.upload.assert_not_called()

    def test_repeated_pagination_is_rejected(self):
        api = release.MenderAPI.__new__(release.MenderAPI)
        api.request = Mock(return_value=(200, {}, [{'id': str(i)} for i in range(100)]))
        with self.assertRaisesRegex(ValueError, 'repeated'):
            api.artifacts()

    def test_chrome_transport_does_not_follow_redirects(self):
        api = release.MenderAPI.__new__(release.MenderAPI)
        api.url = 'https://example.test:18443'
        api.http_client = 'chrome'
        api.http = Mock()
        api.http.request.return_value = Mock(status_code=200, headers={}, content=b'ok')
        self.assertEqual(api._request('/api/test')[2], b'ok')
        self.assertFalse(api.http.request.call_args.kwargs['allow_redirects'])
        api.http.request.return_value.status_code = 302
        with self.assertRaisesRegex(RuntimeError, 'HTTP 302'):
            api._request('/api/test')

    def test_chrome_transport_error_does_not_echo_credentials(self):
        api = release.MenderAPI.__new__(release.MenderAPI)
        api.url = 'https://example.test:18443'
        api.http_client = 'chrome'
        api.http = Mock()
        api.http.request.side_effect = RuntimeError('sensitive request data')
        with self.assertRaises(RuntimeError) as caught:
            api._request('/api/test')
        self.assertNotIn('sensitive request data', str(caught.exception))

    def test_management_tls_verification_defaults_off_and_can_be_restored(self):
        with tempfile.TemporaryDirectory() as directory:
            credentials = Path(directory) / 'credentials.json'
            credentials.write_text(json.dumps({'username': 'test', 'password': 'test'}))
            with patch.object(release.MenderAPI, '_request', return_value=(200, {}, b'test-jwt')):
                api = release.MenderAPI('https://example.test', 'missing-ca', credentials)
                self.assertFalse(api.context.check_hostname)
                self.assertEqual(api.context.verify_mode, release.ssl.CERT_NONE)
                with patch('release.ssl.create_default_context') as verified:
                    release.MenderAPI('https://example.test', 'existing-ca', credentials, verify_tls=True)
                    verified.assert_called_once_with(cafile='existing-ca')


if __name__ == '__main__':
    unittest.main(verbosity=2)
