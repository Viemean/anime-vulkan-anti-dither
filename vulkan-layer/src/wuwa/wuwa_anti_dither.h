#pragma once

#include "../logger.h"
#include "wuwa_dxvk.h"
#include "wuwa_vkd3d.h"

#include <cstdint>
#include <vector>
#include <cstring>
#include <string_view>

namespace wuwa_layer {

  using wuwa_vkd3d::SPV_HEADER_MAGIC;
  using wuwa_vkd3d::SPV_OP_NOP;
  using wuwa_vkd3d::SPV_OP_EXT_INST;
  using wuwa_vkd3d::SPV_OP_CONSTANT;
  using wuwa_vkd3d::SPV_OP_CONSTANT_COMPOSITE;
  using wuwa_vkd3d::SPV_OP_FUNCTION;
  using wuwa_vkd3d::SPV_OP_FUNCTION_PARAMETER;
  using wuwa_vkd3d::SPV_OP_FUNCTION_END;
  using wuwa_vkd3d::SPV_OP_FUNCTION_CALL;
  using wuwa_vkd3d::SPV_OP_VARIABLE;
  using wuwa_vkd3d::SPV_OP_LOAD;
  using wuwa_vkd3d::SPV_OP_STORE;
  using wuwa_vkd3d::SPV_OP_ACCESS_CHAIN;
  using wuwa_vkd3d::SPV_OP_DECORATE;
  using wuwa_vkd3d::SPV_OP_MEMBER_DECORATE;
  using wuwa_vkd3d::SPV_OP_COMPOSITE_CONSTRUCT;
  using wuwa_vkd3d::SPV_OP_COMPOSITE_EXTRACT;
  using wuwa_vkd3d::SPV_OP_COPY_OBJECT;
  using wuwa_vkd3d::SPV_OP_DOT;
  using wuwa_vkd3d::SPV_OP_LABEL;
  using wuwa_vkd3d::SPV_OP_BRANCH;
  using wuwa_vkd3d::SPV_OP_BRANCH_CONDITIONAL;
  using wuwa_vkd3d::SPV_OP_KILL;
  using wuwa_vkd3d::SPV_OP_RETURN;
  using wuwa_vkd3d::SPV_OP_RETURN_VALUE;
  using wuwa_vkd3d::SPV_OP_TERMINATE_INVOCATION;
  using wuwa_vkd3d::SPV_OP_DEMOTE_TO_HELPER_INVOCATION;
  using wuwa_vkd3d::SPV_DECORATION_BUILTIN;
  using wuwa_vkd3d::SPV_BUILTIN_FRAG_COORD;
  using wuwa_vkd3d::SPV_BUILTIN_SAMPLE_POSITION;
  using wuwa_vkd3d::FloatUint;

  inline bool is_layer_active() {
    return game_logger::is_active();
  }

  /*
   * Accurately determines if shader was produced by DXVK or VKD3D-Proton.
   */
  inline bool is_dxvk_translation_layer(const uint32_t* spirv_code, size_t word_count) {
    if (word_count < 5)
      return true;

    // SPIR-V Header Word 2: (Tool ID << 16) | Tool Version
    // Khronos registered Tool ID: VKD3D-Shader = 30017 (0x7541)
    uint32_t generator = spirv_code[2];
    uint16_t tool_id = static_cast<uint16_t>(generator >> 16);
    if (tool_id == 30017) {
      return false; // Confirmed VKD3D-Proton
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

    return true; // Default to DXVK if not VKD3D
  }

  inline void process_spirv_anti_dither(uint32_t* spirv_code, size_t word_count) {
    if (!spirv_code || word_count < 5)
      return;

    if (!is_layer_active())
      return;

    if (spirv_code[0] != SPV_HEADER_MAGIC)
      return;

    if (is_dxvk_translation_layer(spirv_code, word_count)) {
      wuwa_dxvk::process_spirv_anti_dither(spirv_code, word_count);
    } else {
      wuwa_vkd3d::process_spirv_anti_dither(spirv_code, word_count);
    }
  }

} // namespace wuwa_layer
