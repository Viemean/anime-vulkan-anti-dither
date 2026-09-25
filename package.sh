#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="${SCRIPT_DIR}"
VULKAN_DIR="${ROOT_DIR}/vulkan-layer"
BUILD_DIR="${VULKAN_DIR}/build"
OUT_DIR="${ROOT_DIR}/dist/packages"

VERSION="1.0.0"
if [[ -f "${VULKAN_DIR}/meson.build" ]]; then
    PARSED_VER=$(grep -E "version\s*:\s*'" "${VULKAN_DIR}/meson.build" | head -n1 | sed -E "s/.*version\s*:\s*'([^']+)'.*/\1/")
    if [[ -n "${PARSED_VER}" ]]; then
        VERSION="${PARSED_VER}"
    fi
fi

if git describe --tags --exact-match 2>/dev/null; then
    GIT_TAG=$(git describe --tags --exact-match)
    VERSION="${GIT_TAG#v}"
fi

echo "==> [1/4] 开始构建 x86_64 平台 Vulkan Layer (版本: ${VERSION})..."

"${VULKAN_DIR}/build.sh"
ninja -C "${BUILD_DIR}" test

rm -rf "${OUT_DIR}"
mkdir -p "${OUT_DIR}"

TMP_PKG_DIR=$(mktemp -d -t anti-dither-pkg-XXXXXX)
trap 'rm -rf "${TMP_PKG_DIR}"' EXIT

PORTABLE_DIR="${TMP_PKG_DIR}/vulkan-anti-dither"
mkdir -p "${PORTABLE_DIR}"

cp -f "${BUILD_DIR}/libwuwa_antidither_layer.so" "${PORTABLE_DIR}/"
sed 's|@LIBRARY_PATH@|./libwuwa_antidither_layer.so|g' \
    "${VULKAN_DIR}/manifest/VkLayer_WUWA_antidither.json.in" \
    > "${PORTABLE_DIR}/VkLayer_WUWA_antidither.json"
cp -f "${VULKAN_DIR}/install.sh" "${PORTABLE_DIR}/"
cp -f "${VULKAN_DIR}/uninstall.sh" "${PORTABLE_DIR}/"
cp -f "${ROOT_DIR}/README.md" "${PORTABLE_DIR}/"
chmod +x "${PORTABLE_DIR}/install.sh" "${PORTABLE_DIR}/uninstall.sh"

echo "==> [2/4] 打包通用便携压缩包 (.tar.gz / .tar.zst)..."
tar -czf "${OUT_DIR}/vulkan-anti-dither-${VERSION}-x86_64.tar.gz" -C "${TMP_PKG_DIR}" vulkan-anti-dither
ln -sf "vulkan-anti-dither-${VERSION}-x86_64.tar.gz" "${OUT_DIR}/vulkan-anti-dither-latest-x86_64.tar.gz"

if command -v zstd >/dev/null 2>&1; then
    tar -I 'zstd -19 -T0' -cf "${OUT_DIR}/vulkan-anti-dither-${VERSION}-x86_64.tar.zst" -C "${TMP_PKG_DIR}" vulkan-anti-dither
    ln -sf "vulkan-anti-dither-${VERSION}-x86_64.tar.zst" "${OUT_DIR}/vulkan-anti-dither-latest-x86_64.tar.zst"
fi

echo "==> [3/4] 构建 Debian 安装包 (.deb)..."
DEB_ROOT="${TMP_PKG_DIR}/deb-root"
mkdir -p "${DEB_ROOT}/DEBIAN"
mkdir -p "${DEB_ROOT}/usr/lib/x86_64-linux-gnu"
mkdir -p "${DEB_ROOT}/usr/lib"
mkdir -p "${DEB_ROOT}/usr/share/vulkan/implicit_layer.d"
mkdir -p "${DEB_ROOT}/usr/share/doc/vulkan-anti-dither"

cp -f "${BUILD_DIR}/libwuwa_antidither_layer.so" "${DEB_ROOT}/usr/lib/x86_64-linux-gnu/"
ln -sf "/usr/lib/x86_64-linux-gnu/libwuwa_antidither_layer.so" "${DEB_ROOT}/usr/lib/libwuwa_antidither_layer.so"

sed 's|@LIBRARY_PATH@|libwuwa_antidither_layer.so|g' \
    "${VULKAN_DIR}/manifest/VkLayer_WUWA_antidither.json.in" \
    > "${DEB_ROOT}/usr/share/vulkan/implicit_layer.d/VkLayer_WUWA_antidither.json"

cp -f "${ROOT_DIR}/README.md" "${DEB_ROOT}/usr/share/doc/vulkan-anti-dither/"

# 计算已安装大小 (KiB)
INSTALLED_SIZE=$(du -sk "${DEB_ROOT}" | cut -f1)

cat > "${DEB_ROOT}/DEBIAN/control" <<EOF
Package: vulkan-anti-dither
Version: ${VERSION}
Section: graphics
Priority: optional
Architecture: amd64
Maintainer: Viemean <https://github.com/Viemean/anime-vulkan-anti-dither>
Installed-Size: ${INSTALLED_SIZE}
Depends: libc6 (>= 2.34), libvulkan1
Description: Vulkan Anti-Dither Layer for Anime Games
 Universal Vulkan layer intercepting camera dithering discard instructions at runtime.
 Compatible with DXVK, VKD3D-Proton and native Vulkan games on Linux.
EOF

cat > "${DEB_ROOT}/DEBIAN/postinst" <<'EOF'
#!/bin/sh
set -e
ldconfig >/dev/null 2>&1 || true
exit 0
EOF
chmod 0755 "${DEB_ROOT}/DEBIAN/postinst"

cat > "${DEB_ROOT}/DEBIAN/postrm" <<'EOF'
#!/bin/sh
set -e
ldconfig >/dev/null 2>&1 || true
exit 0
EOF
chmod 0755 "${DEB_ROOT}/DEBIAN/postrm"

if command -v dpkg-deb >/dev/null 2>&1; then
    dpkg-deb --build --root-owner-group "${DEB_ROOT}" "${OUT_DIR}/vulkan-anti-dither_${VERSION}_amd64.deb"
    ln -sf "vulkan-anti-dither_${VERSION}_amd64.deb" "${OUT_DIR}/vulkan-anti-dither_latest_amd64.deb"
else
    echo "警告: 系统未找到 dpkg-deb 工具，跳过 .deb 构建"
fi

echo "==> [4/4] 打包完成！输出产物："
ls -lh "${OUT_DIR}"
