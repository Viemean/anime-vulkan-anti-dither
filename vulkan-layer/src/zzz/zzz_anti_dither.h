#pragma once

#include "zzz_vkd3d.h"

namespace zzz_layer {

  enum class ShaderStage {
    Unknown,
    Vertex,
    Fragment,
    Compute
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
    auto stage = detect_shader_stage(spirv_code, word_count);
    if (stage == ShaderStage::Fragment) {
      zzz_vkd3d::process_fragment_shader(spirv_code, word_count);
    }
  }

} // namespace zzz_layer
