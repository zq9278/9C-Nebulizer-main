# 9C Nebulizer — FreeRTOS / STM32 GCC

STM32G070CBT6 固件：原生 FreeRTOS 多任务、STM32 HAL、CMSIS，64 MHz。
保留现有治疗状态机、预热/保温、上位机协议、雾化板协议和 EEPROM 参数格式。

## Electron 模块化上位机

默认上位机已迁移为 Electron + React + TypeScript，按连接、监控、参数、维护、曲线、
日志、串口升级和工厂烧录拆分。通用组件位于 `tools/workbench/packages/`：
`@workbench/core`、`@workbench/charts`、`@workbench/ui`、`@workbench/connections`。
曲线组件只接收通用时序采样与通道定义，不依赖雾化器、串口或 SWD 协议。

```powershell
# 源码运行：先安装 Node.js/npm 和 Python
python -m pip install -r tools/workbench/backend/requirements.txt
python -m pyocd pack install stm32g070cbtx
.\tools\run_debug_host.ps1
```

Windows 免安装包位于 `tools/workbench/release/Nebulizer-Workbench-1.0.0-x64.exe`，
内置无 Qt 硬件后台及芯片算法包。J-Link 仍需要 SEGGER 驱动/DLL。
构建、测试、打包与新项目模块复用见 [Electron 上位机说明](tools/workbench/README.md)。
原 PyQt 界面保留为 `tools/run_pyqt_debug_host.ps1`，不再是默认启动入口。

## 编译

安装 STM32CubeCLT，将 arm-none-eabi-gcc、CMake、Ninja 加入 PATH。
本项目已包含固定版本依赖源码。下面的单体固件构建不需要网络或 Python；
包含 Boot 和升级包的完整构建需要 Python，步骤见下一节。

```sh
cmake --preset debug
cmake --build --preset debug
cmake --preset release
cmake --build --preset release
```

如果 GCC 不在 PATH，设置 `STM32_GCC_PATH` 为其 bin 目录。
产物在 `build/debug/` 或 `build/release/`：nebulizer.elf、.hex、.bin 和 .map。

## 完整固件构建、校验与打包（包含 Boot）

以下命令均在项目根目录执行，使用 Windows PowerShell。每一步成功后再执行下一步；
任何编译或校验失败都应停止，不能发布目录中残留的旧产物。

该版本的 Flash 分区为 Boot 16 KiB、App 110 KiB、有效镜像记录 2 KiB，
不使用外部存储。升级中断后可重新完整升级，不提供旧版本回滚。

### 1. 准备工具与 Python 依赖

安装 STM32CubeCLT、CMake、Ninja 和 Python，并确保 `arm-none-eabi-gcc`、
`arm-none-eabi-nm`、`cmake`、`ninja`、`python` 在 PATH 中。
软件回归测试另外需要本机 GCC/Clang（不能使用 Arm 交叉编译器）；
测试脚本会自动查找 PATH 中的编译器或 Windows 上 CLion 自带的 MinGW。

```powershell
python -m pip install -r tools/nebulizer_debug/requirements.txt
```

如果需要指定本机测试编译器，使用
`python tools/run_tests.py --cc 'C:\实际路径\gcc.exe'` 替代后面的测试命令。

### 2. 编译 Boot 和 App，自动生成固件包

```powershell
cmake --preset upgrade-release
cmake --build --preset upgrade-release
```

这一步会同时编译 `nebulizer_boot` 和 `nebulizer`，生成各自的 ELF、HEX、BIN、MAP，
然后自动运行 `tools/fw_update/package.py`，完成：

- 检查 Boot 的 16 KiB 容量、栈顶和 Reset 向量。
- 检查 App 的 110 KiB 容量、栈顶、Reset 向量及链接地址 `0x08004000`。
- 生成带产品标识、长度和 CRC32 的串口升级包 `nebulizer.nfu`。
- 将 Boot、App 和有效记录合并为首次安装用的 `nebulizer_factory.hex` / `.bin`。

成功输出包含 `Boot ... / 16384 bytes; App ... / 112640 bytes`。
打包失败会使构建失败；无需手动拼接 BIN 或计算有效记录。
需要调试构建时，将两个命令的 preset 改为 `upgrade-debug`，产物位于 `build/upgrade-debug/`。

### 3. 校验固件和升级协议

```powershell
python tools/verify_firmware.py build/upgrade-release/nebulizer.elf
python tools/run_tests.py
python tests/boot/test_upgrade_host.py
```

第一条检查 App 中断向量、RAM 边界和静态 FreeRTOS 实现；Boot 的 BIN 边界和启动向量
在上一步打包时检查。第二条运行 C 回归测试，并生成第三条使用的真实 C Boot 模拟器。
第三条校验升级包 CRC、完整烧录镜像和 HEX 校验和，并测试串口升级全流程、
ACK 丢失重传、镜像错误拒绝及升级窗口。必须按此顺序执行。

修改 SWD 调试上位机后，还可以执行以下回归。该测试目前依赖单体 Debug ELF：

```powershell
cmake --preset debug
cmake --build --preset debug
python tests/debug/test_debug_host.py
python tests/debug/test_daplink_transport.py
```

### 4. 确认产物并归档

发布文件位于 `build/upgrade-release/`：

| 文件 | 用途 |
|---|---|
| `nebulizer_factory.hex` | 首次安装完整固件，包含 Boot、App、有效记录；推荐用于烧录 |
| `nebulizer_factory.bin` | 完整 128 KiB 镜像，烧录基址为 `0x08000000` |
| `nebulizer.nfu` | 后续上位机串口升级，只更新 App |
| `nebulizer.elf` / `.map` | App 调试和分析；SWD 调试上位机选择这个 ELF |
| `nebulizer.hex` / `.bin` | 独立 App 镜像，基址 `0x08004000`；不含 Boot 和有效记录 |
| `nebulizer_boot.elf` / `.map` / `.hex` / `.bin` | 独立 Boot 调试、分析和烧录文件 |

如需把完整烧录固件、升级包和调试文件一起交付，可生成 ZIP：

```powershell
Compress-Archive -Path @(
    'build/upgrade-release/nebulizer_factory.hex',
    'build/upgrade-release/nebulizer_factory.bin',
    'build/upgrade-release/nebulizer.nfu',
    'build/upgrade-release/nebulizer.elf',
    'build/upgrade-release/nebulizer.map',
    'build/upgrade-release/nebulizer.hex',
    'build/upgrade-release/nebulizer.bin',
    'build/upgrade-release/nebulizer_boot.elf',
    'build/upgrade-release/nebulizer_boot.map',
    'build/upgrade-release/nebulizer_boot.hex',
    'build/upgrade-release/nebulizer_boot.bin',
    'docs/FirmwareUpgrade.md'
) -DestinationPath 'build/nebulizer-upgrade-release.zip' -Force
```

ZIP 是交付归档；上位机选择 ZIP 内的 `.nfu`，烧录器选择 `nebulizer_factory.hex`。
归档必须来自同一次通过校验的构建，升级后调试使用同包的 App ELF。

### 5. 首次烧录与后续升级

首次安装需用 DAPLink、ST-Link 或 J-Link 及对应烧录工具写入 `nebulizer_factory.hex`。
现有调试上位机也支持 J-Link / DAPLink 工厂烧录：选择并刷新探针，保持调试断开，
进入“工厂烧录”，选择完整工厂镜像，确认实际输出已停止后执行。
DAPLink 首次烧录先运行 `python -m pyocd pack install stm32g070cbtx` 安装芯片算法。
模块读回校验完整 128 KiB，默认不复位运行；可在窗口勾选校验后复位。
具体操作见 [工厂烧录模块说明](tools/nebulizer_debug/README.md)。
旧单体固件没有本 Boot，不能直接使用 `.nfu` 迁移，也不能只烧录独立 App。
使用 STM32CubeProgrammer + ST-Link 时，例如：

```powershell
STM32_Programmer_CLI -c port=SWD -w build/upgrade-release/nebulizer_factory.hex -v -rst
```

后续升级使用原上位机通信串口 USART1（PB6 TX、PB7 RX、115200 / 8N1）。
先停止治疗、保温和维护，释放并断开 SWD 调试控制，再启动上位机：

```powershell
.\tools\run_debug_host.ps1
```

进入“串口升级”，选择对应 COM 口和 `nebulizer.nfu`。
原串口上位机中的 **Firmware Update** 也可使用同一升级窗口。
升级时关闭其他串口程序，避免串口屏同时向同一总线发帧。
也可以使用命令行，将 COM5 换成实际端口：

```powershell
python tools/fw_update/client.py --port COM5 --firmware build/upgrade-release/nebulizer.nfu
```

完整的协议字段、重传规则、Boot 保护和中断恢复见 [固件升级说明](docs/FirmwareUpgrade.md)。
软件校验不能代替实板验收；首次发布前需验证升级期间输出关闭、正常启动和断电后重新升级。

## 单体固件测试与烧录

原 `debug` / `release` 为不含 Boot 的单体固件，使用以下流程：

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
