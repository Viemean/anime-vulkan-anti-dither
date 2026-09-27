#pragma once

#include "../logger.h"
#include "tof_vkd3d.h"

#include <cstddef>
#include <cstdint>
#include <vector>
#include <string>

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

  inline void process_spirv_anti_dither(uint32_t* spirv_code, size_t word_count) {
    if (!spirv_code || word_count < 5)
      return;

    if (!game_logger::is_active())
      return;

    if (spirv_code[0] != SPV_HEADER_MAGIC)
      return;

    uint32_t shader_hash = game_logger::compute_spirv_hash(spirv_code, word_count);
    ShaderStage stage = detect_shader_stage(spirv_code, word_count);

    if (stage == ShaderStage::Fragment) {
      // 优先处理 VKD3D-Proton (DirectX 12)
      tof_vkd3d::process_spirv_anti_dither(spirv_code, word_count);
    } else if (stage == ShaderStage::Vertex && game_logger::g_dump_enabled) {
      game_logger::dump_shader_bundle(spirv_code, word_count,
                                      spirv_code, word_count,
                                      shader_hash, 0, 0,
                                      false, false);
      game_logger::log_msg("[反虚化驱动层-TOF-VKD3D] 捕获顶点着色器(VS): 0x%08x | 字长: %zu DW\n",
                           shader_hash, word_count);
    }
  }

} // namespace tof_layer
