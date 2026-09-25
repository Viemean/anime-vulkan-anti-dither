#pragma once

#include "logger.h"

#include <cstddef>
#include <cstdint>
#include <vector>
#include <unordered_set>

namespace azur_promilia_layer {

  constexpr uint32_t SPV_HEADER_MAGIC                       = 0x07230203;
  constexpr uint32_t SPV_OP_NOP                             = 0;
  constexpr uint32_t SPV_OP_KILL                            = 252;
  constexpr uint32_t SPV_OP_TERMINATE_INVOCATION            = 4416;
  constexpr uint32_t SPV_OP_DEMOTE_TO_HELPER_INVOCATION     = 5380;
  constexpr uint32_t SPV_OP_DEMOTE_TO_HELPER_INVOCATION_EXT = 5380;

  inline bool is_discard_opcode(uint16_t opcode) {
    return opcode == SPV_OP_KILL ||
           opcode == SPV_OP_TERMINATE_INVOCATION ||
           opcode == SPV_OP_DEMOTE_TO_HELPER_INVOCATION ||
           opcode == SPV_OP_DEMOTE_TO_HELPER_INVOCATION_EXT;
  }

  // Unity Known Secondary Passes (ShadowCaster, DepthOnly, Face Shadow, Outlines)
  inline bool is_known_secondary_pass(size_t word_count) {
    switch (word_count) {
      case 318:
      case 332:
      case 365:
      case 367:
      case 465:
      case 468:
      case 479:
      case 514:
      case 557:
      case 815:
      case 829:
      case 864:
      case 870:
      case 874:
        return true;
      default:
        return false;
    }
  }

  // Farmland Crop & Micro-vegetation Protection (Preserves discard cutout to prevent white squares)
  inline bool is_protected_crop_or_foliage(size_t word_count) {
    switch (word_count) {
      case 114: // Corn tassel top cutout
      case 185: // Farmland micro ground weed
      case 339: // Crop leaf cutout
        return true;
      default:
        return false;
    }
  }

  /*
   * Azur Promilia (Unity Engine) 5-Tier Hierarchical Anti-Dither Filter
   * Reference: shader_knowledge_base_dxvk3.1.json
   *
   * Rule 1: UberShader (> 3000 DW) -> OpNop (Character main meshes, hair, skin, costume)
   * Rule 2: Secondary Passes (514, 557, 815, 829, 864, 870, 874, etc.) -> OpNop (Face shadows & outlines)
   * Rule 3: Protected Crops (114, 185, 339 DW) -> Preserve Discard (Farmland crops 100% transparent cutout)
   * Rule 4: Eyes & Micro Passes (<= 500 DW, excluding Rule 3) -> OpNop (Cornea, Pupil, Iris close-up)
   * Rule 5: World Nature Assets (> 500 DW) -> Preserve Discard (World trees, bush leaves normal cutout)
   */
  inline bool should_neutralize_discard(size_t word_count) {
    // Rule 1: Character UberShaders
    if (word_count > 3000) {
      return true;
    }

    // Rule 2: Known Character Secondary Passes
    if (is_known_secondary_pass(word_count)) {
      return true;
    }

    // Rule 3: Protected Crops & Micro-weeds
    if (is_protected_crop_or_foliage(word_count)) {
      return false;
    }

    // Rule 4: Eyes close-up and micro specular passes
    if (word_count <= 500) {
      return true;
    }

    // Rule 5: World Vegetation & Trees (> 500 DW)
    return false;
  }

  inline void process_spirv_anti_dither(uint32_t* spirv_code, size_t word_count) {
    if (!spirv_code || word_count < 5)
      return;

    if (spirv_code[0] != SPV_HEADER_MAGIC)
      return;

    uint32_t shader_hash = game_logger::compute_spirv_hash(spirv_code, word_count);

    if (game_logger::g_exclude_hashes.count(shader_hash) > 0) {
      game_logger::log_msg("[AzurPromilia-Layer] Shader 0x%08x excluded by blacklist rule\n", shader_hash);
      return;
    }

    bool force_mode = (game_logger::g_force_hashes.count(shader_hash) > 0);
    bool should_neutralize = force_mode || should_neutralize_discard(word_count);

    if (!should_neutralize) {
      return;
    }

    size_t i = 5;
    uint32_t discard_nop_count = 0;

    while (i < word_count) {
      uint32_t word = spirv_code[i];
      uint16_t opcode = word & 0xFFFF;
      uint16_t length = (word >> 16) & 0xFFFF;

      if (length == 0 || (i + length) > word_count)
        break;

      if (is_discard_opcode(opcode)) {
        for (uint16_t k = 0; k < length; ++k) {
          spirv_code[i + k] = (1 << 16) | SPV_OP_NOP;
        }
        discard_nop_count++;
      }

      i += length;
    }

    if (discard_nop_count > 0) {
      game_logger::log_msg("[AzurPromilia-Layer] Neutralized %u discard opcodes in shader 0x%08x (Length: %zu DW)\n",
                           discard_nop_count, shader_hash, word_count);
    }
  }

} // namespace azur_promilia_layer
