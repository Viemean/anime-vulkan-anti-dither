#!/usr/bin/env bash
set -euo pipefail

TARGET_DIR="${XDG_DATA_HOME:-$HOME/.local/share}/vulkan/implicit_layer.d"

rm -f "${TARGET_DIR}/libwuwa_antidither_layer.so" "${TARGET_DIR}/VkLayer_WUWA_antidither.json"

echo "==> 隐式层已卸载 (${TARGET_DIR})"
