# Nebulizer Host PyQt

PyQt6 desktop application for Nebulizer mainboard monitoring and control through `USART1`.

## Install

```sh
cd /Users/zq/project/Nebulizer/software/9C-Nebulizer-main/tools/nebulizer_host_pyqt
/Users/zq/zephyrproject/.venv/bin/python -m pip install -r requirements.txt
```

## Run

Windows PowerShell:

```powershell
cd D:\zephyrproject\9C-Nebulizer-main\tools
.\run_host.ps1
```

Or from this directory:

```powershell
.\run.ps1
```

Linux/macOS:

```sh
cd /Users/zq/project/Nebulizer/software/9C-Nebulizer-main/tools/nebulizer_host_pyqt
/Users/zq/zephyrproject/.venv/bin/python app.py
```

Or use the launcher:

```sh
cd /Users/zq/project/Nebulizer/software/9C-Nebulizer-main/tools/nebulizer_host_pyqt
./run.sh
```

Do not run `serial_client.py` directly. It is a library module used by `app.py`.

## Features

- USART1 live monitoring
- treatment control
- independent PB10 preheat PID, PB11 outlet PID, and outlet-temperature fan PID tuning
- maintenance mode manual fan/mist/heat control
- fault history
- outlet/kettle/target temperature trend
- parameter JSON import/export
- protocol log and ACK/NACK trace

## Read Behavior

- connect success: automatic one-time read of `Config / Preheat PID / Outlet PID / Status / Runtime / Maintenance`
- connected state automatic refresh:
  - `Status` every `500 ms`
  - `Runtime` every `500 ms`
  - `Maintenance` every `1000 ms`
- `Config` and all PID groups stay manual to avoid overwriting local edits before `Apply`
- heartbeat remains automatic so the host link stays alive
- `Read All` is still available when you want an immediate manual refresh
