#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PYTHON_BIN="/Users/zq/zephyrproject/.venv/bin/python"

if [[ ! -x "${PYTHON_BIN}" ]]; then
  echo "Python venv not found: ${PYTHON_BIN}" >&2
  echo "Run with a Python that has PyQt6 and pyserial installed." >&2
  exit 1
fi

exec "${PYTHON_BIN}" "${SCRIPT_DIR}/app.py"
