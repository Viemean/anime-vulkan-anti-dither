#pragma once

#include "dna_vkd3d.h"
#include "dna_dxvk.h"

#include <string_view>

namespace dna_layer {

  inline bool is_dxvk_translation_layer(const uint32_t* spirv_code, size_t word_count) {
    if (word_count < 5)
      return false;

    // SPIR-V Header Word 2: (Tool ID << 16) | Tool Version
    // Khronos 注册 Tool ID: VKD3D-Shader = 30017 (0x7541)
    uint32_t generator = spirv_code[2];
    uint16_t tool_id = static_cast<uint16_t>(generator >> 16);
    if (tool_id == 30017) {
      return false; // 确定为 VKD3D-Proton (DirectX 12)
    }

    // 扫描 OpString / OpName 辅助特征
    size_t i = 5;
    while (i < word_count) {
      uint32_t word = spirv_code[i];
      uint16_t opcode = word & 0xFFFF;
      uint16_t length = (word >> 16) & 0xFFFF;
      if (length == 0 || (i + length) > word_count)
        break;

      if (opcode == 54 /* OpFunction */) {
        break;
      }

      if (opcode == 7 /* OpString */ && length >= 3) {
        size_t max_bytes = static_cast<size_t>(length - 2) * sizeof(uint32_t);
        const char* str_ptr = reinterpret_cast<const char*>(&spirv_code[i + 2]);
        size_t str_len = 0;
        while (str_len < max_bytes && str_ptr[str_len] != '\0') str_len++;
        std::string_view str(str_ptr, str_len);
        if (str.find(".dxil") != std::string_view::npos) {
          return false;
        }
      }

      if (opcode == 5 /* OpName */ && length >= 3) {
        size_t max_bytes = static_cast<size_t>(length - 2) * sizeof(uint32_t);
        const char* str_ptr = reinterpret_cast<const char*>(&spirv_code[i + 2]);
        size_t str_len = 0;
        while (str_len < max_bytes && str_ptr[str_len] != '\0') str_len++;
        std::string_view name(str_ptr, str_len);

        if (name == "cb0" || name == "cb1" || name == "cb2" || name == "cb3" ||
            name == "icb" || name == "dp2_f32" || name == "cvt_f32_u32" ||
            name == "cb0_buf" || name == "cb1_buf" || name == "icb_buf" || name == "u_info") {
          return true;
        }

        if (name == "RootConstants" || name == "registers") {
          return false;
        }
      }
      i += length;
    }

    return true; // 默认非 VKD3D 则走 DXVK
  }

  inline void process_spirv_anti_dither(uint32_t* spirv_code, size_t word_count) {
    if (!spirv_code || word_count < 5)
      return;

    if (!game_logger::is_active())
      return;

    if (spirv_code[0] != dna_vkd3d::SPV_HEADER_MAGIC)
      return;

    bool is_dxvk = is_dxvk_translation_layer(spirv_code, word_count);

    if (is_dxvk) {
      dna_dxvk::process_spirv(spirv_code, word_count);
    } else {
      dna_vkd3d::process_spirv(spirv_code, word_count);
    }
  }

} // namespace dna_layer

