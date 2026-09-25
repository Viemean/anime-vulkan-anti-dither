#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DIST_LIB="${SCRIPT_DIR}/dist/libwuwa_antidither_layer.so"
BUILD_LIB="${SCRIPT_DIR}/build/libwuwa_antidither_layer.so"

LIB_SRC=""
if [[ -f "${DIST_LIB}" ]]; then
    LIB_SRC="${DIST_LIB}"
elif [[ -f "${BUILD_LIB}" ]]; then
    LIB_SRC="${BUILD_LIB}"
else
    echo "==> 未找到已编译库，正在自动触发编译..."
    "${SCRIPT_DIR}/build.sh"
    LIB_SRC="${DIST_LIB}"
fi

LAYER_TYPE="implicit_layer.d"
if [[ "${1:-}" == "--explicit" || "${1:-}" == "-e" ]]; then
    LAYER_TYPE="explicit_layer.d"
    echo "==> [安装模式] 显式层 (需启动项手动指定 VK_INSTANCE_LAYERS=VK_LAYER_WUWA_antidither)"
else
    echo "==> [安装模式] 隐式层 (系统自动识别《鸣潮》游戏主进程并按需激活，对其他游戏0影响)"
fi

TARGET_DIR="${XDG_DATA_HOME:-$HOME/.local/share}/vulkan/${LAYER_TYPE}"
mkdir -p "${TARGET_DIR}"

INSTALLED_LIB="${TARGET_DIR}/libwuwa_antidither_layer.so"
INSTALLED_JSON="${TARGET_DIR}/VkLayer_WUWA_antidither.json"

cp "${LIB_SRC}" "${INSTALLED_LIB}"

sed "s|@LIBRARY_PATH@|${INSTALLED_LIB}|g" \
    "${SCRIPT_DIR}/manifest/VkLayer_WUWA_antidither.json.in" \
    > "${INSTALLED_JSON}"

echo "==> 安装成功！"
echo "  - 库文件:   ${INSTALLED_LIB}"
echo "  - 描述文件: ${INSTALLED_JSON}"
echo ""
if [[ "${LAYER_TYPE}" == "implicit_layer.d" ]]; then
    echo "提示: 无需在 Steam 或 Lutris 做任何额外配置，直接启动游戏即可生效。"
else
    echo "提示: 请在启动项中加入: VK_INSTANCE_LAYERS=VK_LAYER_WUWA_antidither %command%"
fi
