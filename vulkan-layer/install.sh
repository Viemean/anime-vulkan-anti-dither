#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TARGET_DIR="${XDG_DATA_HOME:-$HOME/.local/share}/vulkan/implicit_layer.d"
mkdir -p "${TARGET_DIR}"

INSTALLED_LIB="${TARGET_DIR}/libwuwa_antidither_layer.so"
INSTALLED_JSON="${TARGET_DIR}/VkLayer_WUWA_antidither.json"

LIB_SRC=""
CANDIDATES=(
    "${SCRIPT_DIR}/build-x86_64/libwuwa_antidither_layer.so"
    "${SCRIPT_DIR}/build/libwuwa_antidither_layer.so"
    "${SCRIPT_DIR}/dist/libwuwa_antidither_layer.so"
    "${SCRIPT_DIR}/libwuwa_antidither_layer.so"
)

NEWEST_TIME=0
for path in "${CANDIDATES[@]}"; do
    if [[ -f "${path}" ]]; then
        MOD_TIME=$(stat -c %Y "${path}" 2>/dev/null || stat -f %m "${path}" 2>/dev/null || echo 0)
        if (( MOD_TIME > NEWEST_TIME )); then
            NEWEST_TIME="${MOD_TIME}"
            LIB_SRC="${path}"
        fi
    fi
done

if [[ -z "${LIB_SRC}" ]]; then
    "${SCRIPT_DIR}/build.sh"
    LIB_SRC="${SCRIPT_DIR}/build/libwuwa_antidither_layer.so"
fi

cp -f "${LIB_SRC}" "${INSTALLED_LIB}"

if [[ -f "${SCRIPT_DIR}/manifest/VkLayer_WUWA_antidither.json.in" ]]; then
    sed "s|@LIBRARY_PATH@|${INSTALLED_LIB}|g" \
        "${SCRIPT_DIR}/manifest/VkLayer_WUWA_antidither.json.in" \
        > "${INSTALLED_JSON}"
elif [[ -f "${SCRIPT_DIR}/VkLayer_WUWA_antidither.json" ]]; then
    sed "s|\"./libwuwa_antidither_layer.so\"|\"${INSTALLED_LIB}\"|g; s|@LIBRARY_PATH@|${INSTALLED_LIB}|g" \
        "${SCRIPT_DIR}/VkLayer_WUWA_antidither.json" \
        > "${INSTALLED_JSON}"
fi

CONFIG_DIR="${XDG_CONFIG_HOME:-$HOME/.config}/anti_dither"
CONFIG_FILE="${CONFIG_DIR}/rules.conf"
mkdir -p "${CONFIG_DIR}"

if [[ -f "${SCRIPT_DIR}/manifest/rules.conf" ]]; then
    if [[ -f "${CONFIG_FILE}" ]]; then
        cp -f "${CONFIG_FILE}" "${CONFIG_FILE}.old"
    fi
    cp -f "${SCRIPT_DIR}/manifest/rules.conf" "${CONFIG_FILE}"
fi

echo "==> 隐式层安装完成"
echo "  - 库文件:   ${INSTALLED_LIB}"
echo "  - 描述文件: ${INSTALLED_JSON}"
echo "  - 配置文件: ${CONFIG_FILE}"
