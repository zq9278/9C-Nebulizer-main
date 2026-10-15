"""Build a validated update package and a complete first-install Flash image."""
import argparse
from pathlib import Path
import sys
import struct
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from fw_update.protocol import APP_BASE, META_BASE, RAM_END, package_bytes, metadata_bytes

def ihex(blob, address=0x08000000):
    records = []
    def record(kind, offset, data):
        body = bytes([len(data)]) + struct.pack('>H',offset) + bytes([kind]) + data
        records.append(':' + (body + bytes([-sum(body) & 255])).hex().upper())
    upper = None
    for start in range(0,len(blob),16):
        absolute = address + start
        if absolute >> 16 != upper:
            upper = absolute >> 16
            record(4,0,struct.pack('>H',upper))
        record(0,absolute & 65535,blob[start:start+16])
    record(1,0,b'')
    return '\n'.join(records) + '\n'

def build(app, boot, output):
    data, loader = app.read_bytes(), boot.read_bytes()
    package = package_bytes(data)
    if not 192 <= len(loader) <= 16*1024: raise ValueError('Boot does not fit 16 KiB')
    sp,pc = struct.unpack_from('<II',loader)
    if sp != RAM_END or not pc & 1 or not 0x08000000 <= (pc & ~1) < 0x08000000 + len(loader):
        raise ValueError('Boot vector table mismatch')
    image = bytearray(b'\xff' * (128*1024))
    image[:len(loader)] = loader
    app_offset = APP_BASE - 0x08000000
    image[app_offset:app_offset+len(data)] = data
    meta = metadata_bytes(data); meta_offset = META_BASE - 0x08000000
    image[meta_offset:meta_offset+len(meta)] = meta
    output.mkdir(parents=True,exist_ok=True)
    (output/'nebulizer.nfu').write_bytes(package)
    (output/'nebulizer_factory.bin').write_bytes(image)
    (output/'nebulizer_factory.hex').write_text(ihex(image),encoding='ascii')
    print(f'Boot {len(loader)} / 16384 bytes; App {len(data)} / 112640 bytes; '
          f'package and first-install HEX generated in {output}')

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--app',type=Path,required=True)
    parser.add_argument('--boot',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    args = parser.parse_args()
    build(args.app,args.boot,args.output)
