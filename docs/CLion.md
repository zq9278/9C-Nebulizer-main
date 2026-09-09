# CLion + STM32 GCC

1. 安装 STM32CubeCLT，将 GNU-tools-for-STM32/bin、CMake/bin 和 Ninja/bin 加入 PATH。
2. 在 CLion 打开项目根目录，导入 CMakePresets.json 的 debug/release 配置。
3. 目标选择 nebulizer，交叉编译器由 cmake/arm-none-eabi.cmake 查找。
4. 如果找不到 GCC，在 CMake 环境中设置 STM32_GCC_PATH 指向 GCC 的 bin 目录。
5. 输出在 build/debug/ 或 build/release/；调试使用 nebulizer.elf。

```sh
cmake --preset debug
cmake --build --preset debug
```

用 STM32CubeProgrammer 烧录 HEX/ELF。CLion 调试使用 Embedded GDB Server，
选择 STM32CubeCLT 的 arm-none-eabi-gdb 和 ST-LINK_gdbserver；按本机路径设置服务器，
使用 SWD、STM32G070CBT6、build/debug/nebulizer.elf。

已有 GDB server 时可使用 Remote GDB，端口与 server 配置保持一致。
VS Code 的 launch.json 连接 localhost:3333，需要先单独启动 GDB server。

纯 C 回归测试：`python tools/run_tests.py`，也可用 --cc 指定本机 GCC/Clang。
任务和中断说明见 [FreeRTOS.md](FreeRTOS.md)。
