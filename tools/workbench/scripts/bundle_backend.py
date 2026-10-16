"""Build an independent no-Qt hardware executable; bundle installed G0 DFP."""
from pathlib import Path
import subprocess
import sys
import shutil
ROOT=Path(__file__).resolve().parents[1]
TOOLS=ROOT.parent
def main():
    import PyInstaller
    from pyocd.target.pack.pack_target import ManagedPacks
    import cmsis_pack_manager
    cache=cmsis_pack_manager.Cache(True,True)
    packs=ManagedPacks.get_installed_packs(cache)
    paths=[Path(cache.data_path)/p.get_pack_name() for p in packs if 'STM32G0xx_DFP' in str(p)]
    if not paths:
        raise SystemExit('Install device pack first: python -m pyocd pack install stm32g070cbtx')
    args=[sys.executable,'-m','PyInstaller','--noconfirm','--clean','--onedir',
        '--name','device-backend','--distpath',str(ROOT/'runtime'),'--workpath',str(ROOT/'build/pyinstaller'),
        '--specpath',str(ROOT/'build'),'--paths',str(TOOLS),'--paths',str(TOOLS/'nebulizer_debug'),
        '--collect-submodules','pyocd','--collect-submodules','pylink','--copy-metadata','pyocd',
        '--collect-all','cmsis_pack_manager','--collect-all','libusb_package',
        '--hidden-import','serial.tools.list_ports','--hidden-import','elftools.elf.elffile',
        '--hidden-import','intelhex','--exclude-module','PyQt6','--exclude-module','tkinter',
        '--add-data',str(paths[-1])+':flash-packs',str(ROOT/'backend/server.py')]
    subprocess.run(args,cwd=ROOT,check=True)
if __name__=='__main__':main()
