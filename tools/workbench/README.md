# Electron 模块化设备工作台

Electron + React + TypeScript 桌面界面。包含 SWD/USART1 连接、监控、治疗控制、参数编辑、维护、曲线、日志、App 串口升级和工厂 HEX 烧录。

界面全部使用 Electron；硬件访问放在独立无 Qt 进程中，复用已有 ELF/Flash 校验、邮箱协议、串口升级和烧录实现。
Windows 发布包内置该后台程序、Python 运行时和 STM32G0 DFP，无需用户安装 Python 或 Node。
J-Link 仍需 SEGGER 驱动/DLL，DAPLink 仍需匹配的 USB 驱动和有效接线。

## 启动

源码运行需要 Node.js（本机验证版本 24）、npm 和 Python 3.12：

```powershell
# 在仓库根目录
python -m pip install -r tools/workbench/backend/requirements.txt
python -m pyocd pack install stm32g070cbtx
.\tools\run_debug_host.ps1
```

`run_debug_host.ps1` 现在默认启动 Electron；也可运行 `run_electron_host.ps1`。
首次启动自动 `npm ci`，随后检查 TypeScript 并构建界面。
原 PyQt 调试界面保留为 `tools/run_pyqt_debug_host.ps1`。
不要同时用两个上位机连接同一探针或串口。

手动启动：

```powershell
cd tools/workbench
npm ci
npm start
```

开发热更新：一个终端运行 `npm run dev`，另一个终端设置
`$env:WORKBENCH_DEV_URL='http://127.0.0.1:5173'` 后运行 `npx electron .`。
`WORKBENCH_PYTHON` 可以指定后台 Python 可执行文件路径。

## 功能边界

| 模块 | 内容 |
|---|---|
| 连接 | J-Link / DAPLink，ELF 校验；USART1 115200/8N1；默认观察，手动接管 |
| 监控 | 四路温度、ADC、PID 反馈、液位/盖子/雾化状态；治疗命令及控制权管理 |
| 参数 | 独立编辑草稿、脏标记、JSON 导入导出、批次 ACK、成功后设备读回 |
| 维护 | 进入/退出维护、风机/雾化档位、手动加热 |
| 曲线 | 通用时序图、通道选择、多单位轴、横纵缩放/平移、CSV 导出和连续记录 |
| 串口升级 | `.nfu` 校验、进入 Boot、串行分块与重试、取消、Flash CRC 与启动确认 |
| 工厂烧录 | 完整 128 KiB HEX、芯片检查、擦写和完整读回校验、可选复位 |
| 日志 | 错误/事件筛选、搜索和 CSV 导出 |

观察模式不发送业务修改；SWD 连接仍不暂停/复位。工厂烧录是单独的明确安装操作。
串口接管后发送心跳；串口释放会关闭维护和保温、暂停治疗并断开串口。
SWD 释放继续使用固件调试租约。安装与连接互斥；安装前需要操作者确认实际输出已停止。
安装不自动重试整次作业，未知结果不当作成功；工厂烧录不支持强行中止。

保温两小时计时由硬件服务执行，不依赖当前显示的页面；断开后计时清除。
曲线断开后保留，开始新连接时清空以免混合不同设备；设备时钟倒退时开启新的曲线历史。
CSV 连续记录独立于曲线缓存和页面，清空曲线不会中止 CSV 写入。
缓存 864,000 点，100 ms 采样约 24 小时；长时间保存使用 CSV。

## 目录与复用

```text
workbench/
  electron/                 桌面窗口、受限 preload、RPC、文件对话框、CSV 写入
  backend/                  JSON-lines 硬件服务，无 Qt
  packages/
    core/                   通道/采样/API 契约、环形缓存、保留尖峰的抽样、CSV
    charts/                 通用 TimeSeriesChart，与设备协议完全无关
    ui/                     通用 Panel、Field、NumberInput、Badge 等
    connections/            注入 API 与状态的通用连接面板
  src/
    projects/nebulizer/     9C 模型、命令、限制、单位换算与通道映射
    core/                   本项目运行状态、事件流、采样接入
    modules/                各独立功能页面，组合通用组件和项目适配器
  tests/                    缓存/命令、真实 Electron、无硬件后台测试
  scripts/                  无 Qt 后台程序打包
```

四个 npm workspace 包可以原样加入其他项目的 workspace；React 是 peer dependency。
`src/modules` 是功能页面组合层，包含当前设备的业务操作，不能直接当作其他硬件的协议实现。
其他项目新增自己的 `projects/<id>` 模型和后台适配器，复用上述库及桌面 IPC 架构。
不要把雾化器字段或 STM32 命令添加到 `packages/charts`。
通用 UI 使用 CSS 类，宿主负责主题；本项目主题位于 `src/style.css`、`src/tokens.css`。

曲线仅接收通道描述和 `{ time, values }`，不认识 NTC、PID、SWD 或串口：

```tsx
import {SampleBuffer} from '@workbench/core';
import {TimeSeriesChart} from '@workbench/charts';

const buffer = new SampleBuffer(100_000);
const channels = [{id:'voltage',label:'电压',unit:'V',color:'#3478ed'}];
// 每次接收数据：加入缓存，更新宿主 revision；组件不读设备、不发命令。
buffer.push({time:1.25,values:{voltage:3.31}});
// <TimeSeriesChart buffer={buffer} channels={channels} revision={revision}/>
```

`@workbench/connections` 接收 `api`、`state`、`onConnect`、`options` 和项目说明；
不导入本应用 store 或雾化器协议。`useConnectionOptions(projectId, defaults)` 使用独立配置键，
新项目不要复用 `nebulizer` 标识。

后台 `tools/device_io/factory.py` 是无界面的 STM32G070/Nebulizer 烧录适配器，
不是通用任意芯片烧录器。其他硬件须提供正确目标算法和镜像验证，不能只改界面芯片名称。

## 测试

```powershell
# 在仓库根目录，先生成协议测试用快照
python tools/run_tests.py
python tools/workbench/tests/test_backend.py
python tests/debug/test_factory_flash.py
python tests/debug/test_debug_host.py
python tests/debug/test_daplink_transport.py
python tests/boot/test_upgrade_host.py

cd tools/workbench
npm test
npm run build
npm run test:desktop
# 完成 npm run dist 后，验证实际发布目录中的程序及内置后台
node tests/packaged.cjs
```

桌面测试会启动真实 Electron，检查沙箱、preload、Python RPC、文件校验、模块切换、
参数草稿、确认拦截和曲线绘制；仅将合成快照注入渲染器，不执行设备连接或烧写。
截图保存在 `test-results/`。合成快照不会作为真实控制器的停止确认。
后台单元测试使用假串口验证真实帧编解码及命令失败后停止后续批次。
当前未执行实机 SWD、实际串口控制、升级或工厂烧录，发布包也仍需实机验收。

## Windows 发布包

```powershell
cd tools/workbench
npm ci
npm run dist
```

`dist` 完成 TypeScript/Vite 构建、PyInstaller 无 Qt 后台打包和 Electron portable EXE 生成。
需先安装本页依赖和 DFP。DFP 随后台打包，不依赖目标电脑的 pyOCD 缓存。
产物：`release/Nebulizer-Workbench-1.0.0-x64.exe`。
`release/win-unpacked/` 为可直接运行的解包目录，也可用于部署检查。
可执行文件未配置发布签名证书；不会生成密钥或替换已有信任锚。

设备固件仍使用 [Boot + App 工厂 HEX / App 升级包流程](../../docs/FirmwareUpgrade.md)，
Electron 应用的 EXE 与 MCU 固件包是两类独立产物。
