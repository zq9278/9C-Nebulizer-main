"""Build and execute portable C regression tests, using a native GCC/Clang."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--cc', default=os.environ.get('HOST_CC'))
    args = parser.parse_args()
    cc = args.cc or shutil.which('gcc') or shutil.which('clang')
    if not cc and os.name == 'nt':
        candidates = sorted(Path('C:/Program Files/JetBrains').glob('CLion */bin/mingw/bin/gcc.exe'))
        if candidates:
            cc = str(candidates[-1])
    if not cc:
        parser.error('Native GCC/Clang required: pass --cc or set HOST_CC')
    output = ROOT / 'build' / 'tests'
    output.mkdir(parents=True, exist_ok=True)
    common = ['protocols/common/crc16_modbus.c', 'protocols/common/frame_codec.c']
    cases = {
        'protocol/test_crc16_modbus.c': common,
        'protocol/test_frame_codec.c': common,
        'protocol/test_stream_parser.c': common + ['protocols/common/ring_frame_parser.c'],
        'state_machine/test_treatment_sm.c': ['src/state_machine/treatment_sm.c'],
    }
    environment = os.environ.copy()
    environment['PATH'] = str(Path(cc).resolve().parent) + os.pathsep + environment.get('PATH', '')
    for test, sources in cases.items():
        executable = output / (Path(test).stem + ('.exe' if os.name == 'nt' else ''))
        subprocess.run([cc, '-std=c11', '-Wall', '-Wextra', '-Werror', '-I.', '-Iinclude',
                        'tests/' + test, *sources, '-o', str(executable)], cwd=ROOT, env=environment, check=True)
        subprocess.run([str(executable)], cwd=ROOT, env=environment, check=True)

if __name__ == '__main__':
    main()
