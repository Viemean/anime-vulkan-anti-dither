#pragma once

#include "zzz_vkd3d.h"
#include "zzz_dxvk.h"
#include <string_view>

namespace zzz_layer {

  enum class ShaderStage {
    Unknown,
    Vertex,
    Fragment,
    Compute
  };

  /**
   * @brief 精确识别着色器是由 DXVK (DX11) 还是 VKD3D-Proton (DX12) 翻译生成
   */
  inline bool is_dxvk_translation_layer(const uint32_t* spirv_code, size_t word_count) {
    if (word_count < 5)
      return true;

    // SPIR-V Header Word 2: (Tool ID << 16) | Tool Version
    // Khronos 注册 Tool ID: VKD3D-Shader = 30017 (0x7541)
    uint32_t generator = spirv_code[2];
    uint16_t tool_id = static_cast<uint16_t>(generator >> 16);
    if (tool_id == 30017) {
      return false; // VKD3D-Proton
    }

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
        if (str.find(".dxil") != std::string_view::npos || str.find(".dxbc") != std::string_view::npos) {
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

        if (name == "RootConstants" || name == "registers" || name == "demote_cond") {
          return false;
        }
      }
      i += length;
    }

    return true; // 默认回退 DXVK
  }

  inline ShaderStage detect_shader_stage(const uint32_t* spirv_code, size_t word_count) {
    if (!spirv_code || word_count < 5)
      return ShaderStage::Unknown;

    size_t i = 5;
    while (i < word_count) {
      uint32_t word = spirv_code[i];
      uint16_t opcode = word & 0xFFFF;
      uint16_t length = (word >> 16) & 0xFFFF;
      if (length == 0 || (i + length) > word_count)
        break;

      if (opcode == 15 /* OpEntryPoint */ && length >= 3) {
        uint32_t exec_model = spirv_code[i + 1];
        switch (exec_model) {
          case 0: return ShaderStage::Vertex;
          case 4: return ShaderStage::Fragment;
          case 5: return ShaderStage::Compute;
          default: return ShaderStage::Unknown;
        }
      }
      i += length;
    }
    return ShaderStage::Unknown;
  }

  /**
   * @brief 绝区零着色器反虚化分发入口
   */
  inline void process_spirv_anti_dither(uint32_t* spirv_code, size_t word_count) {
    bool is_dxvk = is_dxvk_translation_layer(spirv_code, word_count);
    auto stage = detect_shader_stage(spirv_code, word_count);

    if (is_dxvk) {
      if (stage == ShaderStage::Fragment) {
        zzz_dxvk::process_spirv_anti_dither(spirv_code, word_count);
      } else if (stage == ShaderStage::Vertex) {
        zzz_dxvk::process_vertex_shader(spirv_code, word_count);
      }
    } else {
      if (stage == ShaderStage::Fragment) {
        zzz_vkd3d::process_fragment_shader(spirv_code, word_count);
      }
    }
  }

} // namespace zzz_layer
