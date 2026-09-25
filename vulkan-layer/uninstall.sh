#!/usr/bin/env bash
set -euo pipefail

DATA_HOME="${XDG_DATA_HOME:-$HOME/.local/share}"
IMPLICIT_DIR="${DATA_HOME}/vulkan/implicit_layer.d"
EXPLICIT_DIR="${DATA_HOME}/vulkan/explicit_layer.d"

rm -f "${IMPLICIT_DIR}/libwuwa_antidither_layer.so" "${IMPLICIT_DIR}/VkLayer_WUWA_antidither.json"
rm -f "${EXPLICIT_DIR}/libwuwa_antidither_layer.so" "${EXPLICIT_DIR}/VkLayer_WUWA_antidither.json"

echo "==> Vulkan Layer 已完全卸载！"
