# 9C Nebulizer — FreeRTOS / STM32 GCC

STM32G070CBT6 固件：原生 FreeRTOS 多任务、STM32 HAL、CMSIS，64 MHz。
保留现有治疗状态机、预热/保温、上位机协议、雾化板协议和 EEPROM 参数格式。

## 编译

安装 STM32CubeCLT，将 arm-none-eabi-gcc、CMake、Ninja 加入 PATH。
本项目已包含固定版本依赖源码，构建不需要网络或 Python。

```sh
cmake --preset debug
cmake --build --preset debug
cmake --preset release
cmake --build --preset release
```

如果 GCC 不在 PATH，设置 `STM32_GCC_PATH` 为其 bin 目录。
产物在 `build/debug/` 或 `build/release/`：nebulizer.elf、.hex、.bin 和 .map。

## 测试与烧录

```sh
python tools/run_tests.py
python tools/verify_firmware.py build/release/nebulizer.elf
```

连接 ST-Link 后，使用 STM32CubeProgrammer 打开 HEX/ELF 烧录，或运行：

```sh
STM32_Programmer_CLI -c port=SWD -w build/release/nebulizer.hex -v -rst
```

三路 UART 均为 115200、8N1，调试日志为 USART2 PA2/PA3。
软件回归测试不替代实板测试；本次迁移尚未烧录和验证外设波形。

## 项目导航

- [J-Link + ELF 调试上位机](tools/nebulizer_debug/README.md)：独立 SWD 通道，保留原串口屏协议；启动 `tools/run_debug_host.ps1`。

- [任务、引脚、中断与板上验证](docs/FreeRTOS.md)
- [CLion 配置](docs/CLion.md)
- [上位机协议](docs/HostProtocolReference.md)
- [Python/PyQt6 上位机](tools/nebulizer_host_pyqt/README.md)
- `include/FreeRTOSConfig.h`：内核配置，静态内存分配。
- `include/nebulizer/app_config.h`：业务配置、任务栈和队列大小。
- `src/platform/`：时钟、HAL 外设、板级引脚、日志和异常处理。
- `src/app/`：任务创建、上下文和事件分发。
- `services/`、`protocols/`、`drivers_app/`：业务服务、协议和驱动。
- `cmake/`：GCC 工具链和 128 KiB Flash / 36 KiB RAM 链接脚本。
- [第三方源码版本和许可证](third_party/README.md)

### 雾化板缺水确认

主控逐条处理雾化板状态/故障报告：连续缺水至少 20 秒才确认缺水；期间任一次不缺水报告取消本次计时。已确认缺水后，连续至少 5 秒不报缺水才确认补水恢复；期间任一次缺水报告取消恢复计时。由达到门限后的有效报告提交状态，不把通信静默当作补水；通信超时中断候选计时。其他故障和雾化板自身锁定保持有效。确认恢复后仍需满足原有盖子、心跳、在线及故障条件才能自动继续剩余治疗时间。
