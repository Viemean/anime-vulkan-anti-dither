#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="${SCRIPT_DIR}"
VULKAN_DIR="${ROOT_DIR}/vulkan-layer"
OUT_DIR="${ROOT_DIR}/dist/packages"

# 1. 解析版本号 (优先读取环境变量，其次匹配精确 tag，否则默认 1.0.0)
if [[ -z "${VERSION:-}" ]]; then
    if GIT_TAG=$(git -C "${ROOT_DIR}" describe --tags --exact-match 2>/dev/null); then
        VERSION="${GIT_TAG#v}"
    else
        VERSION="1.0.0"
    fi
fi

HOST_ARCH="$(uname -m)"
case "${HOST_ARCH}" in
    x86_64)  NATIVE_ARCH="x86_64" ;;
    aarch64|arm64) NATIVE_ARCH="aarch64" ;;
    *) NATIVE_ARCH="${HOST_ARCH}" ;;
esac

# 2. 解析目标架构列表
TARGET_ARCHES=()
if [[ $# -eq 0 || "$1" == "all" || "$1" == "--all" ]]; then
    TARGET_ARCHES=("x86_64" "aarch64")
else
    for arg in "$@"; do
        case "$arg" in
            x86_64|amd64)   TARGET_ARCHES+=("x86_64") ;;
            aarch64|arm64)  TARGET_ARCHES+=("aarch64") ;;
            native)         TARGET_ARCHES+=("${NATIVE_ARCH}") ;;
            *)
                echo "未知目标架构: ${arg}，仅支持 x86_64, aarch64(arm64), all, native"
                exit 1
                ;;
        esac
    done
fi

mkdir -p "${OUT_DIR}"

build_and_package_arch() {
    local target="$1"
    local deb_arch=""
    local deb_libdir=""
    local cross_needed=false

    case "${target}" in
        x86_64)
            deb_arch="amd64"
            deb_libdir="usr/lib/x86_64-linux-gnu"
            if [[ "${NATIVE_ARCH}" != "x86_64" ]]; then
                cross_needed=true
            fi
            ;;
        aarch64)
            deb_arch="arm64"
            deb_libdir="usr/lib/aarch64-linux-gnu"
            if [[ "${NATIVE_ARCH}" != "aarch64" ]]; then
                cross_needed=true
            fi
            ;;
        *)
            echo "不支持的架构: ${target}"
            return 1
            ;;
    esac

    local build_dir="${VULKAN_DIR}/build-${target}"
    echo ""
    echo "=========================================================="
    echo "==> 开始构建与打包: ${target} (Debian: ${deb_arch}, 版本: ${VERSION})"
    echo "=========================================================="

    local meson_args=(
        "--buildtype" "release"
        "--strip"
        "-Dlogging=true"
    )

    if [[ "${cross_needed}" == "true" ]]; then
        mkdir -p "${build_dir}"
        local cross_file="${build_dir}/cross-file.ini"

        if [[ "${target}" == "aarch64" ]]; then
            if ! command -v aarch64-linux-gnu-g++ >/dev/null 2>&1; then
                echo "==> [跳过] 当前环境未检测到 aarch64-linux-gnu-g++ 交叉编译器。"
                echo "    如需本地构建 aarch64 包，请先安装交叉编译工具链 (如 Debian/Ubuntu: g++-aarch64-linux-gnu 或 Arch: aarch64-linux-gnu-gcc)。"
                return 0
            fi

            cat > "${cross_file}" <<EOF
[binaries]
c = 'aarch64-linux-gnu-gcc'
cpp = 'aarch64-linux-gnu-g++'
ar = 'aarch64-linux-gnu-ar'
strip = 'aarch64-linux-gnu-strip'
pkg-config = 'aarch64-linux-gnu-pkg-config'

[host_machine]
system = 'linux'
cpu_family = 'aarch64'
cpu = 'aarch64'
endian = 'little'
EOF
            meson_args+=("--cross-file" "${cross_file}")
        elif [[ "${target}" == "x86_64" ]]; then
            if ! command -v x86_64-linux-gnu-g++ >/dev/null 2>&1; then
                echo "==> [跳过] 当前环境未检测到 x86_64-linux-gnu-g++ 交叉编译器。"
                return 0
            fi

            cat > "${cross_file}" <<EOF
[binaries]
c = 'x86_64-linux-gnu-gcc'
cpp = 'x86_64-linux-gnu-g++'
ar = 'x86_64-linux-gnu-ar'
strip = 'x86_64-linux-gnu-strip'
pkg-config = 'x86_64-linux-gnu-pkg-config'

[host_machine]
system = 'linux'
cpu_family = 'x86_64'
cpu = 'x86_64'
endian = 'little'
EOF
            meson_args+=("--cross-file" "${cross_file}")
        fi
    fi

    # 配置构建目录
    if [[ ! -d "${build_dir}" ]]; then
        meson setup "${build_dir}" "${VULKAN_DIR}" "${meson_args[@]}"
    else
        meson setup --reconfigure "${build_dir}" "${VULKAN_DIR}" "${meson_args[@]}"
    fi

    # 编译动态库
    ninja -C "${build_dir}" libwuwa_antidither_layer.so

    # 原生架构跑全量单元测试
    if [[ "${cross_needed}" == "false" ]]; then
        echo "==> 执行原生架构单元测试..."
        ninja -C "${build_dir}" test
    fi

    # 准备打包临时目录
    local tmp_pkg_dir
    tmp_pkg_dir=$(mktemp -d -t "anti-dither-${target}-XXXXXX")
    trap 'rm -rf "${tmp_pkg_dir}"' RETURN

    local portable_dir="${tmp_pkg_dir}/vulkan-anti-dither"
    mkdir -p "${portable_dir}"

    cp -f "${build_dir}/libwuwa_antidither_layer.so" "${portable_dir}/"
    sed 's|@LIBRARY_PATH@|./libwuwa_antidither_layer.so|g' \
        "${VULKAN_DIR}/manifest/VkLayer_WUWA_antidither.json.in" \
        > "${portable_dir}/VkLayer_WUWA_antidither.json"
    cp -f "${VULKAN_DIR}/install.sh" "${portable_dir}/"
    cp -f "${VULKAN_DIR}/uninstall.sh" "${portable_dir}/"
    if [[ -f "${ROOT_DIR}/README.md" ]]; then
        cp -f "${ROOT_DIR}/README.md" "${portable_dir}/"
    fi
    chmod +x "${portable_dir}/install.sh" "${portable_dir}/uninstall.sh"

    # 打包通用便携包
    echo "==> 打包通用便携包 (.tar.gz / .tar.zst)..."
    tar -czf "${OUT_DIR}/vulkan-anti-dither-${VERSION}-${target}.tar.gz" -C "${tmp_pkg_dir}" vulkan-anti-dither

    if command -v zstd >/dev/null 2>&1; then
        tar -I 'zstd -19 -T0' -cf "${OUT_DIR}/vulkan-anti-dither-${VERSION}-${target}.tar.zst" -C "${tmp_pkg_dir}" vulkan-anti-dither
    fi

    # 构建 Debian 安装包
    echo "==> 构建 Debian 安装包 (${deb_arch}.deb)..."
    local deb_root="${tmp_pkg_dir}/deb-root"
    mkdir -p "${deb_root}/DEBIAN"
    mkdir -p "${deb_root}/${deb_libdir}"
    mkdir -p "${deb_root}/usr/lib"
    mkdir -p "${deb_root}/usr/share/vulkan/implicit_layer.d"
    mkdir -p "${deb_root}/usr/share/doc/vulkan-anti-dither"

    cp -f "${build_dir}/libwuwa_antidither_layer.so" "${deb_root}/${deb_libdir}/"
    ln -sf "/${deb_libdir}/libwuwa_antidither_layer.so" "${deb_root}/usr/lib/libwuwa_antidither_layer.so"

    sed 's|@LIBRARY_PATH@|libwuwa_antidither_layer.so|g' \
        "${VULKAN_DIR}/manifest/VkLayer_WUWA_antidither.json.in" \
        > "${deb_root}/usr/share/vulkan/implicit_layer.d/VkLayer_WUWA_antidither.json"

    if [[ -f "${ROOT_DIR}/README.md" ]]; then
        cp -f "${ROOT_DIR}/README.md" "${deb_root}/usr/share/doc/vulkan-anti-dither/"
    fi

    local installed_size
    installed_size=$(du -sk "${deb_root}" | cut -f1)

    cat > "${deb_root}/DEBIAN/control" <<EOF
Package: vulkan-anti-dither
Version: ${VERSION}
Section: graphics
Priority: optional
Architecture: ${deb_arch}
Maintainer: Viemean <https://github.com/Viemean/anime-vulkan-anti-dither>
Installed-Size: ${installed_size}
Depends: libc6 (>= 2.34), libvulkan1
Description: Vulkan Anti-Dither Layer for Anime Games
 Universal Vulkan layer intercepting camera dithering discard instructions at runtime.
 Compatible with DXVK, VKD3D-Proton and native Vulkan games on Linux (${target}).
EOF

    cat > "${deb_root}/DEBIAN/postinst" <<'EOF'
#!/bin/sh
set -e
ldconfig >/dev/null 2>&1 || true
exit 0
EOF
    chmod 0755 "${deb_root}/DEBIAN/postinst"

    cat > "${deb_root}/DEBIAN/postrm" <<'EOF'
#!/bin/sh
set -e
ldconfig >/dev/null 2>&1 || true
exit 0
EOF
    chmod 0755 "${deb_root}/DEBIAN/postrm"

    if command -v dpkg-deb >/dev/null 2>&1; then
        dpkg-deb --build --root-owner-group "${deb_root}" "${OUT_DIR}/vulkan-anti-dither_${VERSION}_${deb_arch}.deb"
    else
        echo "警告: 系统未找到 dpkg-deb 工具，跳过 .deb 构建"
    fi

    rm -rf "${tmp_pkg_dir}"
    trap - RETURN
    echo "==> [成功] 完成 ${target} 架构打包"
}

# 循环构建各架构
for arch in "${TARGET_ARCHES[@]}"; do
    build_and_package_arch "${arch}"
done

# 如果原生是 x86_64，同步一份最新便携文件到 dist/ 根目录
if [[ -f "${VULKAN_DIR}/build-x86_64/libwuwa_antidither_layer.so" ]]; then
    mkdir -p "${ROOT_DIR}/dist"
    cp -f "${VULKAN_DIR}/build-x86_64/libwuwa_antidither_layer.so" "${ROOT_DIR}/dist/libwuwa_antidither_layer.so"
    sed 's|@LIBRARY_PATH@|./libwuwa_antidither_layer.so|g' \
        "${VULKAN_DIR}/manifest/VkLayer_WUWA_antidither.json.in" \
        > "${ROOT_DIR}/dist/VkLayer_WUWA_antidither.json"
elif [[ -f "${VULKAN_DIR}/build/libwuwa_antidither_layer.so" ]]; then
    mkdir -p "${ROOT_DIR}/dist"
    cp -f "${VULKAN_DIR}/build/libwuwa_antidither_layer.so" "${ROOT_DIR}/dist/libwuwa_antidither_layer.so"
    sed 's|@LIBRARY_PATH@|./libwuwa_antidither_layer.so|g' \
        "${VULKAN_DIR}/manifest/VkLayer_WUWA_antidither.json.in" \
        > "${ROOT_DIR}/dist/VkLayer_WUWA_antidither.json"
fi

echo ""
echo "=========================================================="
echo "==> 所有发布产物归档完毕！目录列表 (${OUT_DIR}):"
echo "=========================================================="
ls -lh "${OUT_DIR}"
