"""Fetch the pinned upstream sources required for an offline firmware build."""
import concurrent.futures
import io
from pathlib import Path
import urllib.request
import zipfile

ROOT = Path(__file__).resolve().parents[1] / 'third_party'
PACKAGES = [
    ('FreeRTOS', 'FreeRTOS/FreeRTOS-Kernel', 'V11.2.0',
     ('include/', 'portable/GCC/ARM_CM0/', 'tasks.c', 'queue.c', 'list.c', 'timers.c', 'event_groups.c', 'stream_buffer.c', 'LICENSE.md')),
    ('CMSIS', 'ARM-software/CMSIS_5', '5.9.0', ('CMSIS/Core/Include/', 'LICENSE.txt')),
    ('STM32G0', 'STMicroelectronics/cmsis-device-g0', 'v1.4.4',
     ('Include/', 'Source/Templates/system_stm32g0xx.c', 'Source/Templates/gcc/startup_stm32g070xx.s', 'LICENSE.md')),
    ('STM32G0_HAL', 'STMicroelectronics/stm32g0xx-hal-driver', 'v1.4.6', ('Inc/', 'Src/', 'LICENSE.md')),
]

def fetch(package):
    name, repo, tag, selected = package
    url = f'https://codeload.github.com/{repo}/zip/refs/tags/{tag}'
    with urllib.request.urlopen(url, timeout=90) as response:
        archive = zipfile.ZipFile(io.BytesIO(response.read()))
    for entry in archive.infolist():
        relative = entry.filename.split('/', 1)[-1]
        if entry.is_dir() or not any(relative == p or (p.endswith('/') and relative.startswith(p)) for p in selected):
            continue
        destination = ROOT / name / relative
        if not destination.resolve().is_relative_to(ROOT.resolve()):
            raise ValueError(relative)
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_bytes(archive.read(entry))
    print(f'{name}: {repo} @ {tag}', flush=True)

if __name__ == '__main__':
    with concurrent.futures.ThreadPoolExecutor(max_workers=4) as executor:
        list(executor.map(fetch, PACKAGES))
