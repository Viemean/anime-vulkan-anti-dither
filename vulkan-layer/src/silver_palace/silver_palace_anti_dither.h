#pragma once

#include "../logger.h"
#include "silver_palace_vkd3d.h"

#include <cstddef>
#include <cstdint>

namespace silver_palace_layer {

  using silver_palace_vkd3d::SPV_HEADER_MAGIC;
  using silver_palace_vkd3d::SPV_OP_ENTRY_POINT;

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

  /**
   * @brief 《白银之城》(Silver Palace) 着色器反虚化分发入口。
   *
   * @param spirv_code 欲处理的 SPIR-V 32 位字数组指针。
   * @param word_count 字节码包含的 32 位字数量。
   */
  inline void process_spirv_anti_dither(uint32_t* spirv_code, size_t word_count) {
    if (!spirv_code || word_count < 5)
      return;

    if (!game_logger::is_active())
      return;

    if (spirv_code[0] != SPV_HEADER_MAGIC)
      return;

    ShaderStage stage = detect_shader_stage(spirv_code, word_count);

    if (stage == ShaderStage::Fragment) {
      silver_palace_vkd3d::process_spirv_anti_dither(spirv_code, word_count);
    }
  }

} // namespace silver_palace_layer
