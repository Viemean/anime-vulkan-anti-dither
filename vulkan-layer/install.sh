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

CONFIG_STATUS="保留现有配置"
if [[ -f "${SCRIPT_DIR}/manifest/rules.conf" ]]; then
    if [[ ! -f "${CONFIG_FILE}" ]]; then
        cp -f "${SCRIPT_DIR}/manifest/rules.conf" "${CONFIG_FILE}"
        CONFIG_STATUS="初次安装 (已部署默认模板)"
    else
        # 智能对比添加：保留用户所有现有配置与数值，仅对比追加用户配置中缺失的新配置项
        if command -v python3 >/dev/null 2>&1; then
            APPEND_OUTPUT=$(python3 - "${CONFIG_FILE}" "${SCRIPT_DIR}/manifest/rules.conf" << 'EOF'
import sys
import re

user_path = sys.argv[1]
template_path = sys.argv[2]

try:
    with open(user_path, 'r', encoding='utf-8') as f:
        user_content = f.read()
    with open(template_path, 'r', encoding='utf-8') as f:
        template_content = f.read()

    def extract_keys(text):
        keys = set()
        for line in text.splitlines():
            m = re.match(r'^[#;]?\s*([a-zA-Z_][a-zA-Z0-9_]*)\s*=', line)
            if m:
                keys.add(m.group(1))
        return keys

    user_keys = extract_keys(user_content)
    sections = re.split(r'(?m)(?=^# -{40,})', template_content)
    to_append = []
    appended_keys = []

    for sec in sections:
        sec_keys = extract_keys(sec)
        missing = sec_keys - user_keys
        if missing:
            to_append.append(sec.strip())
            appended_keys.extend(sorted(list(missing)))
            user_keys.update(sec_keys)

    if to_append:
        with open(user_path, 'a', encoding='utf-8') as f:
            if not user_content.endswith('\n'):
                f.write('\n')
            f.write('\n' + '\n\n'.join(to_append) + '\n')
        print("对比追加新配置项: " + ", ".join(appended_keys))
    else:
        print("配置已是最新")
except Exception as e:
    print(f"对比跳过 ({e})")
EOF
)
            CONFIG_STATUS="保留现有配置 (${APPEND_OUTPUT})"
        else
            CONFIG_STATUS="保留现有配置 (已存在)"
        fi
    fi
fi

echo "==> 隐式层安装完成"
echo "  - 库文件:   ${INSTALLED_LIB}"
echo "  - 描述文件: ${INSTALLED_JSON}"
echo "  - 配置文件: ${CONFIG_FILE} [${CONFIG_STATUS}]"

