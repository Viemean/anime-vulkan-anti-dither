#pragma once

#include "../logger.h"
#include "tof_dxvk.h"
#include "tof_vkd3d.h"

#include <cstddef>
#include <cstdint>
#include <vector>
#include <string>
#include <string_view>

namespace tof_layer {

  using tof_vkd3d::SPV_HEADER_MAGIC;
  using tof_vkd3d::SPV_OP_ENTRY_POINT;

  enum class ShaderStage {
    Unknown,
    Vertex,
    Fragment,
    Compute,
    Other
  };

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

      if (opcode == SPV_OP_ENTRY_POINT && length >= 2) {
        uint32_t exec_model = spirv_code[i + 1];
        switch (exec_model) {
          case 0: return ShaderStage::Vertex;
          case 4: return ShaderStage::Fragment;
          case 5: return ShaderStage::Compute;
          default: return ShaderStage::Other;
        }
      }

      if (opcode == 54 /* OpFunction */) {
        break;
      }

      i += length;
    }

    return ShaderStage::Unknown;
  }

  inline bool is_dxvk_translation_layer(const uint32_t* spirv_code, size_t word_count) {
    if (word_count < 5)
      return true;

    // SPIR-V Header Word 2: (Tool ID << 16) | Tool Version
    // Khronos 注册 Tool ID: VKD3D-Shader = 30017 (0x7541)
    uint32_t generator = spirv_code[2];
    uint16_t tool_id = static_cast<uint16_t>(generator >> 16);
    if (tool_id == 30017) {
      return false; // 确定为 VKD3D-Proton (DirectX 12)
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

        if (name == "RootConstants" || name == "registers") {
          return false;
        }
      }
      i += length;
    }

    return true; // 默认若非明确 VKD3D 则走 DXVK
  }

  inline void process_spirv_anti_dither(uint32_t* spirv_code, size_t word_count) {
    if (!spirv_code || word_count < 5)
      return;

    if (!game_logger::is_active())
      return;

    if (spirv_code[0] != SPV_HEADER_MAGIC)
      return;

    uint32_t shader_hash = game_logger::compute_spirv_hash(spirv_code, word_count);
    ShaderStage stage = detect_shader_stage(spirv_code, word_count);
    bool is_dxvk = is_dxvk_translation_layer(spirv_code, word_count);

    if (stage == ShaderStage::Fragment) {
      if (is_dxvk) {
        tof_dxvk::process_spirv_anti_dither(spirv_code, word_count);
      } else {
        tof_vkd3d::process_spirv_anti_dither(spirv_code, word_count);
      }
    } else if (stage == ShaderStage::Vertex && game_logger::g_dump_enabled) {
      game_logger::dump_shader_bundle(spirv_code, word_count,
                                      spirv_code, word_count,
                                      shader_hash, 0, 0,
                                      false, false);
      const char* backend = is_dxvk ? "DXVK" : "VKD3D";
      game_logger::log_msg("[反虚化驱动层-TOF-%s] 捕获顶点着色器(VS): 0x%08x | 字长: %zu DW\n",
                           backend, shader_hash, word_count);
    }
  }

} // namespace tof_layer
