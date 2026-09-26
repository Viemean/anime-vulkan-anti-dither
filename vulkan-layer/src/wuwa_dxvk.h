#pragma once

#include "logger.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>
#include <unordered_set>
#include <unordered_map>

namespace wuwa_dxvk {

  constexpr uint32_t SPV_HEADER_MAGIC                       = 0x07230203;
  constexpr uint32_t SPV_OP_NOP                             = 0;
  constexpr uint32_t SPV_OP_EXT_INST                        = 12;
  constexpr uint32_t SPV_OP_CONSTANT                        = 43;
  constexpr uint32_t SPV_OP_CONSTANT_COMPOSITE              = 44;
  constexpr uint32_t SPV_OP_FUNCTION                        = 54;
  constexpr uint32_t SPV_OP_FUNCTION_PARAMETER              = 55;
  constexpr uint32_t SPV_OP_FUNCTION_END                    = 56;
  constexpr uint32_t SPV_OP_FUNCTION_CALL                   = 57;
  constexpr uint32_t SPV_OP_VARIABLE                        = 59;
  constexpr uint32_t SPV_OP_LOAD                            = 61;
  constexpr uint32_t SPV_OP_STORE                           = 62;
  constexpr uint32_t SPV_OP_ACCESS_CHAIN                    = 65;
  constexpr uint32_t SPV_OP_DECORATE                        = 71;
  constexpr uint32_t SPV_OP_MEMBER_DECORATE                 = 72;
  constexpr uint32_t SPV_OP_COMPOSITE_CONSTRUCT             = 80;
  constexpr uint32_t SPV_OP_COMPOSITE_EXTRACT               = 81;
  constexpr uint32_t SPV_OP_COPY_OBJECT                    = 83;
  constexpr uint32_t SPV_OP_DOT                             = 148;
  constexpr uint32_t SPV_OP_LABEL                           = 248;
  constexpr uint32_t SPV_OP_BRANCH                          = 249;
  constexpr uint32_t SPV_OP_BRANCH_CONDITIONAL              = 250;
  constexpr uint32_t SPV_OP_KILL                            = 252;
  constexpr uint32_t SPV_OP_RETURN                          = 253;
  constexpr uint32_t SPV_OP_RETURN_VALUE                    = 254;
  constexpr uint32_t SPV_OP_TERMINATE_INVOCATION            = 4416;
  constexpr uint32_t SPV_OP_DEMOTE_TO_HELPER_INVOCATION     = 5380;
  constexpr uint32_t SPV_OP_DEMOTE_TO_HELPER_INVOCATION_EXT = 5380;

  constexpr uint32_t SPV_DECORATION_BUILTIN                 = 11;
  constexpr uint32_t SPV_BUILTIN_FRAG_COORD                 = 15;
  constexpr uint32_t SPV_BUILTIN_SAMPLE_POSITION            = 19;

  union FloatUint {
    float f;
    uint32_t u;
  };

  inline bool is_layer_active() {
    return game_logger::is_active();
  }

  inline bool is_discard_opcode(uint16_t opcode) {
    return opcode == SPV_OP_KILL ||
           opcode == SPV_OP_TERMINATE_INVOCATION ||
           opcode == SPV_OP_DEMOTE_TO_HELPER_INVOCATION ||
           opcode == SPV_OP_DEMOTE_TO_HELPER_INVOCATION_EXT;
  }

  inline bool is_image_sample_opcode(uint16_t opcode) {
    return (opcode >= 87 && opcode <= 107) || (opcode >= 305 && opcode <= 318) || opcode == 86;
  }

  inline void process_spirv_anti_dither(uint32_t* spirv_code, size_t word_count) {
    if (!spirv_code || word_count < 5)
      return;

    if (!is_layer_active())
      return;

    if (spirv_code[0] != SPV_HEADER_MAGIC)
      return;

    uint32_t bound = spirv_code[3];
    if (bound == 0 || bound > 1048576)
      return;

    uint32_t shader_hash = game_logger::compute_spirv_hash(spirv_code, word_count);

    // Blacklist check
    if (game_logger::g_exclude_hashes.count(shader_hash) > 0) {
      game_logger::log_msg("[反虚化驱动层-DXVK] Shader 0x%08x excluded by blacklist rule\n", shader_hash);
      return;
    }

    bool is_force_mode = (game_logger::g_force_hashes.count(shader_hash) > 0);

    std::vector<uint32_t> orig_copy;
    if (game_logger::g_dump_enabled) {
      orig_copy.assign(spirv_code, spirv_code + word_count);
    }

    std::vector<uint8_t> is_frag_coord_var(bound, 0);
    std::vector<uint8_t> is_dither_const(bound, 0);
    std::vector<uint8_t> is_dither_noise(bound, 0);
    std::vector<uint8_t> depends_on_frag_coord(bound, 0);
    std::vector<uint8_t> depends_on_sample(bound, 0);
    std::vector<uint8_t> is_demote_func(bound, 0);
    std::vector<uint32_t> cond_for_label(bound, 0);

    bool has_any_frag_coord = false;
    bool has_any_sample = false;
    bool has_dither_signature = false;
    bool is_foliage_lod_shader = false;
    bool is_particle_effect_shader = false;

    /*
     * Pre-Pass 1: Detect FragCoord inputs, UE DitherTemporalAA constants, and 3x3/4x4 Bayer Matrix Tables
     */
    std::vector<uint8_t> is_bayer_fraction(bound, 0);

    /*
     * Pre-Pass 1: Detect FragCoord inputs and complete 3x3 Bayer Matrix Tables (0/9..8/9)
     */
    {
      size_t i = 5;
      while (i < word_count) {
        uint32_t word = spirv_code[i];
        uint16_t opcode = word & 0xFFFF;
        uint16_t length = (word >> 16) & 0xFFFF;
        if (length == 0 || (i + length) > word_count)
          break;

        if (opcode == 5 /* OpName */ && length >= 3) {
          size_t max_bytes = static_cast<size_t>(length - 2) * sizeof(uint32_t);
          const char* str_ptr = reinterpret_cast<const char*>(&spirv_code[i + 2]);
          size_t str_len = 0;
          while (str_len < max_bytes && str_ptr[str_len] != '\0') {
            str_len++;
          }
          std::string_view name_sv(str_ptr, str_len);
          if (name_sv.find("NIAGARA") != std::string_view::npos ||
              name_sv.find("PARTICLE") != std::string_view::npos ||
              name_sv.find("Niagara") != std::string_view::npos ||
              name_sv.find("Particle") != std::string_view::npos) {
            is_particle_effect_shader = true;
          }
        } else if (opcode == SPV_OP_DECORATE && length >= 4) {
          uint32_t target_id = spirv_code[i + 1];
          uint32_t decoration = spirv_code[i + 2];
          uint32_t builtin = spirv_code[i + 3];
          if (decoration == SPV_DECORATION_BUILTIN &&
              (builtin == SPV_BUILTIN_FRAG_COORD || builtin == SPV_BUILTIN_SAMPLE_POSITION)) {
            if (target_id < bound) {
              is_frag_coord_var[target_id] = 1;
              depends_on_frag_coord[target_id] = 1;
              has_any_frag_coord = true;
            }
          }
        } else if (opcode == SPV_OP_CONSTANT && length >= 4) {
          uint32_t res_id = spirv_code[i + 2];
          FloatUint fu;
          fu.u = spirv_code[i + 3];

          // Foliage / StaticMesh LOD signature constant (347.8345)
          if (fu.f >= 347.83f && fu.f <= 347.84f) {
            is_foliage_lod_shader = true;
          }
          // 3x3 Bayer Table fractions (0/9..8/9)
          else if ((fu.f >= -0.0001f && fu.f <= 0.0001f) ||
              (fu.f >= 0.1111f && fu.f <= 0.1112f) ||
              (fu.f >= 0.2222f && fu.f <= 0.2223f) ||
              (fu.f >= 0.3333f && fu.f <= 0.3334f) ||
              (fu.f >= 0.4444f && fu.f <= 0.4445f) ||
              (fu.f >= 0.5555f && fu.f <= 0.5556f) ||
              (fu.f >= 0.6666f && fu.f <= 0.6667f) ||
              (fu.f >= 0.7777f && fu.f <= 0.7778f) ||
              (fu.f >= 0.8888f && fu.f <= 0.8889f)) {
            if (res_id < bound) {
              is_bayer_fraction[res_id] = 1;
            }
          }
          // Interleaved Gradient Noise (IGN) constants (0.06711056, 0.00583715, 52.982918)
          else if ((fu.f >= 0.06711f && fu.f <= 0.06712f) ||
                   (fu.f >= 0.005837f && fu.f <= 0.005838f) ||
                   (fu.f >= 52.982f && fu.f <= 52.983f)) {
            if (res_id < bound) {
              is_dither_const[res_id] = 1;
              has_dither_signature = true;
            }
          }
        } else if (opcode == SPV_OP_CONSTANT_COMPOSITE && length >= 4) {
          uint32_t res_id = spirv_code[i + 2];
          uint32_t bayer_fraction_matches = 0;
          uint32_t dither_const_matches = 0;
          for (uint16_t c = 3; c < length; ++c) {
            uint32_t const_id = spirv_code[i + c];
            if (const_id < bound) {
              if (is_bayer_fraction[const_id])
                bayer_fraction_matches++;
              if (is_dither_const[const_id])
                dither_const_matches++;
            }
          }
          // Case 1: DX12 (VKD3D) 1D array of 9 floats
          if (bayer_fraction_matches >= 6 && length >= 9) {
            if (res_id < bound) {
              is_dither_const[res_id] = 1;
              has_dither_signature = true;
            }
          }
          // Case 2: DX11 (DXVK) v4float vector composed of Bayer fractions (e.g. 0/9, 7/9, 3/9, 6/9)
          else if (bayer_fraction_matches >= 3 && length >= 6) {
            if (res_id < bound) {
              is_dither_const[res_id] = 1;
              has_dither_signature = true;
            }
          }
          // Case 3: DX11 (DXVK) IGN Vector (0.06711056, 0.00583715) or array of Bayer vectors
          else if (dither_const_matches >= 2) {
            if (res_id < bound) {
              is_dither_const[res_id] = 1;
              has_dither_signature = true;
            }
          }
        } else if (opcode == SPV_OP_VARIABLE && length >= 4) {
          uint32_t res_id = spirv_code[i + 2];
          if (length >= 5) {
            uint32_t initializer = spirv_code[i + 4];
            if (initializer < bound && is_dither_const[initializer]) {
              if (res_id < bound) {
                is_dither_const[res_id] = 1;
                has_dither_signature = true;
              }
            }
          }
        }
        i += length;
      }
    }

    if ((is_foliage_lod_shader || is_particle_effect_shader) && !is_force_mode) {
      // 100% Protect Foliage LOD crossfades and Particle/Ribbon/Niagara attack effects
      return;
    }

    /*
     * Pass 1: Forward SSA Data-flow analysis
     * - Seed Dither Noise generators:
     *   1. OpDot with is_dither_const (UE4 DitherTemporalAA)
     *   2. OpInBoundsAccessChain / OpAccessChain on Bayer matrix tables
     *   3. SRem/FMod on FragCoord
     * - Propagate is_dither_noise and depends_on_sample downstream
     */
    bool changed = true;
    uint32_t iteration = 0;
    constexpr uint32_t max_iterations = 32;

    while (changed && iteration < max_iterations) {
      changed = false;
      iteration++;
      size_t i = 5;
      uint32_t current_func = 0;

      while (i < word_count) {
        uint32_t word = spirv_code[i];
        uint16_t opcode = word & 0xFFFF;
        uint16_t length = (word >> 16) & 0xFFFF;

        if (length == 0 || (i + length) > word_count)
          break;

        if (opcode == SPV_OP_FUNCTION && length >= 3) {
          current_func = spirv_code[i + 2];
        } else if (opcode == SPV_OP_FUNCTION_END) {
          current_func = 0;
        } else if (is_discard_opcode(opcode)) {
          if (current_func > 0 && current_func < bound)
            is_demote_func[current_func] = 1;
        } else if (opcode == SPV_OP_BRANCH_CONDITIONAL && length >= 4) {
          uint32_t cond = spirv_code[i + 1];
          uint32_t true_label = spirv_code[i + 2];
          uint32_t false_label = spirv_code[i + 3];
          if (true_label < bound)
            cond_for_label[true_label] = cond;
          if (false_label < bound)
            cond_for_label[false_label] = cond;
        }

        uint32_t res_id = 0;
        if (length >= 3 &&
            opcode != SPV_OP_DECORATE && opcode != SPV_OP_MEMBER_DECORATE &&
            opcode != SPV_OP_STORE && opcode != SPV_OP_BRANCH &&
            opcode != SPV_OP_BRANCH_CONDITIONAL && opcode != SPV_OP_KILL &&
            opcode != SPV_OP_RETURN && opcode != SPV_OP_RETURN_VALUE &&
            opcode != SPV_OP_TERMINATE_INVOCATION &&
            opcode != SPV_OP_DEMOTE_TO_HELPER_INVOCATION) {
          res_id = (opcode == SPV_OP_VARIABLE && length >= 3) ? spirv_code[i + 2] :
                   (opcode >= 19 && opcode <= 54 && opcode != 59) ? spirv_code[i + 1] :
                   spirv_code[i + 2];
        }

        // Memory load / access chain from FragCoord or Bayer Matrix
        if (opcode == SPV_OP_LOAD && length >= 4) {
          uint32_t ptr_id = spirv_code[i + 3];
          if (ptr_id < bound) {
            if (is_frag_coord_var[ptr_id]) {
              if (res_id < bound && !depends_on_frag_coord[res_id]) {
                depends_on_frag_coord[res_id] = 1;
                changed = true;
              }
            }
            if (is_dither_const[ptr_id] || is_dither_noise[ptr_id]) {
              if (res_id < bound && !is_dither_noise[res_id]) {
                is_dither_noise[res_id] = 1;
                changed = true;
              }
            }
          }
        } else if ((opcode == SPV_OP_ACCESS_CHAIN || opcode == 66 /* InBoundsAccessChain */) && length >= 4) {
          uint32_t base_ptr = spirv_code[i + 3];
          if (base_ptr < bound) {
            if (is_frag_coord_var[base_ptr]) {
              if (res_id < bound && !is_frag_coord_var[res_id]) {
                is_frag_coord_var[res_id] = 1;
                depends_on_frag_coord[res_id] = 1;
                changed = true;
              }
            }
            if (is_dither_const[base_ptr]) {
              if (res_id < bound && !is_dither_const[res_id]) {
                is_dither_const[res_id] = 1;
                is_dither_noise[res_id] = 1;
                changed = true;
              }
            }
          }
        } else if (opcode == SPV_OP_STORE && length >= 3) {
          uint32_t ptr_id = spirv_code[i + 1];
          uint32_t obj_id = spirv_code[i + 2];
          if (ptr_id < bound && obj_id < bound) {
            if (is_dither_noise[obj_id] && !is_dither_noise[ptr_id]) {
              is_dither_noise[ptr_id] = 1;
              changed = true;
            }
            if (depends_on_frag_coord[obj_id] && !depends_on_frag_coord[ptr_id]) {
              depends_on_frag_coord[ptr_id] = 1;
              changed = true;
            }
            if (depends_on_sample[obj_id] && !depends_on_sample[ptr_id]) {
              depends_on_sample[ptr_id] = 1;
              changed = true;
            }
          }
        }

        // Dither dot product detection: OpDot with is_dither_const (IGN Dither or Bayer vector)
        if (opcode == SPV_OP_DOT && length >= 5) {
          uint32_t op1 = spirv_code[i + 3];
          uint32_t op2 = spirv_code[i + 4];
          if (op1 < bound && op2 < bound) {
            if (is_dither_const[op1] || is_dither_const[op2]) {
              if (res_id < bound && !is_dither_noise[res_id]) {
                is_dither_noise[res_id] = 1;
                changed = true;
              }
            }
          }
        } else if (opcode == SPV_OP_FUNCTION_CALL && length >= 5) {
          for (uint16_t c = 4; c < length; ++c) {
            uint32_t arg = spirv_code[i + c];
            if (arg < bound) {
              if (is_dither_const[arg] || is_dither_noise[arg]) {
                if (res_id < bound && !is_dither_noise[res_id]) {
                  is_dither_noise[res_id] = 1;
                  changed = true;
                }
              }
              if (depends_on_frag_coord[arg]) {
                if (res_id < bound && !depends_on_frag_coord[res_id]) {
                  depends_on_frag_coord[res_id] = 1;
                  changed = true;
                }
              }
            }
          }
        } else if ((opcode >= 137 && opcode <= 141) && length >= 5) { // OpUMod (137), OpSRem (138), OpSMod (139), OpFRem (140), OpFMod (141)
          uint32_t op1 = spirv_code[i + 3];
          if (op1 < bound && depends_on_frag_coord[op1]) {
            if (res_id < bound && !is_dither_noise[res_id]) {
              is_dither_noise[res_id] = 1;
              changed = true;
            }
          }
        } else if (opcode == SPV_OP_COMPOSITE_CONSTRUCT && length >= 4) {
          for (uint16_t c = 3; c < length; ++c) {
            uint32_t op = spirv_code[i + c];
            if (op < bound && is_dither_const[op]) {
              if (res_id < bound && !is_dither_const[res_id]) {
                is_dither_const[res_id] = 1;
                changed = true;
              }
              break;
            }
          }
        }

        // Image sample dependency
        if (is_image_sample_opcode(opcode)) {
          has_any_sample = true;
          if (res_id < bound && depends_on_sample[res_id] == 0) {
            depends_on_sample[res_id] = 1;
            changed = true;
          }
        }

        // Propagate is_dither_noise & depends_on_sample downstream to res_id
        if (res_id > 0 && res_id < bound) {
          uint16_t start_op = (opcode >= 19 && opcode <= 54 && opcode != 59) ? 2 : 3;
          for (uint16_t k = start_op; k < length && (i + k) < word_count; ++k) {
            uint32_t op_val = spirv_code[i + k];
            if (op_val < bound) {
              if (is_dither_noise[op_val] && !is_dither_noise[res_id]) {
                is_dither_noise[res_id] = 1;
                changed = true;
              }
              if (depends_on_frag_coord[op_val] && !depends_on_frag_coord[res_id]) {
                depends_on_frag_coord[res_id] = 1;
                changed = true;
              }
              if (depends_on_sample[op_val] && !depends_on_sample[res_id]) {
                depends_on_sample[res_id] = 1;
                changed = true;
              }
            }
          }
        }

        i += length;
      }
    }

    /*
     * Pass 2: Precise Character/Toon Dither Neutralization & Foliage LOD Protection
     * Rule:
     * - Character/Toon Bayer Dither Noise -> Neutralized to OpNop (100% smooth camera close-ups)
     * - Foliage/StaticMesh LOD cross-fade (Signature A without Bayer) -> Preserved (Prevents tree geometric cards)
     * - Particle sphere bounding clips, depth fades, vertex alpha, material cutouts -> Preserved 100%
     */
    size_t i = 5;
    uint32_t current_label = 0;
    uint32_t demote_modified_count = 0;
    uint32_t demote_preserved_count = 0;

    // Step A: Trace backward slice from Demote conditions only!
    // CRITICAL: We NEVER touch NMins in color clamps, shadow maps, lighting or SV_Target outputs!
    // NMins are ONLY uncoupled if they directly participate in computing a demote condition.
    std::vector<uint8_t> is_in_demote_slice(bound, 0);
    std::vector<uint8_t> is_const_zero_or_neg(bound, 0);

    // Identify zero or negative constants
    {
      size_t k = 5;
      while (k < word_count) {
        uint32_t w = spirv_code[k];
        uint16_t op = w & 0xFFFF;
        uint16_t l = (w >> 16) & 0xFFFF;
        if (l == 0 || (k + l) > word_count) break;
        if (op == SPV_OP_CONSTANT && l >= 4) {
          uint32_t cid = spirv_code[k + 2];
          FloatUint fu;
          fu.u = spirv_code[k + 3];
          if (cid < bound && fu.f <= 0.0001f) {
            is_const_zero_or_neg[cid] = 1;
          }
        }
        k += l;
      }
    }

    // Seed backward slice from demote function arguments and conditional branch labels
    {
      size_t k = 5;
      uint32_t current_slice_label = 0;
      while (k < word_count) {
        uint32_t w = spirv_code[k];
        uint16_t op = w & 0xFFFF;
        uint16_t l = (w >> 16) & 0xFFFF;
        if (l == 0 || (k + l) > word_count) break;

        if (op == SPV_OP_LABEL && l >= 2) {
          current_slice_label = spirv_code[k + 1];
        } else if (op == SPV_OP_FUNCTION_CALL && l >= 5) {
          uint32_t fid = spirv_code[k + 3];
          uint32_t aid = spirv_code[k + 4];
          if (fid < bound && is_demote_func[fid] && aid < bound) {
            is_in_demote_slice[aid] = 1;
          }
        } else if (is_discard_opcode(op)) {
          if (current_slice_label < bound) {
            uint32_t cid = cond_for_label[current_slice_label];
            if (cid > 0 && cid < bound) is_in_demote_slice[cid] = 1;
          }
        }
        k += l;
      }

      // Propagate backward slice upstream up to 4 levels
      for (int step = 0; step < 4; ++step) {
        k = 5;
        while (k < word_count) {
          uint32_t w = spirv_code[k];
          uint16_t op = w & 0xFFFF;
          uint16_t l = (w >> 16) & 0xFFFF;
          if (l == 0 || (k + l) > word_count) break;

          uint32_t res_id = 0;
          if (l >= 3 && op != SPV_OP_DECORATE && op != SPV_OP_MEMBER_DECORATE &&
              op != SPV_OP_STORE && op != SPV_OP_BRANCH && op != SPV_OP_BRANCH_CONDITIONAL) {
            res_id = (op == SPV_OP_VARIABLE && l >= 3) ? spirv_code[k + 2] :
                     (op >= 19 && op <= 54 && op != 59) ? spirv_code[k + 1] : spirv_code[k + 2];
          }

          if (res_id > 0 && res_id < bound && is_in_demote_slice[res_id]) {
            uint16_t start_op = (op == SPV_OP_EXT_INST) ? 5 : 3;
            for (uint16_t j = start_op; j < l; ++j) {
              uint32_t operand_id = spirv_code[k + j];
              if (operand_id < bound) {
                is_in_demote_slice[operand_id] = 1;
              }
            }
          }
          k += l;
        }
      }
    }

    // Step A.1: Uncouple ONLY NMins that belong to the demote slice and are NOT clamped to zero
    uint32_t nmin_uncoupled_count = 0;
    i = 5;
    while (i < word_count) {
      uint32_t word = spirv_code[i];
      uint16_t opcode = word & 0xFFFF;
      uint16_t length = (word >> 16) & 0xFFFF;
      if (length == 0 || (i + length) > word_count) break;

      if (opcode == SPV_OP_EXT_INST && length >= 7) {
        uint32_t inst = spirv_code[i + 4];
        // GLSL.std.450 NMin (79) or FMin (28)
        if (inst == 79 || inst == 28) {
          uint32_t res_id = spirv_code[i + 2];
          uint32_t op1 = spirv_code[i + 5];
          uint32_t op2 = spirv_code[i + 6];
          if (op1 < bound && op2 < bound && res_id < bound && is_in_demote_slice[res_id]) {
            bool op1_dither = is_dither_noise[op1];
            bool op2_dither = is_dither_noise[op2];

            if (op1_dither ^ op2_dither) {
              uint32_t survivor_op = op1_dither ? op2 : op1;
              // Ensure survivor is a valid cutout, NOT a zero/negative clamp constant!
              if (survivor_op < bound && !is_const_zero_or_neg[survivor_op]) {
                spirv_code[i] = (4 << 16) | SPV_OP_COPY_OBJECT;
                spirv_code[i + 3] = survivor_op;
                for (uint16_t k = 4; k < length && (i + k) < word_count; ++k) {
                  spirv_code[i + k] = (1 << 16) | SPV_OP_NOP;
                }
                demote_modified_count++;
                nmin_uncoupled_count++;
                is_dither_noise[res_id] = 0;
              }
            }
          }
        }
      }
      i += length;
    }

    // If NMin was uncoupled, recompute is_dither_noise so downstream demote conditions see pure Cutout!
    if (nmin_uncoupled_count > 0) {
      for (size_t k = 0; k < bound; ++k) is_dither_noise[k] = 0;
      bool dither_data_changed = true;
      uint32_t dither_iter = 0;
      while (dither_data_changed && dither_iter < 16) {
        dither_data_changed = false;
        dither_iter++;
        i = 5;
        while (i < word_count) {
          uint32_t word = spirv_code[i];
          uint16_t opcode = word & 0xFFFF;
          uint16_t length = (word >> 16) & 0xFFFF;
          if (length == 0 || (i + length) > word_count) break;
          uint32_t res_id = 0;
          if (length >= 3 && opcode != SPV_OP_DECORATE && opcode != SPV_OP_MEMBER_DECORATE &&
              opcode != SPV_OP_STORE && opcode != SPV_OP_BRANCH && opcode != SPV_OP_BRANCH_CONDITIONAL) {
            res_id = (opcode == SPV_OP_VARIABLE && length >= 3) ? spirv_code[i + 2] :
                     (opcode >= 19 && opcode <= 54 && opcode != 59) ? spirv_code[i + 1] : spirv_code[i + 2];
          }
          if (opcode == SPV_OP_LOAD && length >= 4) {
            uint32_t ptr_id = spirv_code[i + 3];
            if (ptr_id < bound && is_dither_const[ptr_id] && res_id < bound && !is_dither_noise[res_id]) {
              is_dither_noise[res_id] = 1;
              dither_data_changed = true;
            }
          } else if ((opcode == SPV_OP_ACCESS_CHAIN || opcode == 66) && length >= 4) {
            uint32_t base_ptr = spirv_code[i + 3];
            if (base_ptr < bound && is_dither_const[base_ptr] && res_id < bound && !is_dither_noise[res_id]) {
              is_dither_noise[res_id] = 1;
              dither_data_changed = true;
            }
          } else if (opcode == SPV_OP_DOT && length >= 5) {
            uint32_t op1 = spirv_code[i + 3]; uint32_t op2 = spirv_code[i + 4];
            if (op1 < bound && op2 < bound && (is_dither_const[op1] || is_dither_const[op2]) && res_id < bound && !is_dither_noise[res_id]) {
              is_dither_noise[res_id] = 1;
              dither_data_changed = true;
            }
          } else if ((opcode >= 137 && opcode <= 141) && length >= 5) {
            uint32_t op1 = spirv_code[i + 3];
            if (op1 < bound && depends_on_frag_coord[op1] && res_id < bound && !is_dither_noise[res_id]) {
              is_dither_noise[res_id] = 1;
              dither_data_changed = true;
            }
          }
          if (res_id > 0 && res_id < bound && opcode != SPV_OP_COPY_OBJECT) {
            uint16_t start_op = (opcode == SPV_OP_EXT_INST) ? 5 : 3;
            for (uint16_t j = start_op; j < length; ++j) {
              uint32_t op = spirv_code[i + j];
              if (op < bound && is_dither_noise[op] && !is_dither_noise[res_id]) {
                is_dither_noise[res_id] = 1;
                dither_data_changed = true;
                break;
              }
            }
          }
          i += length;
        }
      }
    }

    // Step B: NOP pure dither demotes (Character close-up dither)
    // CRITICAL: Any condition that depends on texture sample is ALPHA CUTOUT -> NEVER NOP!
    i = 5;
    while (i < word_count) {
      uint32_t word = spirv_code[i];
      uint16_t opcode = word & 0xFFFF;
      uint16_t length = (word >> 16) & 0xFFFF;

      if (length == 0 || (i + length) > word_count)
        break;

      if (opcode == SPV_OP_LABEL && length >= 2) {
        current_label = spirv_code[i + 1];
      } else if (opcode == SPV_OP_FUNCTION_CALL && length >= 5) {
        uint32_t func_id = spirv_code[i + 3];
        uint32_t arg_id = spirv_code[i + 4];

        if (func_id < bound && is_demote_func[func_id]) {
          bool should_nop = false;
          if (is_force_mode) {
            should_nop = true;
          } else if (arg_id < bound && is_dither_noise[arg_id]) {
            if (has_dither_signature) {
              should_nop = true;
            }
          }

          if (should_nop) {
            demote_modified_count++;
            for (uint16_t k = 0; k < length && (i + k) < word_count; ++k)
              spirv_code[i + k] = (1 << 16) | SPV_OP_NOP;
          } else {
            demote_preserved_count++;
          }
        }
      } else if (is_discard_opcode(opcode)) {
        uint32_t cond = (current_label < bound) ? cond_for_label[current_label] : 0;
        bool should_nop = false;

        if (is_force_mode) {
          should_nop = true;
        } else if (cond > 0 && cond < bound && is_dither_noise[cond]) {
          if (has_dither_signature) {
            should_nop = true;
          }
        }

        if (should_nop) {
          demote_modified_count++;
          for (uint16_t k = 0; k < length && (i + k) < word_count; ++k)
            spirv_code[i + k] = (1 << 16) | SPV_OP_NOP;
        } else {
          demote_preserved_count++;
        }
      }

      i += length;
    }

    if ((demote_modified_count + demote_preserved_count) > 0) {
      const char* category = has_dither_signature ? "角色卡通网点(Toon Bayer)" : (has_any_frag_coord ? "屏幕空间计算" : "特效/材质裁剪");
      game_logger::log_msg("[反虚化驱动层-DXVK] 着色器: 0x%08x | 字长: %zu | 消除虚化: %u 处 (解耦: %u) | 保留正常裁剪: %u 处 | 类别: %s\n",
              shader_hash, word_count, demote_modified_count, nmin_uncoupled_count, demote_preserved_count, category);
    }

    if (game_logger::g_dump_enabled && (demote_modified_count > 0 || demote_preserved_count > 0)) {
      game_logger::dump_shader_bundle(orig_copy.data(), orig_copy.size(),
                                      spirv_code, word_count,
                                      shader_hash, demote_modified_count, demote_preserved_count,
                                      has_any_frag_coord, has_any_sample);
    }
  }

} // namespace wuwa_dxvk



