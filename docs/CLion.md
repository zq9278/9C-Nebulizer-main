# CLion 开发配置与编译烧录说明

本文档适用于当前工程：

- 项目路径：`/Users/zq/project/Nebulizer/software/9C-Nebulizer-main`
- 目标板：`nebulizer_g070cbt6`
- 板级目录：`boards/arm/nebulizer_g070cbt6/`
- 建议 Zephyr 基础路径：`/Users/zq/zephyrproject/zephyr`
- 建议 west/Python 虚拟环境：`/Users/zq/zephyrproject/.venv/`
- 建议 Zephyr SDK：`/Users/zq/zephyr-sdk-1.0.1`

## 1. 前置条件

确认本机已具备以下工具：

- `CLion`
- `CMake`
- `Ninja`
- `Zephyr SDK`
- `west`
- Python 虚拟环境中的 Zephyr 依赖包

本工程已经包含：

- 独立 board 定义：`boards/arm/nebulizer_g070cbt6/`
- `BOARD_ROOT` 配置：见项目根 [CMakeLists.txt](/Users/zq/project/Nebulizer/software/9C-Nebulizer-main/CMakeLists.txt)
- west 工作区指向：见 [.west/config](/Users/zq/project/Nebulizer/software/9C-Nebulizer-main/.west/config)
- CLion 预设：见 [CMakePresets.json](/Users/zq/project/Nebulizer/software/9C-Nebulizer-main/CMakePresets.json)

## 2. 常用路径和环境变量

建议使用如下路径。若你本机不同，需同步修改 CLion 配置和 `CMakePresets.json`。

- `ZEPHYR_BASE=/Users/zq/zephyrproject/zephyr`
- `ZEPHYR_SDK_INSTALL_DIR=/Users/zq/zephyr-sdk-1.0.1`
- `WEST_PYTHON=/Users/zq/zephyrproject/.venv/bin/python`
- `WEST=/Users/zq/zephyrproject/.venv/bin/west`

可在终端临时导出：

```bash
export ZEPHYR_BASE=/Users/zq/zephyrproject/zephyr
export ZEPHYR_SDK_INSTALL_DIR=/Users/zq/zephyr-sdk-1.0.1
export PATH=/Users/zq/zephyrproject/.venv/bin:$PATH
```

检查命令：

```bash
echo "$ZEPHYR_BASE"
echo "$ZEPHYR_SDK_INSTALL_DIR"
which west
which python
cmake --version
ninja --version
```

## 3. 在 CLion 中导入项目

### 方法 A：直接打开项目目录

1. 打开 CLion。
2. 选择 `Open`。
3. 选择目录：
   `/Users/zq/project/Nebulizer/software/9C-Nebulizer-main`
4. CLion 会检测到 [CMakeLists.txt](/Users/zq/project/Nebulizer/software/9C-Nebulizer-main/CMakeLists.txt)。
5. 首次加载时，选择使用 `CMakePresets.json`，优先使用 preset：
   `clion-nebulizer-g070cbt6`

### 方法 B：手工创建 CMake Profile

如果不使用 preset，可在：

- `CLion > Settings > Build, Execution, Deployment > CMake`

新增一个 Profile，例如：

- `Name`: `nebulizer_g070cbt6`
- `Build type`: `RelWithDebInfo`
- `Generator`: `Ninja`
- `Build directory`: `cmake-build-clion-nebulizer-g070cbt6`

`CMake options` 建议填写：

```cmake
-DBOARD=nebulizer_g070cbt6
-DZEPHYR_BASE=/Users/zq/zephyrproject/zephyr
-DPython3_EXECUTABLE=/Users/zq/zephyrproject/.venv/bin/python
-DNO_BUILD_TYPE_WARNING=ON
-DCMAKE_EXPORT_COMPILE_COMMANDS=ON
```

`Environment` 建议增加：

```text
ZEPHYR_BASE=/Users/zq/zephyrproject/zephyr;ZEPHYR_SDK_INSTALL_DIR=/Users/zq/zephyr-sdk-1.0.1;PATH=/Users/zq/zephyrproject/.venv/bin:$PATH
```

## 4. CMake 工具链与 Zephyr SDK 配置

CLion 不需要手工写传统 `toolchain.cmake`。当前工程通过 Zephyr 的标准方式工作：

- `find_package(Zephyr REQUIRED ...)`
- `BOARD=nebulizer_g070cbt6`
- `BOARD_ROOT` 指向当前工程根

CLion 中重点确认以下内容：

1. `CMake` 可执行文件可用。
2. `Ninja` 可执行文件可用。
3. `Python3_EXECUTABLE` 指向 Zephyr 虚拟环境中的 Python。
4. `ZEPHYR_BASE` 指向 `/Users/zq/zephyrproject/zephyr`。
5. `ZEPHYR_SDK_INSTALL_DIR` 指向 `/Users/zq/zephyr-sdk-1.0.1`。

说明：

- 如果 CLion 使用 `Debug` profile，Zephyr 可能提示 build type 与优化级别不一致。
- 当前工程已默认设置 `NO_BUILD_TYPE_WARNING=ON`。
- 如果你希望更贴近嵌入式实际构建，建议直接使用 `RelWithDebInfo`。

如果 CLion 没有自动识别 SDK，可在 CMake Profile 的 Environment 中明确设置：

```text
ZEPHYR_TOOLCHAIN_VARIANT=zephyr;ZEPHYR_SDK_INSTALL_DIR=/Users/zq/zephyr-sdk-1.0.1
```

## 5. 设置构建目标和 board

当前工程必须使用：

```text
BOARD=nebulizer_g070cbt6
```

不要继续使用：

- `nucleo_g070rb`
- `nucleo_g070rb_stm32g070xx.overlay`

因为当前硬件资源已经独立定义在：

- [boards/arm/nebulizer_g070cbt6/nebulizer_g070cbt6.dts](/Users/zq/project/Nebulizer/software/9C-Nebulizer-main/boards/arm/nebulizer_g070cbt6/nebulizer_g070cbt6.dts)
- [boards/arm/nebulizer_g070cbt6/pinctrl.dtsi](/Users/zq/project/Nebulizer/software/9C-Nebulizer-main/boards/arm/nebulizer_g070cbt6/pinctrl.dtsi)

## 6. 执行 pristine build

### 方式 A：终端使用 west

在项目根目录执行：

```bash
/Users/zq/zephyrproject/.venv/bin/west build -b nebulizer_g070cbt6 . -d build_west -p always
```

### 方式 B：终端直接调用 CMake

```bash
cmake -B build -GNinja \
  -DBOARD=nebulizer_g070cbt6 \
  -DZEPHYR_BASE=/Users/zq/zephyrproject/zephyr \
  -DPython3_EXECUTABLE=/Users/zq/zephyrproject/.venv/bin/python \
  .

cmake --build build -j8
```

如果需要等价的 pristine 行为，先删掉构建目录：

```bash
rm -rf build cmake-build-clion-nebulizer-g070cbt6 build_west
```

### 方式 C：CLion 中执行 pristine

CLion 没有 west 的 `-p always` 语义按钮。推荐以下做法之一：

1. `File > Reload CMake Project`
2. `Tools > CMake > Reset Cache and Reload Project`
3. 删除 `cmake-build-clion-nebulizer-g070cbt6/` 后重新加载

如果 DTS、Kconfig、board、`prj.conf` 有改动，优先做 pristine。

## 7. 编译固件

### CLion 中编译

若使用 preset：

- 选择 `clion-nebulizer-g070cbt6`
- 点击 `Build`

若使用手工 CMake Profile：

- 选中 `nebulizer_g070cbt6` Profile
- 点击 `Build Project`

### 终端编译

west：

```bash
/Users/zq/zephyrproject/.venv/bin/west build -b nebulizer_g070cbt6 . -d build_west
```

cmake：

```bash
cmake --build build -j8
```

## 8. 构建产物位置

### 使用 CLion preset / 手工 CMake Profile

默认输出目录：

- `cmake-build-clion-nebulizer-g070cbt6/zephyr/`

常用产物：

- `cmake-build-clion-nebulizer-g070cbt6/zephyr/zephyr.elf`
- `cmake-build-clion-nebulizer-g070cbt6/zephyr/zephyr.bin`
- `cmake-build-clion-nebulizer-g070cbt6/zephyr/zephyr.hex`
- `cmake-build-clion-nebulizer-g070cbt6/zephyr/zephyr.map`

### 使用 west

默认输出目录：

- `build_west/zephyr/`

常用产物：

- `build_west/zephyr/zephyr.elf`
- `build_west/zephyr/zephyr.bin`
- `build_west/zephyr/zephyr.hex`
- `build_west/zephyr/zephyr.map`

### 使用本工程已有 build 目录

如果你直接用了项目根 `build/`：

- `build/zephyr/zephyr.elf`
- `build/zephyr/zephyr.bin`
- `build/zephyr/zephyr.hex`

## 9. 烧录到开发板

当前板级 runner 配置在：

- [boards/arm/nebulizer_g070cbt6/board.cmake](/Users/zq/project/Nebulizer/software/9C-Nebulizer-main/boards/arm/nebulizer_g070cbt6/board.cmake)

已配置：

- `stm32cubeprogrammer`
- `openocd`
- `pyocd`
- `jlink`

### 方式 A：west flash

如果使用 `build_west`：

```bash
/Users/zq/zephyrproject/.venv/bin/west flash -d build_west
```

如果先执行过默认 `west build`：

```bash
/Users/zq/zephyrproject/.venv/bin/west flash
```

### 方式 B：指定 runner

例如优先使用 ST-Link / STM32CubeProgrammer：

```bash
/Users/zq/zephyrproject/.venv/bin/west flash -d build_west --runner stm32cubeprogrammer
```

使用 J-Link：

```bash
/Users/zq/zephyrproject/.venv/bin/west flash -d build_west --runner jlink
```

使用 pyOCD：

```bash
/Users/zq/zephyrproject/.venv/bin/west flash -d build_west --runner pyocd
```

### 方式 C：CLion External Tool

可在 CLion 中添加 External Tool：

- `Program`:
  `/Users/zq/zephyrproject/.venv/bin/west`
- `Arguments`:
  `flash -d $ProjectFileDir$/build_west`
- `Working directory`:
  `$ProjectFileDir$`

也可以增加一个带 runner 的版本：

- `Arguments`:
  `flash -d $ProjectFileDir$/build_west --runner stm32cubeprogrammer`

## 10. 使用调试器加载 elf

若不走 `west flash`，也可直接用调试器工具加载：

- ELF 文件：
  `build_west/zephyr/zephyr.elf`

常见方式：

1. ST-Link + STM32CubeProgrammer
2. J-Link + JLinkGDBServer
3. pyOCD

当前板未占用 SWD：

- `PA13 = SWDIO`
- `PA14 = SWCLK`

因此可直接连接 SWD 调试。

## 11. CLion 中推荐的工作流

推荐分成两个配置：

1. `CLion CMake Profile`
   用于索引、跳转、单步、静态检查、直接编译
2. `west build_west`
   用于标准 Zephyr 验证、烧录、runner 行为一致性

建议操作顺序：

1. 在 CLion 中编辑代码
2. 若改了 DTS/Kconfig/board，先 pristine
3. 在 CLion 中 `Build`
4. 在终端执行 `west build -b nebulizer_g070cbt6 . -d build_west`
5. 用 `west flash -d build_west` 烧录

## 12. 常见问题与排错建议

### 1. `west: unknown command "build"`

原因：

- 当前目录不是有效 west 工作区
- 使用了系统里错误的 west

处理：

```bash
which west
/Users/zq/zephyrproject/.venv/bin/west topdir
```

本工程已经包含 [.west/config](/Users/zq/project/Nebulizer/software/9C-Nebulizer-main/.west/config)。
若仍异常，优先显式使用：

```bash
/Users/zq/zephyrproject/.venv/bin/west build -b nebulizer_g070cbt6 .
```

### 2. `Error finding board: nebulizer_g070cbt6`

检查：

- `boards/arm/nebulizer_g070cbt6/board.yml` 是否存在
- `CMakeLists.txt` 是否保留 `BOARD_ROOT`
- 是否在项目根目录执行命令

检查命令：

```bash
pwd
ls boards/arm/nebulizer_g070cbt6
```

### 3. DTS alias / chosen 相关错误

当前业务代码依赖 board alias 和 custom node。
如有 DTS 报错，先检查：

- [boards/arm/nebulizer_g070cbt6/nebulizer_g070cbt6.dts](/Users/zq/project/Nebulizer/software/9C-Nebulizer-main/boards/arm/nebulizer_g070cbt6/nebulizer_g070cbt6.dts)
- [src/platform/board_devices.c](/Users/zq/project/Nebulizer/software/9C-Nebulizer-main/src/platform/board_devices.c)

遇到 DTS 改动后旧缓存污染，优先 pristine。

### 4. Python 包缺失，例如 `jsonschema`

说明用了错误的 Python。
应让 CLion 和 CMake 都使用：

```text
/Users/zq/zephyrproject/.venv/bin/python
```

必要时：

```bash
source /Users/zq/zephyrproject/.venv/bin/activate
pip install -U pip
pip install -r /Users/zq/zephyrproject/zephyr/scripts/requirements.txt
```

### 5. 找不到 Zephyr SDK

检查：

```bash
echo "$ZEPHYR_SDK_INSTALL_DIR"
ls /Users/zq/zephyr-sdk-1.0.1
```

若 CLion 未继承环境变量，在 CMake Profile 中显式设置：

```text
ZEPHYR_TOOLCHAIN_VARIANT=zephyr
ZEPHYR_SDK_INSTALL_DIR=/Users/zq/zephyr-sdk-1.0.1
```

### 6. 烧录失败

优先排查：

- ST-Link/J-Link/pyOCD 是否安装
- USB 线缆和供电
- SWD 接线是否正确
- 板子是否被旧程序锁死，需要 under-reset 连接

本工程 `board.cmake` 已对 `stm32cubeprogrammer` 和 `pyocd` 配置了硬件 reset / under-reset 选项。

### 7. 改了 `prj.conf`、`Kconfig`、DTS，但 CLion 里没生效

这是缓存问题。做一次完整 pristine：

```bash
rm -rf cmake-build-clion-nebulizer-g070cbt6 build build_west
```

然后重新加载 CLion，再重新构建。

### 8. `__ASSERT() statements are globally ENABLED`

这不是错误，是提示当前工程启用了运行时断言。

来源：

- [prj.conf](/Users/zq/project/Nebulizer/software/9C-Nebulizer-main/prj.conf) 中 `CONFIG_ASSERT=y`

当前工程保留该配置，原因是它对驱动封装、状态机和安全路径调试更有价值。
如果你要做更接近发布版的构建，可改为：

```text
CONFIG_ASSERT=n
```

然后 pristine rebuild。

## 13. 推荐命令清单

标准 west 构建：

```bash
/Users/zq/zephyrproject/.venv/bin/west build -b nebulizer_g070cbt6 . -d build_west
```

pristine build：

```bash
/Users/zq/zephyrproject/.venv/bin/west build -b nebulizer_g070cbt6 . -d build_west -p always
```

烧录：

```bash
/Users/zq/zephyrproject/.venv/bin/west flash -d build_west
```

J-Link 烧录：

```bash
/Users/zq/zephyrproject/.venv/bin/west flash -d build_west --runner jlink
```

直接 CMake 构建：

```bash
cmake -B build -GNinja \
  -DBOARD=nebulizer_g070cbt6 \
  -DZEPHYR_BASE=/Users/zq/zephyrproject/zephyr \
  -DPython3_EXECUTABLE=/Users/zq/zephyrproject/.venv/bin/python \
  .

cmake --build build -j8
```
