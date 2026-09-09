"""Check linked vectors, memory boundaries and RTOS symbols."""
import argparse
from pathlib import Path
import shutil
import struct
import subprocess

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('elf', type=Path)
    parser.add_argument('--nm', default=shutil.which('arm-none-eabi-nm'))
    args = parser.parse_args()
    if not args.nm:
        parser.error('arm-none-eabi-nm must be in PATH or supplied with --nm')
    symbols = {}
    for line in subprocess.check_output([args.nm, str(args.elf)], text=True).splitlines():
        parts = line.split()
        if len(parts) == 3:
            symbols[parts[2]] = (int(parts[0], 16), parts[1])
    binary = args.elf.with_suffix('.bin').read_bytes()
    assert 0 < len(binary) <= 128 * 1024
    stack, reset = struct.unpack_from('<II', binary)
    assert stack == 0x20009000
    assert reset == symbols['Reset_Handler'][0] | 1
    for index, name in {3:'HardFault_Handler', 11:'SVC_Handler', 14:'PendSV_Handler',
                        15:'SysTick_Handler', 22:'EXTI2_3_IRQHandler', 36:'TIM15_IRQHandler',
                        43:'USART1_IRQHandler', 45:'USART3_4_IRQHandler'}.items():
        assert symbols[name][1] == 'T', f'{name} resolves to a weak default'
        assert struct.unpack_from('<I', binary, index * 4)[0] == symbols[name][0] | 1
    for name in ('vTaskStartScheduler', 'xTaskCreateStatic', 'xQueueGenericCreateStatic', 'xPortSysTickHandler'):
        assert name in symbols, f'Missing FreeRTOS implementation: {name}'
    assert symbols['_ebss'][0] <= stack - 2048, 'RAM overlaps interrupt stack'
    assert not any(name.startswith('k_') for name in symbols), 'Unexpected old kernel API'
    assert not any(name in symbols for name in ('pvPortMalloc', 'malloc', '_sbrk')), 'Unexpected dynamic allocation'
    print(f'{args.elf}: vectors and static FreeRTOS verified; BIN {len(binary)} bytes; '
          f'RAM through BSS {symbols["_ebss"][0] - 0x20000000} bytes + 2048-byte interrupt stack')

if __name__ == '__main__':
    main()
