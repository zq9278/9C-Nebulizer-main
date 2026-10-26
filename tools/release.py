"""Build checked factory deliveries or signed STM32 Mender Releases. Never deploys."""
import argparse
import base64
from datetime import datetime, timezone
import hashlib
import io
import json
import os
from pathlib import Path
import re
import shutil
import ssl
import subprocess
import sys
import tarfile
import urllib.error
import urllib.request
import uuid
import zipfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from fw_update.protocol import Firmware
from device_io.factory import FactoryImage

DEVICE_TYPE = 'SLK-Nebulizer-t113-pi-V1.0'
MODULE = 'slk-nebulizer-stm32app'
PREFIX = 'SLK-Nebulizer-stm32app-'
DEPENDENCY = {'slk_nebulizer_stm32_protocol': '1'}
VERSION = re.compile(r'^V(\d+)\.(\d+)\.(\d+)\.(\d{8})$')
READ_ONLY_SERVER = Path('D:/Technology_stack/Web/mender' if os.name == 'nt'
                        else '/mnt/d/Technology_stack/Web/mender').resolve()


def run(command, log=None):
    result = subprocess.run([str(x) for x in command], cwd=ROOT,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
                            encoding='utf-8', errors='replace')
    if log:
        Path(log).write_text(result.stdout, encoding='utf-8')
    if result.returncode:
        raise RuntimeError(f'{Path(str(command[0])).name} failed ({result.returncode}); '
                           f'see {log}' if log else result.stdout)
    return result.stdout


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def fresh_output(path):
    path = Path(path).resolve()
    if path == READ_ONLY_SERVER or READ_ONLY_SERVER in path.parents:
        raise ValueError('Mender server directory is read-only; choose a product output directory')
    path.mkdir(parents=True, exist_ok=False)  # Never overwrite a previous delivery.
    return path


def build(preset, output):
    run(['cmake', '--preset', preset], output / 'configure.log')
    run(['cmake', '--build', '--preset', preset], output / 'build.log')
    folder = ROOT / 'build' / preset
    cache = (folder / 'CMakeCache.txt').read_text(encoding='utf-8')
    nm = re.search(r'^CMAKE_NM:FILEPATH=(.+)$', cache, re.M)
    command = [sys.executable, ROOT / 'tools/verify_firmware.py', folder / 'nebulizer.elf']
    if nm:
        command.extend(['--nm', nm[1].strip()])
    run(command, output / 'firmware-check.log')
    firmware = Firmware.load(folder / 'nebulizer.nfu')
    factory = FactoryImage(folder / 'nebulizer_factory.hex')
    if factory.app_length != len(firmware.data):
        raise ValueError('Factory and NFU lengths differ')
    if factory.data[0x4000:0x4000 + len(firmware.data)] != firmware.data:
        raise ValueError('Factory and NFU contents differ')
    # Verify both ELF Flash load sections, including RAM .data initializers.
    from elftools.elf.elffile import ELFFile
    for name in ('nebulizer', 'nebulizer_boot'):
        with (folder / (name + '.elf')).open('rb') as stream:
            elf = ELFFile(stream)
            for segment in elf.iter_segments():
                if segment['p_type'] != 'PT_LOAD' or not segment['p_filesz']:
                    continue
                for section in elf.iter_sections():
                    if (not section['sh_flags'] & 2 or section['sh_type'] == 'SHT_NOBITS'
                            or not section['sh_size'] or not segment.section_in_segment(section)):
                        continue
                    address = segment['p_paddr'] + section['sh_addr'] - segment['p_vaddr']
                    if 0x08000000 <= address < 0x08020000:
                        offset = address - 0x08000000
                        if factory.data[offset:offset + section['sh_size']] != section.data():
                            raise ValueError(f'{name} ELF differs from factory Flash at {address:#x}')
    return folder


def manifest(output, files, **extra):
    info = dict(created_utc=datetime.now(timezone.utc).isoformat(),
                source_commit=run(['git', 'rev-parse', 'HEAD']).strip(),
                source_changes=run(['git', 'status', '--porcelain']).splitlines(),
                hardware_test='not performed', deployment_created=False,
                files={p.name: {'bytes': p.stat().st_size, 'sha256': digest(p)} for p in files},
                **extra)
    (output / 'release-record.json').write_text(json.dumps(info, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    (output / 'SHA256SUMS').write_text(''.join(f'{digest(p)}  {p.name}\n' for p in files), encoding='utf-8')
    return info


def factory_release(args):
    output = fresh_output(args.output or ROOT / 'build/deliveries' / ('factory-' + args.preset + '-' + datetime.now().strftime('%Y%m%d-%H%M%S')))
    folder = build(args.preset, output)
    files = []
    for name in ('nebulizer_factory.hex', 'nebulizer_factory.bin', 'nebulizer.nfu',
                 'nebulizer.elf', 'nebulizer.bin', 'nebulizer.hex', 'nebulizer.map',
                 'nebulizer_boot.elf', 'nebulizer_boot.bin', 'nebulizer_boot.hex', 'nebulizer_boot.map'):
        target = output / name
        shutil.copy2(folder / name, target)
        files.append(target)
    manifest(output, files, preset=args.preset, kind='factory', uploaded=False)
    archive = output / 'factory-delivery.zip'
    with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED) as z:
        for path in files + [output / 'release-record.json', output / 'SHA256SUMS']:
            z.write(path, path.name)
    print(f'Factory HEX, matching ELF, NFU and ZIP: {output}')


def linux_path(path):
    value = str(Path(path).resolve())
    if os.name == 'nt':
        return '/mnt/' + value[0].lower() + value[2:].replace('\\', '/')
    return value


class ArtifactTool:
    def __init__(self, path, distro):
        self.path, self.distro = Path(path).resolve(), distro
        if not self.path.is_file():
            raise ValueError('Linux mender-artifact tool not found; specify --artifact-tool')

    def call(self, arguments, log):
        command = [self.path, *arguments]
        if os.name == 'nt':
            command = ['wsl.exe', '-d', self.distro, '--', linux_path(self.path), *arguments]
        return run(command, log)


class MenderAPI:
    def __init__(self, url, ca, credentials, http_client='urllib', verify_tls=False):
        if not url.startswith('https://'):
            raise ValueError('Mender management URL must use HTTPS')
        self.url = url.rstrip('/')
        self.verify_tls = bool(verify_tls)
        self.context = (ssl.create_default_context(cafile=str(ca)) if self.verify_tls
                        else ssl._create_unverified_context())
        self.http_client = http_client
        if http_client == 'chrome':
            try:
                from curl_cffi import requests
            except ModuleNotFoundError:
                # Optional project-local dependency, outside server/source directories.
                sys.path.insert(0, str(ROOT / 'build/api-http-libs'))
                try:
                    from curl_cffi import requests
                except ModuleNotFoundError:
                    raise ValueError('Install optional API client: python -m pip install '
                                     '--target build/api-http-libs -r tools/requirements-release.txt') from None
            self.http = requests.Session(impersonate='chrome',
                                         verify=str(Path(ca).resolve()) if self.verify_tls else False)
        c = json.loads(Path(credentials).read_text(encoding='utf-8-sig'))
        auth = base64.b64encode((c['username'] + ':' + c['password']).encode()).decode()
        # Never print or persist credentials, authentication headers or JWT.
        self.jwt = self._request('/api/management/v1/useradm/auth/login', 'POST', b'',
                                 {'Authorization': 'Basic ' + auth})[2].decode()

    def _request(self, path, method='GET', data=None, headers=None):
        if self.http_client == 'chrome':
            try:
                response = self.http.request(method, self.url + path, data=data,
                                             headers=headers or {}, timeout=180,
                                             allow_redirects=False)
            except Exception as error:
                # Do not expose credentials, JWT or request bodies in diagnostics.
                raise RuntimeError(f'Mender {method} {path}: HTTPS transport failed '
                                   f'({type(error).__name__})') from None
            if not 200 <= response.status_code < 300:
                raise RuntimeError(f'Mender {method} {path}: HTTP {response.status_code}')
            return response.status_code, dict(response.headers), response.content
        req = urllib.request.Request(self.url + path, data=data, headers=headers or {}, method=method)
        try:
            with urllib.request.urlopen(req, context=self.context, timeout=180) as response:
                return response.status, dict(response.headers), response.read()
        except urllib.error.HTTPError as error:
            # Server response bodies can include confidential data; only report status/path.
            raise RuntimeError(f'Mender {method} {path}: HTTP {error.code}') from None

    def request(self, path, method='GET', data=None, content_type=None):
        headers = {'Authorization': 'Bearer ' + self.jwt}
        if content_type:
            headers['Content-Type'] = content_type
        status, response_headers, blob = self._request(path, method, data, headers)
        return status, response_headers, json.loads(blob) if blob else None

    def artifacts(self):
        items, seen = [], set()
        for page in range(1, 101):
            _, _, batch = self.request(f'/api/management/v1/deployments/artifacts?per_page=100&page={page}')
            if not isinstance(batch, list):
                raise ValueError('Unexpected Artifact list response')
            for item in batch:
                if item['id'] in seen:
                    raise ValueError('Server repeated a pagination result; aborting')
                seen.add(item['id'])
                items.append(item)
            if len(batch) < 100:
                return items
        raise ValueError('Artifact listing exceeded 100 pages; aborting')

    def upload(self, path):
        boundary = 'nebulizer-' + uuid.uuid4().hex
        body = (f'--{boundary}\r\nContent-Disposition: form-data; name="artifact"; filename="{path.name}"\r\n'
                'Content-Type: application/octet-stream\r\n\r\n').encode()
        body += path.read_bytes() + f'\r\n--{boundary}--\r\n'.encode()
        return self.request('/api/management/v1/deployments/artifacts', 'POST', body,
                            'multipart/form-data; boundary=' + boundary)


def validate_version(version, existing):
    match = VERSION.fullmatch(version)
    if not match:
        raise ValueError('Version must be Vmajor.minor.patch.YYYYMMDD')
    datetime.strptime(match[4], '%Y%m%d')
    if any(item.get('name') == PREFIX + version for item in existing):
        raise ValueError('Artifact name already exists; increment the version, never overwrite')
    target = tuple(map(int, match.groups()[:3]))
    for item in existing:
        name = item.get('name', '')
        old = VERSION.fullmatch(name[len(PREFIX):]) if name.startswith(PREFIX) else None
        if old and tuple(map(int, old.groups()[:3])) >= target:
            raise ValueError('New semantic version must exceed all published standard STM32 versions')


def inspect_payload(path, expected):
    with tarfile.open(path) as archive:
        members = [m for m in archive.getmembers() if re.fullmatch(r'data/\d+\.tar(?:\.gz)?', m.name)]
        if len(members) != 1:
            raise ValueError('Expected exactly one Mender payload')
        with tarfile.open(fileobj=io.BytesIO(archive.extractfile(members[0]).read()), mode='r:*') as data:
            files = [m for m in data.getmembers() if m.isfile()]
            if len(files) != 1 or files[0].name != 'nebulizer.nfu':
                raise ValueError('Artifact payload must contain only nebulizer.nfu')
            if data.extractfile(files[0]).read() != expected.read_bytes():
                raise ValueError('Embedded NFU differs from build')


def check_uploaded(item, name, nfu):
    if item.get('name') != name or item.get('device_types_compatible') != [DEVICE_TYPE]:
        raise ValueError('Uploaded Artifact identity/device type differs')
    updates = item.get('updates', [])
    if len(updates) != 1 or updates[0].get('type_info', {}).get('type') != MODULE:
        raise ValueError('Uploaded update module differs')
    files = updates[0].get('files', [])
    if len(files) != 1 or files[0].get('name') != 'nebulizer.nfu' or files[0].get('checksum') != digest(nfu):
        raise ValueError('Uploaded NFU hash differs')


def stm32_release(args):
    validate_version(args.version, [])
    key, public = Path(args.key).resolve(), Path(args.public_key).resolve()
    if not key.is_file() or not public.is_file():
        raise ValueError('Existing signing key and trusted product public key are required')
    tool = ArtifactTool(args.artifact_tool, args.wsl_distro)
    api = MenderAPI(args.server, args.ca, args.credentials, args.http_client,
                    args.verify_tls) if args.upload else None
    existing = api.artifacts() if api else []
    validate_version(args.version, existing)
    output = fresh_output(args.output or ROOT / 'build/deliveries' / (PREFIX + args.version))
    if api:
        filtered = [a for a in existing if a.get('name', '').startswith(PREFIX)]
        (output / 'server-before.json').write_text(json.dumps(filtered, indent=2) + '\n', encoding='utf-8')
    folder = build(args.preset, output)
    nfu = output / 'nebulizer.nfu'
    shutil.copy2(folder / nfu.name, nfu)
    shutil.copy2(folder / 'nebulizer.elf', output / 'nebulizer.elf')
    package = output / (PREFIX + args.version + '.mender')
    tool.call(['write', 'module-image', '-n', PREFIX + args.version, '-t', DEVICE_TYPE, '-T', MODULE,
               '-d', 'slk_nebulizer_stm32_protocol:1', '-f', linux_path(nfu),
               '--software-name', 'stm32app', '--software-version', args.version,
               '-k', linux_path(key), '-o', linux_path(package)], output / 'artifact-build.log')
    result = tool.call(['read', '-k', linux_path(public), linux_path(package)], output / 'signature-check.log')
    if 'signed and verified correctly' not in result:
        raise ValueError('Tool did not confirm a signed, verified Artifact')
    inspect_payload(package, nfu)
    info = manifest(output, [package, nfu, output / 'nebulizer.elf'], kind='stm32app',
                    preset=args.preset, artifact_name=PREFIX + args.version, module=MODULE,
                    device_type=DEVICE_TYPE, dependency=DEPENDENCY, uploaded=False,
                    target_server=args.server if args.upload else None, http_client=args.http_client,
                    https_certificate_verification=args.verify_tls,
                    verification='signature and exact embedded NFU passed',
                    verification_public_key_sha256=digest(public))
    if api:
        # Recheck immediately before POST to avoid ordinary concurrent name conflicts.
        validate_version(args.version, api.artifacts())
        status, headers, _ = api.upload(package)
        # Save receipt before readback: a failed GET must not hide a completed upload.
        receipt = {'status': status, 'location': headers.get('Location') or headers.get('location'),
                   'artifact_name': PREFIX + args.version, 'readback_verified': False,
                   'deployment_created': False}
        receipt_path = output / 'server-upload.json'
        receipt_path.write_text(json.dumps(receipt, indent=2) + '\n', encoding='utf-8')
        info['uploaded'] = True
        (output / 'release-record.json').write_text(json.dumps(info, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
        matches = [a for a in api.artifacts() if a.get('name') == PREFIX + args.version]
        if len(matches) != 1:
            raise ValueError('Upload returned but readback could not uniquely locate Artifact; inspect receipt')
        check_uploaded(matches[0], PREFIX + args.version, nfu)
        receipt.update(artifact_id=matches[0]['id'], readback_verified=True)
        receipt_path.write_text(json.dumps(receipt, indent=2) + '\n', encoding='utf-8')
        print(f'Uploaded and verified Artifact: {matches[0]["id"]}; no deployment created')
    print(f'Signed STM32 delivery: {output}')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='command', required=True)
    factory = commands.add_parser('factory', help='Build Boot + App factory HEX and matching delivery ZIP')
    stm32 = commands.add_parser('stm32', help='Build and sign App-only Mender Artifact, optionally upload')
    for command in (factory, stm32):
        command.add_argument('--preset', choices=['upgrade-release', 'upgrade-debug'], default='upgrade-release')
        command.add_argument('--output', type=Path, help='New output directory; must not exist')
    factory.set_defaults(action=factory_release)
    stm32.set_defaults(action=stm32_release)
    stm32.add_argument('--version', required=True)
    stm32.add_argument('--key', type=Path, default=ROOT / 'crt/artifact-private.pem')
    stm32.add_argument('--public-key', type=Path, default=ROOT / 'crt/artifact-public.pem',
                       help='Product verification public key; default crt/artifact-public.pem')
    stm32.add_argument('--artifact-tool', type=Path, default=Path('D:/Technology_stack/Web/mender/tools/mender-artifact') if os.name == 'nt' else Path('/mnt/d/Technology_stack/Web/mender/tools/mender-artifact'))
    stm32.add_argument('--wsl-distro', default='Ubuntu-24.04')
    stm32.add_argument('--upload', action='store_true', help='Upload Releases only; never creates Deployment')
    stm32.add_argument('--http-client', choices=['urllib', 'chrome'], default='urllib',
                       help='Chrome-compatible TLS via optional curl_cffi')
    stm32.add_argument('--verify-tls', action='store_true',
                       help='Explicitly restore HTTPS certificate/CA/hostname validation; default off')
    stm32.add_argument('--server', default='https://127.0.0.1:18443')
    stm32.add_argument('--credentials', type=Path, default=Path('D:/Technology_stack/Web/mender/admin-credentials.json') if os.name == 'nt' else Path('/mnt/d/Technology_stack/Web/mender/admin-credentials.json'))
    stm32.add_argument('--ca', type=Path, default=Path('D:/Technology_stack/Web/mender/server/compose/certs/mender-ca.crt') if os.name == 'nt' else Path('/mnt/d/Technology_stack/Web/mender/server/compose/certs/mender-ca.crt'))
    args = parser.parse_args()
    try:
        args.action(args)
    except (OSError, ValueError, RuntimeError, tarfile.TarError) as error:
        print(f'FAILED: {error}', file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
