# 验证记录 · 2026-10-16

环境：Windows 11 x64，Node 24.15.0，Python 3.12.10，Electron 44.7.2。

| 检查 | 结果 | 范围 |
|---|---|---|
| `npm run build` | 通过 | TypeScript 和 Vite 生产构建 |
| `npm test` | 6 项通过 | 环形缓存、保留尖峰抽样、CSV、配置顺序及 PID 编码 |
| `python tools/workbench/tests/test_backend.py` | 3 项通过 | 无硬件假串口、真实帧/快照编解码、作业互斥、确认、NACK 停止批次 |
| `python tests/debug/test_factory_flash.py` | 6 项通过 | 镜像检查、J-Link/DAPLink mock、完整读回、复位顺序、失败拦截 |
| `python tests/debug/test_debug_host.py` | 24 项通过 | 原 SWD 邮箱会话、参数、曲线及界面回归 |
| `python tests/debug/test_daplink_transport.py` | 3 项通过 | DAPLink 配置、写入及异常清理 |
| `python tests/boot/test_upgrade_host.py` | 9 项通过 | 升级包及真实 C Boot 模拟器协议往返、重传、取消和 GUI 回归 |
| `npm run test:desktop` | 通过 | 真实 Electron 进程、隔离 preload、Python RPC、模块切换、草稿保留、确认拦截、合成采样曲线 |
| `npm run dist` | 通过 | 内置无 Qt 后台、Python 运行时、G0 DFP、Windows portable EXE |
| `node tests/packaged.cjs` | 通过 | 实际发布目录的 Electron、内置后台启动、串口枚举、工厂镜像解析；外部 Python 路径刻意设为不存在 |

发布文件：`release/Nebulizer-Workbench-1.0.0-x64.exe`。
未使用硬件连接执行控制或擦写，未进行实机升级/烧录验收。
串口枚举和本地固件解析不代表实际控制器通信已验证。
桌面测试中的合成数据仅用于渲染测试，不作为安装前真实输出停止状态。

截图：`test-results/desktop-offline.png`、`desktop-samples.png`、`packaged-factory.png`。
EXE、截图和构建缓存为本机构建产物，已通过 `.gitignore` 排除。
