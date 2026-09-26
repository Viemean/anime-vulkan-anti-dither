#pragma once

#include "logger.h"
#include "wuwa_vkd3d.h"

#include <cstdint>
#include <vector>
#include <cstring>
#include <string_view>

namespace wuwa_layer {

  constexpr uint32_t SPV_HEADER_MAGIC                       = 0x07230203;
  constexpr uint32_t SPV_OP_NOP                             = 0;
  constexpr uint32_t SPV_OP_NAME                            = 5;
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
  constexpr uint32_t SPV_OP_IN_BOUNDS_ACCESS_CHAIN          = 66;
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
  constexpr uint32_t SPV_OP_DEMOTE_TO_HELPER_INVOCATION_EXT = 5379;

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

  inline bool is_dxvk_translation_layer(const uint32_t* spirv_code, size_t word_count) {
    size_t i = 5;
    while (i < word_count) {
      uint32_t word = spirv_code[i];
      uint16_t opcode = word & 0xFFFF;
      uint16_t length = (word >> 16) & 0xFFFF;
      if (length == 0 || (i + length) > word_count)
        break;

      // OpCapability PhysicalStorageBufferAddresses (5347) is exclusively used by VKD3D-Proton
      if (opcode == 17 /* OpCapability */ && length >= 2) {
        if (spirv_code[i + 1] == 5347) {
          return false;
        }
      }

      // OpMemoryModel PhysicalStorageBuffer64 (5348)
      if (opcode == 14 /* OpMemoryModel */ && length >= 3) {
        if (spirv_code[i + 1] == 5348) {
          return false;
        }
      }

      if (opcode == 54 /* OpFunction */) {
        break;
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

    return true; // Default to DXVK if ambiguous
  }

  /*
   * Processes DXVK shaders directly inline (fallback pipeline prior to DXVK module testing).
   */
  inline void process_spirv_dxvk_inline(uint32_t* spirv_code, size_t word_count) {
    uint32_t bound = spirv_code[3];
    if (bound == 0 || bound > 1048576)
      return;

    uint32_t shader_hash = game_logger::compute_spirv_hash(spirv_code, word_count);

    if (game_logger::g_exclude_hashes.count(shader_hash) > 0) {
      game_logger::log_msg("[Anti-Dither-DXVK] Shader 0x%08x excluded by blacklist rule\n", shader_hash);
      return;
    }

    bool is_force_mode = (game_logger::g_force_hashes.count(shader_hash) > 0);

    bool has_any_discard_opcode = false;
    for (size_t k = 5; k < word_count; ) {
      uint32_t w = spirv_code[k];
      uint16_t op = w & 0xFFFF;
      uint16_t l = (w >> 16) & 0xFFFF;
      if (l == 0 || (k + l) > word_count) break;
      if (is_discard_opcode(op)) {
        has_any_discard_opcode = true;
        break;
      }
      k += l;
    }
    if (!has_any_discard_opcode && !is_force_mode) {
      return;
    }

    std::vector<uint32_t> orig_copy;
    if (game_logger::g_dump_enabled) {
      orig_copy.assign(spirv_code, spirv_code + word_count);
    }

    std::vector<uint8_t> is_frag_coord_var(bound, 0);
    std::vector<uint8_t> is_dither_const(bound, 0);
    std::vector<uint8_t> is_dither_noise(bound, 0);
    std::vector<uint8_t> depends_on_frag_coord(bound, 0);
    std::vector<uint8_t> depends_on_sample(bound, 0);
    std::vector<uint32_t> cond_for_label(bound, 0);
    std::vector<uint8_t> is_const_uint_5(bound, 0);
    std::vector<uint8_t> is_bayer_fraction(bound, 0);
    std::vector<uint8_t> is_const_zero_or_neg(bound, 0);

    bool has_any_frag_coord = false;
    bool has_any_sample = false;
    bool is_particle_effect_shader = false;

    // Pre-Pass 1: Detect FragCoord, IGN, Bayer matrix, and Modulo constants
    {
      size_t i = 5;
      while (i < word_count) {
        uint32_t word = spirv_code[i];
        uint16_t opcode = word & 0xFFFF;
        uint16_t length = (word >> 16) & 0xFFFF;
        if (length == 0 || (i + length) > word_count)
          break;

        if (opcode == SPV_OP_NAME && length >= 3) {
          size_t max_bytes = static_cast<size_t>(length - 2) * sizeof(uint32_t);
          const char* str_ptr = reinterpret_cast<const char*>(&spirv_code[i + 2]);
          size_t str_len = 0;
          while (str_len < max_bytes && str_ptr[str_len] != '\0') str_len++;
          std::string_view name_sv(str_ptr, str_len);
          if (name_sv.find("NIAGARA") != std::string_view::npos ||
              name_sv.find("PARTICLE") != std::string_view::npos ||
              name_sv.find("Niagara") != std::string_view::npos ||
              name_sv.find("Particle") != std::string_view::npos ||
              name_sv.find("centroid") != std::string_view::npos ||
              name_sv == "COLOR" ||
              name_sv.find("Foliage") != std::string_view::npos ||
              name_sv.find("Landscape") != std::string_view::npos) {
            is_particle_effect_shader = true;
          }
          if (name_sv == "SV_Position") {
            uint32_t tid = spirv_code[i + 1];
            if (tid < bound) {
              is_frag_coord_var[tid] = 1;
              depends_on_frag_coord[tid] = 1;
              has_any_frag_coord = true;
            }
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

          if (fu.u == 5) {
            if (res_id < bound) is_const_uint_5[res_id] = 1;
          }
          if (fu.f <= 0.0001f) {
            if (res_id < bound) is_const_zero_or_neg[res_id] = 1;
          }
          if ((fu.f >= 0.1110f && fu.f <= 0.1112f) ||
              (fu.f >= 0.2221f && fu.f <= 0.2223f) ||
              (fu.f >= 0.3332f && fu.f <= 0.3334f) ||
              (fu.f >= 0.4443f && fu.f <= 0.4445f) ||
              (fu.f >= 0.5554f && fu.f <= 0.5556f) ||
              (fu.f >= 0.7776f && fu.f <= 0.7779f) ||
              (fu.f >= 0.8887f && fu.f <= 0.8890f)) {
            if (res_id < bound) is_bayer_fraction[res_id] = 1;
          } else if ((fu.f >= 0.06711f && fu.f <= 0.06712f) ||
                     (fu.f >= 0.005837f && fu.f <= 0.005838f) ||
                     (fu.f >= 52.982f && fu.f <= 52.983f)) {
            if (res_id < bound) is_dither_const[res_id] = 1;
          }
        } else if (opcode == SPV_OP_CONSTANT_COMPOSITE && length >= 4) {
          uint32_t res_id = spirv_code[i + 2];
          uint32_t bayer_cnt = 0;
          uint32_t dither_cnt = 0;
          for (uint16_t c = 3; c < length; ++c) {
            uint32_t cid = spirv_code[i + c];
            if (cid < bound) {
              if (is_bayer_fraction[cid]) bayer_cnt++;
              if (is_dither_const[cid]) dither_cnt++;
            }
          }
          if ((bayer_cnt >= 4 && length >= 9) || dither_cnt >= 2) {
            if (res_id < bound) is_dither_const[res_id] = 1;
          }
        } else if (opcode == SPV_OP_VARIABLE && length >= 4) {
          uint32_t res_id = spirv_code[i + 2];
          if (length >= 5) {
            uint32_t init = spirv_code[i + 4];
            if (init < bound && is_dither_const[init] && res_id < bound) {
              is_dither_const[res_id] = 1;
            }
          }
        }
        i += length;
      }
    }

    if (is_particle_effect_shader && !is_force_mode) {
      return;
    }

    // Pass 1: Forward SSA Data-flow analysis
    bool changed = true;
    uint32_t iteration = 0;
    while (changed && iteration < 32) {
      changed = false;
      iteration++;
      size_t k = 5;
      while (k < word_count) {
        uint32_t word = spirv_code[k];
        uint16_t opcode = word & 0xFFFF;
        uint16_t length = (word >> 16) & 0xFFFF;
        if (length == 0 || (k + length) > word_count) break;

        if (opcode == SPV_OP_BRANCH_CONDITIONAL && length >= 4) {
          uint32_t cond = spirv_code[k + 1];
          uint32_t true_lbl = spirv_code[k + 2];
          uint32_t false_lbl = spirv_code[k + 3];
          if (true_lbl < bound) cond_for_label[true_lbl] = cond;
          if (false_lbl < bound) cond_for_label[false_lbl] = cond;
        }

        uint32_t res_id = 0;
        if (length >= 3 &&
            opcode != SPV_OP_DECORATE && opcode != SPV_OP_MEMBER_DECORATE &&
            opcode != SPV_OP_STORE && opcode != SPV_OP_BRANCH &&
            opcode != SPV_OP_BRANCH_CONDITIONAL && opcode != SPV_OP_KILL &&
            opcode != SPV_OP_RETURN && opcode != SPV_OP_RETURN_VALUE &&
            opcode != SPV_OP_TERMINATE_INVOCATION &&
            opcode != SPV_OP_DEMOTE_TO_HELPER_INVOCATION) {
          res_id = (opcode == SPV_OP_VARIABLE && length >= 3) ? spirv_code[k + 2] :
                   (opcode >= 19 && opcode <= 54 && opcode != 59) ? spirv_code[k + 1] : spirv_code[k + 2];
        }

        if (opcode == SPV_OP_LOAD && length >= 4) {
          uint32_t ptr = spirv_code[k + 3];
          if (ptr < bound) {
            if (is_frag_coord_var[ptr] && res_id < bound && !depends_on_frag_coord[res_id]) {
              depends_on_frag_coord[res_id] = 1; changed = true;
            }
            if ((is_dither_const[ptr] || is_dither_noise[ptr]) && res_id < bound && !is_dither_noise[res_id]) {
              is_dither_noise[res_id] = 1; changed = true;
            }
          }
        } else if ((opcode == SPV_OP_ACCESS_CHAIN || opcode == SPV_OP_IN_BOUNDS_ACCESS_CHAIN) && length >= 4) {
          uint32_t base = spirv_code[k + 3];
          if (base < bound) {
            if (is_frag_coord_var[base] && res_id < bound && !is_frag_coord_var[res_id]) {
              is_frag_coord_var[res_id] = 1; depends_on_frag_coord[res_id] = 1; changed = true;
            }
            if (is_dither_const[base] && res_id < bound && !is_dither_const[res_id]) {
              is_dither_const[res_id] = 1; is_dither_noise[res_id] = 1; changed = true;
            }
          }
        } else if (opcode == SPV_OP_STORE && length >= 3) {
          uint32_t ptr = spirv_code[k + 1];
          uint32_t obj = spirv_code[k + 2];
          if (ptr < bound && obj < bound) {
            if (is_dither_noise[obj] && !is_dither_noise[ptr]) { is_dither_noise[ptr] = 1; changed = true; }
            if (depends_on_frag_coord[obj] && !depends_on_frag_coord[ptr]) { depends_on_frag_coord[ptr] = 1; changed = true; }
            if (depends_on_sample[obj] && !depends_on_sample[ptr]) { depends_on_sample[ptr] = 1; changed = true; }
          }
        } else if (opcode == 137 /* OpUMod */ && length >= 5) {
          uint32_t op1 = spirv_code[k + 3];
          uint32_t op2 = spirv_code[k + 4];
          if (op1 < bound && op2 < bound) {
            if (depends_on_frag_coord[op1] && is_const_uint_5[op2]) {
              if (res_id < bound && !is_dither_noise[res_id]) {
                is_dither_noise[res_id] = 1;
                changed = true;
              }
            }
          }
        } else if (opcode == SPV_OP_DOT && length >= 5) {
          uint32_t op1 = spirv_code[k + 3];
          uint32_t op2 = spirv_code[k + 4];
          if (op1 < bound && op2 < bound && (is_dither_const[op1] || is_dither_const[op2])) {
            if (res_id < bound && !is_dither_noise[res_id]) {
              is_dither_noise[res_id] = 1; changed = true;
            }
          }
        } else if (opcode == SPV_OP_FUNCTION_CALL && length >= 4) {
          for (uint16_t c = 4; c < length; ++c) {
            uint32_t arg = spirv_code[k + c];
            if (arg < bound) {
              if ((is_dither_const[arg] || is_dither_noise[arg]) && res_id < bound && !is_dither_noise[res_id]) {
                is_dither_noise[res_id] = 1; changed = true;
              }
              if (depends_on_frag_coord[arg] && res_id < bound && !depends_on_frag_coord[res_id]) {
                depends_on_frag_coord[res_id] = 1; changed = true;
              }
            }
          }
        }

        if (is_image_sample_opcode(opcode)) {
          has_any_sample = true;
          if (res_id < bound && !depends_on_sample[res_id]) {
            depends_on_sample[res_id] = 1; changed = true;
          }
        }

        if (res_id > 0 && res_id < bound) {
          uint16_t start_op = (opcode == SPV_OP_EXT_INST) ? 5 : 3;
          uint16_t end_op = length;
          if (opcode == SPV_OP_COMPOSITE_EXTRACT) { start_op = 3; end_op = 4; }
          for (uint16_t j = start_op; j < end_op; ++j) {
            uint32_t op = spirv_code[k + j];
            if (op < bound) {
              if (depends_on_frag_coord[op] && !depends_on_frag_coord[res_id]) { depends_on_frag_coord[res_id] = 1; changed = true; }
              if (is_dither_noise[op] && !is_dither_noise[res_id]) { is_dither_noise[res_id] = 1; changed = true; }
              if (depends_on_sample[op] && !depends_on_sample[res_id]) { depends_on_sample[res_id] = 1; changed = true; }
            }
          }
        }

        k += length;
      }
    }

    // Step A: Backward Slice from inlined Demote/Discard labels
    std::vector<uint8_t> is_in_demote_slice(bound, 0);
    uint32_t cur_label = 0;
    {
      size_t k = 5;
      while (k < word_count) {
        uint32_t w = spirv_code[k];
        uint16_t op = w & 0xFFFF;
        uint16_t l = (w >> 16) & 0xFFFF;
        if (l == 0 || (k + l) > word_count) break;

        if (op == SPV_OP_LABEL && l >= 2) {
          cur_label = spirv_code[k + 1];
        } else if (is_discard_opcode(op)) {
          if (cur_label < bound) {
            uint32_t cid = cond_for_label[cur_label];
            if (cid > 0 && cid < bound) is_in_demote_slice[cid] = 1;
          }
        }
        k += l;
      }

      for (int step = 0; step < 8; ++step) {
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

    uint32_t demote_modified_count = 0;
    uint32_t demote_preserved_count = 0;
    uint32_t nmin_uncoupled_count = 0;

    // Uncouple NMin in demote slice
    size_t k = 5;
    while (k < word_count) {
      uint32_t word = spirv_code[k];
      uint16_t opcode = word & 0xFFFF;
      uint16_t length = (word >> 16) & 0xFFFF;
      if (length == 0 || (k + length) > word_count) break;

      if (opcode == SPV_OP_EXT_INST && length >= 7) {
        uint32_t inst = spirv_code[k + 4];
        if (inst == 79 /* NMin */ || inst == 28 /* FMin */) {
          uint32_t res_id = spirv_code[k + 2];
          uint32_t op1 = spirv_code[k + 5];
          uint32_t op2 = spirv_code[k + 6];
          if (op1 < bound && op2 < bound && res_id < bound && is_in_demote_slice[res_id]) {
            bool op1_dither = is_dither_noise[op1];
            bool op2_dither = is_dither_noise[op2];
            if (op1_dither ^ op2_dither) {
              uint32_t survivor_op = op1_dither ? op2 : op1;
              if (survivor_op < bound && !is_const_zero_or_neg[survivor_op]) {
                spirv_code[k] = (4 << 16) | SPV_OP_COPY_OBJECT;
                spirv_code[k + 3] = survivor_op;
                for (uint16_t n = 4; n < length && (k + n) < word_count; ++n) {
                  spirv_code[k + n] = (1 << 16) | SPV_OP_NOP;
                }
                demote_modified_count++;
                nmin_uncoupled_count++;
                is_dither_noise[res_id] = 0;
              }
            }
          }
        }
      }
      k += length;
    }

    if (nmin_uncoupled_count > 0) {
      for (int step = 0; step < 4; ++step) {
        k = 5;
        while (k < word_count) {
          uint32_t word = spirv_code[k];
          uint16_t opcode = word & 0xFFFF;
          uint16_t length = (word >> 16) & 0xFFFF;
          if (length == 0 || (k + length) > word_count) break;

          uint32_t res_id = 0;
          if (length >= 3 &&
              opcode != SPV_OP_DECORATE && opcode != SPV_OP_MEMBER_DECORATE &&
              opcode != SPV_OP_STORE && opcode != SPV_OP_BRANCH &&
              opcode != SPV_OP_BRANCH_CONDITIONAL && opcode != SPV_OP_KILL &&
              opcode != SPV_OP_RETURN && opcode != SPV_OP_RETURN_VALUE &&
              opcode != SPV_OP_TERMINATE_INVOCATION &&
              opcode != SPV_OP_DEMOTE_TO_HELPER_INVOCATION) {
            res_id = (opcode == SPV_OP_VARIABLE && length >= 3) ? spirv_code[k + 2] :
                     (opcode >= 19 && opcode <= 54 && opcode != 59) ? spirv_code[k + 1] : spirv_code[k + 2];
          }

          if (opcode == SPV_OP_COPY_OBJECT && length >= 4) {
            uint32_t src = spirv_code[k + 3];
            if (src < bound && res_id < bound) {
              is_dither_noise[res_id] = is_dither_noise[src];
            }
          } else if (res_id > 0 && res_id < bound && is_dither_noise[res_id]) {
            bool still_dither = false;
            uint16_t start_op = (opcode == SPV_OP_EXT_INST) ? 5 : 3;
            for (uint16_t j = start_op; j < length; ++j) {
              uint32_t op = spirv_code[k + j];
              if (op < bound && is_dither_noise[op]) { still_dither = true; break; }
            }
            if (!still_dither && !is_dither_const[res_id]) {
              is_dither_noise[res_id] = 0;
            }
          }
          k += length;
        }
      }
    }

    // Step B: NOP pure dither demotes
    cur_label = 0;
    k = 5;
    while (k < word_count) {
      uint32_t word = spirv_code[k];
      uint16_t opcode = word & 0xFFFF;
      uint16_t length = (word >> 16) & 0xFFFF;
      if (length == 0 || (k + length) > word_count) break;

      if (opcode == SPV_OP_LABEL && length >= 2) {
        cur_label = spirv_code[k + 1];
      } else if (is_discard_opcode(opcode)) {
        uint32_t cond = (cur_label < bound) ? cond_for_label[cur_label] : 0;
        bool should_nop = false;

        if (is_force_mode) {
          should_nop = true;
        } else if (cond > 0 && cond < bound && is_dither_noise[cond]) {
          should_nop = true;
        }

        if (should_nop) {
          demote_modified_count++;
          for (uint16_t n = 0; n < length && (k + n) < word_count; ++n) {
            spirv_code[k + n] = (1 << 16) | SPV_OP_NOP;
          }
        } else {
          demote_preserved_count++;
        }
      }
      k += length;
    }

    if ((demote_modified_count + demote_preserved_count) > 0) {
      game_logger::log_msg("[反虚化驱动层-DXVK] 着色器: 0x%08x | 字长: %zu | 消除虚化: %u 处 (解耦: %u) | 保留正常裁剪: %u 处\n",
              shader_hash, word_count, demote_modified_count, nmin_uncoupled_count, demote_preserved_count);
    }

    if (game_logger::g_dump_enabled && (demote_modified_count > 0 || demote_preserved_count > 0)) {
      game_logger::dump_shader_bundle(orig_copy.data(), orig_copy.size(),
                                      spirv_code, word_count,
                                      shader_hash, demote_modified_count, demote_preserved_count,
                                      has_any_frag_coord, has_any_sample);
    }
  }

  inline void process_spirv_anti_dither(uint32_t* spirv_code, size_t word_count) {
    if (!spirv_code || word_count < 5)
      return;

    if (!is_layer_active())
      return;

    if (spirv_code[0] != SPV_HEADER_MAGIC)
      return;

    if (!is_dxvk_translation_layer(spirv_code, word_count)) {
      wuwa_vkd3d::process_spirv_anti_dither(spirv_code, word_count);
    } else {
      process_spirv_dxvk_inline(spirv_code, word_count);
    }
  }

} // namespace wuwa_layer
