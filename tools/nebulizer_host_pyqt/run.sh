#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PYTHON_BIN="${PYTHON_BIN:-python3}"

if ! command -v "${PYTHON_BIN}" >/dev/null 2>&1; then
  echo "Python not found: ${PYTHON_BIN}" >&2
  echo "Run with a Python that has PyQt6 and pyserial installed." >&2
  exit 1
fi

exec "${PYTHON_BIN}" "${SCRIPT_DIR}/app.py"
