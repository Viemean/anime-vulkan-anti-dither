#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build"
DIST_DIR="${SCRIPT_DIR}/dist"

echo "==> [Vulkan Layer 构建] 开始构建 libwuwa_antidither_layer..."

LOGGING_OPT="true"
for arg in "$@"; do
    if [[ "$arg" == "--no-logging" || "$arg" == "--clean-release" ]]; then
        LOGGING_OPT="false"
        echo "==> [发行构建模式] 彻底清除所有日志与诊断开销 (-Dlogging=false)"
    fi
done

if [[ ! -d "${BUILD_DIR}" ]]; then
    meson setup "${BUILD_DIR}" "${SCRIPT_DIR}" --buildtype release --strip -Dlogging="${LOGGING_OPT}"
else
    meson setup --reconfigure "${BUILD_DIR}" "${SCRIPT_DIR}" --buildtype release --strip -Dlogging="${LOGGING_OPT}"
fi

ninja -C "${BUILD_DIR}"

mkdir -p "${DIST_DIR}"
cp "${BUILD_DIR}/libwuwa_antidither_layer.so" "${DIST_DIR}/libwuwa_antidither_layer.so"

sed 's|@LIBRARY_PATH@|./libwuwa_antidither_layer.so|g' \
    "${SCRIPT_DIR}/manifest/VkLayer_WUWA_antidither.json.in" \
    > "${DIST_DIR}/VkLayer_WUWA_antidither.json"

echo "==> 构建完成！产物已归档至: ${DIST_DIR}"
ls -lh "${DIST_DIR}"
