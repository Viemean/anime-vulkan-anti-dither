#pragma once

#include "../logger.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>
#include <unordered_set>
#include <unordered_map>
#include <string_view>

namespace tof_dxvk {

  constexpr uint32_t SPV_HEADER_MAGIC                       = 0x07230203;
  constexpr uint32_t SPV_OP_NOP                             = 0;
  constexpr uint32_t SPV_OP_NAME                            = 5;
  constexpr uint32_t SPV_OP_EXT_INST                        = 12;
  constexpr uint32_t SPV_OP_ENTRY_POINT                     = 15;
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
  constexpr uint32_t SPV_OP_COPY_OBJECT                     = 83;
  constexpr uint32_t SPV_OP_UMOD                            = 137;
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

  inline bool is_discard_opcode(uint16_t opcode) {
    return opcode == SPV_OP_KILL ||
           opcode == SPV_OP_TERMINATE_INVOCATION ||
           opcode == SPV_OP_DEMOTE_TO_HELPER_INVOCATION ||
           opcode == SPV_OP_DEMOTE_TO_HELPER_INVOCATION_EXT;
  }

  inline bool is_image_sample_opcode(uint16_t opcode) {
    return (opcode >= 87 && opcode <= 107) || (opcode >= 305 && opcode <= 318);
  }

  /*
   * 幻塔 (Tower of Fantasy - UE4 / DXVK - DirectX 11) 反虚化驱动模块
   *
   * 核心处理逻辑：
   * 1. DX11/DXVK 着色器形态适配：
   *    - 捕获 OpKill (252) 与 OpDemoteToHelperInvocation (5380)；
   *    - 识别 OpConstantComposite 打包的 Bayer 矩阵向量 (v4float) 与标量常量；
   *    - 支持通过 OpCompositeExtract 提取的标量噪声追踪。
   * 2. 目标达成与保护铁律：
   *    - 角色 Toon/DitherTemporalAA/近景消隐分支精准消除；
   *    - 凡依赖贴图采样的 Alpha Cutout（植被/树木/树叶/布料边缘）100% 绝对保护；
   *    - 排除小字长纯写 0 遮罩着色器，防止产生黑色空洞。
   */
  inline void process_spirv_anti_dither(uint32_t* spirv_code, size_t word_count) {
    if (!spirv_code || word_count < 5)
      return;

    if (!game_logger::is_active())
      return;

    if (spirv_code[0] != SPV_HEADER_MAGIC)
      return;

    uint32_t bound = spirv_code[3];
    if (bound == 0 || bound > 1048576)
      return;

    uint32_t shader_hash = game_logger::compute_spirv_hash(spirv_code, word_count);

    if (game_logger::g_exclude_hashes.count(shader_hash) > 0) {
      game_logger::log_msg("[反虚化驱动层-TOF-DXVK] 着色器 0x%08x 在黑名单中，跳过处理\n", shader_hash);
      return;
    }

    bool is_force_mode = (game_logger::g_force_hashes.count(shader_hash) > 0);

    // 检查是否存在像素丢弃指令 (OpKill / OpDemote)
    bool has_any_discard = false;
    for (size_t k = 5; k < word_count; ) {
      uint32_t w = spirv_code[k];
      uint16_t op = w & 0xFFFF;
      uint16_t l = (w >> 16) & 0xFFFF;
      if (l == 0 || (k + l) > word_count) break;
      if (is_discard_opcode(op)) {
        has_any_discard = true;
        break;
      }
      k += l;
    }
    if (!has_any_discard && !is_force_mode) {
      return;
    }

    std::vector<uint32_t> orig_copy;
    if (game_logger::g_dump_enabled) {
      orig_copy.assign(spirv_code, spirv_code + word_count);
    }

    std::vector<uint8_t> is_frag_coord_var(bound, 0);
    std::vector<uint8_t> depends_on_frag_coord(bound, 0);
    std::vector<uint8_t> depends_on_sample(bound, 0);
    std::vector<uint8_t> is_dither_const(bound, 0);
    std::vector<uint8_t> is_bayer_fraction(bound, 0);
    std::vector<uint8_t> is_dither_noise(bound, 0);
    std::vector<uint32_t> cond_for_label(bound, 0);

    bool has_any_frag_coord = false;
    bool has_any_sample = false;
    uint32_t demote_count = 0;

    // Pass 1: 扫描 FragCoord、标量与复合常量
    {
      size_t i = 5;
      while (i < word_count) {
        uint32_t word = spirv_code[i];
        uint16_t opcode = word & 0xFFFF;
        uint16_t length = (word >> 16) & 0xFFFF;
        if (length == 0 || (i + length) > word_count)
          break;

        if (opcode == SPV_OP_DECORATE && length >= 4) {
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

          // 1. 相机近景消隐 1000.0f 与 0.001f 常量
          if ((fu.f >= 999.0f && fu.f <= 1001.0f) || (fu.f >= 0.0009f && fu.f <= 0.0011f)) {
            if (res_id < bound) {
              is_dither_const[res_id] = 1;
              is_dither_noise[res_id] = 1;
            }
          }
          // 2. 标量 Bayer Table 分数 (0/9..8/9) 与点阵常量
          else if ((fu.f >= -0.0001f && fu.f <= 0.0001f) ||
                   (fu.f >= 0.1111f && fu.f <= 0.1112f) ||
                   (fu.f >= 0.2222f && fu.f <= 0.2223f) ||
                   (fu.f >= 0.3333f && fu.f <= 0.3334f) ||
                   (fu.f >= 0.4444f && fu.f <= 0.4445f) ||
                   (fu.f >= 0.5555f && fu.f <= 0.5556f) ||
                   (fu.f >= 0.6666f && fu.f <= 0.6667f) ||
                   (fu.f >= 0.7777f && fu.f <= 0.7778f) ||
                   (fu.f >= 0.8888f && fu.f <= 0.8889f) ||
                   (std::abs(fu.f - 0.166666672f) < 0.0001f) ||
                   (std::abs(fu.f - 0.015625f) < 0.0001f) ||
                   (std::abs(fu.f - 0.0625f) < 0.0001f) ||
                   (std::abs(fu.f - 0.25f) < 0.0001f) ||
                   (std::abs(fu.f - 0.333299994f) < 0.0001f)) {
            if (res_id < bound) {
              is_bayer_fraction[res_id] = 1;
              is_dither_const[res_id] = 1;
            }
          }
          // 3. 屏幕空间 Interleaved Gradient Noise (IGN) 标量常量
          else if ((fu.f >= 0.06711f && fu.f <= 0.06712f) ||
                   (fu.f >= 0.005837f && fu.f <= 0.005838f) ||
                   (fu.f >= 52.982f && fu.f <= 52.983f)) {
            if (res_id < bound) {
              is_dither_const[res_id] = 1;
            }
          }
        } else if (opcode == SPV_OP_CONSTANT_COMPOSITE && length >= 4) {
          // DXVK 专有：识别打包在 v4float 中的 Bayer 矩阵向量
          uint32_t res_id = spirv_code[i + 2];
          uint32_t bayer_fraction_matches = 0;
          uint32_t dither_const_matches = 0;
          for (uint16_t c = 3; c < length; ++c) {
            uint32_t const_id = spirv_code[i + c];
            if (const_id < bound) {
              if (is_bayer_fraction[const_id]) bayer_fraction_matches++;
              if (is_dither_const[const_id]) dither_const_matches++;
            }
          }
          if ((bayer_fraction_matches >= 3 && length >= 6) ||
              (dither_const_matches >= 2 && length >= 5) ||
              (bayer_fraction_matches >= 6)) {
            if (res_id < bound) {
              is_dither_const[res_id] = 1;
              is_dither_noise[res_id] = 1;
            }
          }
        } else if (opcode == SPV_OP_VARIABLE && length >= 5) {
          uint32_t res_id = spirv_code[i + 2];
          uint32_t initializer = spirv_code[i + 4];
          if (initializer < bound && (is_dither_const[initializer] || is_dither_noise[initializer])) {
            if (res_id < bound) {
              is_dither_const[res_id] = 1;
              is_dither_noise[res_id] = 1;
            }
          }
        } else if (is_discard_opcode(opcode)) {
          demote_count++;
        }

        i += length;
      }
    }

    if (demote_count == 0 && !is_force_mode) {
      return;
    }

    // Pass 2a: FragCoord 依赖先导收敛传播 (确保屏幕空间坐标提前收敛，防止误将噪声贴图采样判定为材质采样)
    bool fc_changed = true;
    uint32_t fc_iteration = 0;
    while (fc_changed && fc_iteration < 20) {
      fc_changed = false;
      fc_iteration++;
      size_t i = 5;
      while (i < word_count) {
        uint32_t word = spirv_code[i];
        uint16_t opcode = word & 0xFFFF;
        uint16_t length = (word >> 16) & 0xFFFF;
        if (length == 0 || (i + length) > word_count)
          break;

        uint32_t res_id = 0;
        if (length >= 3 &&
            opcode != SPV_OP_DECORATE && opcode != SPV_OP_MEMBER_DECORATE &&
            opcode != SPV_OP_STORE && opcode != SPV_OP_BRANCH &&
            opcode != SPV_OP_BRANCH_CONDITIONAL && opcode != SPV_OP_KILL &&
            opcode != SPV_OP_RETURN && opcode != SPV_OP_RETURN_VALUE &&
            opcode != SPV_OP_TERMINATE_INVOCATION &&
            opcode != SPV_OP_DEMOTE_TO_HELPER_INVOCATION) {
          res_id = (opcode >= 19 && opcode <= 39) ? spirv_code[i + 1] : spirv_code[i + 2];
        }

        if (opcode == SPV_OP_LOAD && length >= 4) {
          uint32_t ptr_id = spirv_code[i + 3];
          if (ptr_id < bound && is_frag_coord_var[ptr_id] && res_id < bound && !depends_on_frag_coord[res_id]) {
            depends_on_frag_coord[res_id] = 1;
            fc_changed = true;
          }
        } else if ((opcode == SPV_OP_ACCESS_CHAIN || opcode == 66) && length >= 4) {
          uint32_t base_ptr = spirv_code[i + 3];
          if (base_ptr < bound && is_frag_coord_var[base_ptr] && res_id < bound && !is_frag_coord_var[res_id]) {
            is_frag_coord_var[res_id] = 1;
            depends_on_frag_coord[res_id] = 1;
            fc_changed = true;
          }
        } else if (res_id > 0 && res_id < bound && opcode != SPV_OP_ACCESS_CHAIN && opcode != 66) {
          uint16_t start_op = (opcode == SPV_OP_EXT_INST) ? 5 : 3;
          bool parent_is_frag_coord = false;
          for (uint16_t k = start_op; k < length; ++k) {
            uint32_t op_id = spirv_code[i + k];
            if (op_id < bound && depends_on_frag_coord[op_id]) {
              parent_is_frag_coord = true;
              break;
            }
          }
          if (parent_is_frag_coord && !depends_on_frag_coord[res_id]) {
            depends_on_frag_coord[res_id] = 1;
            fc_changed = true;
          }
        }
        i += length;
      }
    }

    // Pass 2b: SSA 数据流级联依赖传播
    bool changed = true;
    uint32_t iteration = 0;
    constexpr uint32_t max_iterations = 28;

    while (changed && iteration < max_iterations) {
      changed = false;
      iteration++;
      size_t i = 5;

      while (i < word_count) {
        uint32_t word = spirv_code[i];
        uint16_t opcode = word & 0xFFFF;
        uint16_t length = (word >> 16) & 0xFFFF;
        if (length == 0 || (i + length) > word_count)
          break;

        if (opcode == SPV_OP_BRANCH_CONDITIONAL && length >= 4) {
          uint32_t cond = spirv_code[i + 1];
          uint32_t true_lbl = spirv_code[i + 2];
          uint32_t false_lbl = spirv_code[i + 3];
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
          res_id = (opcode >= 19 && opcode <= 39) ? spirv_code[i + 1] : spirv_code[i + 2];
        }

        // 标记纹理采样输出 (非纯 FragCoord 采样的贴图输出)
        if (is_image_sample_opcode(opcode) && length >= 5) {
          uint32_t coord_id = spirv_code[i + 4];
          if (coord_id < bound && !depends_on_frag_coord[coord_id]) {
            has_any_sample = true;
            if (res_id < bound && !depends_on_sample[res_id]) {
              depends_on_sample[res_id] = 1;
              changed = true;
            }
          }
        }

        // 识别 DitherTemporalAA 取模运算 (% 5)
        if (opcode == SPV_OP_UMOD && length >= 5) {
          if (res_id < bound && !is_dither_noise[res_id]) {
            is_dither_noise[res_id] = 1;
            changed = true;
          }
        }

        // DXVK 专有：从 Dither 向量提取标量 (OpCompositeExtract)
        if (opcode == SPV_OP_COMPOSITE_EXTRACT && length >= 5) {
          uint32_t composite_id = spirv_code[i + 3];
          if (composite_id < bound) {
            if (is_dither_noise[composite_id] && res_id < bound && !is_dither_noise[res_id]) {
              is_dither_noise[res_id] = 1;
              changed = true;
            }
            if (depends_on_sample[composite_id] && res_id < bound && !depends_on_sample[res_id]) {
              depends_on_sample[res_id] = 1;
              changed = true;
            }
          }
        }

        // DXVK 专有：向量构造 (OpCompositeConstruct)
        if (opcode == SPV_OP_COMPOSITE_CONSTRUCT && length >= 4) {
          bool any_noise = false;
          bool any_sample = false;
          for (uint16_t c = 3; c < length; ++c) {
            uint32_t op_id = spirv_code[i + c];
            if (op_id < bound) {
              if (is_dither_noise[op_id]) any_noise = true;
              if (depends_on_sample[op_id]) any_sample = true;
            }
          }
          if (res_id < bound) {
            if (any_noise && !is_dither_noise[res_id]) {
              is_dither_noise[res_id] = 1;
              changed = true;
            }
            if (any_sample && !depends_on_sample[res_id]) {
              depends_on_sample[res_id] = 1;
              changed = true;
            }
          }
        }

        // Load 变量传播
        if (opcode == SPV_OP_LOAD && length >= 4) {
          uint32_t ptr_id = spirv_code[i + 3];
          if (ptr_id < bound) {
            if (is_frag_coord_var[ptr_id] && res_id < bound && !depends_on_frag_coord[res_id]) {
              depends_on_frag_coord[res_id] = 1;
              changed = true;
            }
            if ((is_dither_const[ptr_id] || is_dither_noise[ptr_id]) && res_id < bound && !is_dither_noise[res_id]) {
              is_dither_noise[res_id] = 1;
              changed = true;
            }
          }
        } else if ((opcode == SPV_OP_ACCESS_CHAIN || opcode == 66) && length >= 4) {
          uint32_t base_ptr = spirv_code[i + 3];
          if (base_ptr < bound) {
            if (is_frag_coord_var[base_ptr] && res_id < bound && !is_frag_coord_var[res_id]) {
              is_frag_coord_var[res_id] = 1;
              depends_on_frag_coord[res_id] = 1;
              changed = true;
            }
            if ((is_dither_const[base_ptr] || is_dither_noise[base_ptr]) && res_id < bound && !is_dither_noise[res_id]) {
              is_dither_const[res_id] = 1;
              is_dither_noise[res_id] = 1;
              changed = true;
            }
          }
        }

        // FragCoord 与 Dither 常量的点积运算标记为噪声源
        if (opcode == SPV_OP_DOT && length >= 5) {
          uint32_t op1 = spirv_code[i + 3];
          uint32_t op2 = spirv_code[i + 4];
          if (op1 < bound && op2 < bound) {
            if ((is_dither_const[op1] || is_dither_const[op2]) &&
                (depends_on_frag_coord[op1] || depends_on_frag_coord[op2])) {
              if (res_id < bound && !is_dither_noise[res_id]) {
                is_dither_noise[res_id] = 1;
                changed = true;
              }
            }
          }
        }

        // 通用数据流级联传播；排除 AccessChain 与 SampledImage 避免污染
        if (res_id > 0 && res_id < bound && opcode != SPV_OP_ACCESS_CHAIN && opcode != 66 && opcode != 86) {
          uint16_t start_op = (opcode == SPV_OP_EXT_INST) ? 5 : 3;
          bool parent_is_noise = false;
          bool parent_is_sample = false;
          bool parent_is_frag_coord = false;

          for (uint16_t k = start_op; k < length; ++k) {
            uint32_t op_id = spirv_code[i + k];
            if (op_id < bound) {
              if (is_dither_noise[op_id]) parent_is_noise = true;
              if (depends_on_sample[op_id]) parent_is_sample = true;
              if (depends_on_frag_coord[op_id]) parent_is_frag_coord = true;
            }
          }

          if (parent_is_frag_coord && !depends_on_frag_coord[res_id]) {
            depends_on_frag_coord[res_id] = 1;
            changed = true;
          }
          if (parent_is_sample && !depends_on_sample[res_id]) {
            depends_on_sample[res_id] = 1;
            changed = true;
          }
          if (parent_is_noise && !depends_on_sample[res_id] && !is_dither_noise[res_id]) {
            is_dither_noise[res_id] = 1;
            changed = true;
          }
        }

        i += length;
      }
    }

    // 排除无纹理采样的小字长遮罩与深度着色器 (保护眼眶镂空避免黑色空洞)
    if (!has_any_sample && word_count < 800 && !is_force_mode) {
      if (game_logger::g_dump_enabled) {
        game_logger::dump_shader_bundle(orig_copy.data(), orig_copy.size(),
                                        spirv_code, word_count,
                                        shader_hash, 0, 1,
                                        has_any_frag_coord, has_any_sample);
      }
      return;
    }

    // Pass 3: 执行上游解耦与下游中和
    uint32_t noped_count = 0;
    uint32_t preserved_cutout_count = 0;
    uint32_t current_label = 0;

    size_t i = 5;
    while (i < word_count) {
      uint32_t word = spirv_code[i];
      uint16_t opcode = word & 0xFFFF;
      uint16_t length = (word >> 16) & 0xFFFF;
      if (length == 0 || (i + length) > word_count)
        break;

      if (opcode == SPV_OP_LABEL && length >= 2) {
        current_label = spirv_code[i + 1];
      }
      // 上游极值解耦：GLSL.std.450 FMin (37) 或 NMin (79)
      else if (opcode == SPV_OP_EXT_INST && length >= 7) {
        uint32_t inst_type = spirv_code[i + 4];
        if (inst_type == 37 /* FMin */ || inst_type == 79 /* NMin */) {
          uint32_t op1 = spirv_code[i + 5];
          uint32_t op2 = spirv_code[i + 6];
          if (op1 < bound && op2 < bound) {
            if (is_dither_noise[op1] && !is_dither_noise[op2]) {
              spirv_code[i + 5] = op2;
              noped_count++;
            } else if (is_dither_noise[op2] && !is_dither_noise[op1]) {
              spirv_code[i + 6] = op1;
              noped_count++;
            }
          }
        }
      }
      // 上游解耦：剥离 LogicalOr 中的 Dither 噪声分支
      else if (opcode == 166 /* OpLogicalOr */ && length >= 5) {
        uint32_t op1 = spirv_code[i + 3];
        uint32_t op2 = spirv_code[i + 4];
        if (op1 < bound && op2 < bound) {
          if (is_dither_noise[op1] && !is_dither_noise[op2]) {
            spirv_code[i + 3] = op2;
            noped_count++;
          } else if (is_dither_noise[op2] && !is_dither_noise[op1]) {
            spirv_code[i + 4] = op1;
            noped_count++;
          }
        }
      }
      // 下游 Demote/Kill 精准决策与保护铁律
      else if (is_discard_opcode(opcode)) {
        uint32_t cond = (current_label < bound) ? cond_for_label[current_label] : 0;
        bool should_nop = false;

        if (is_force_mode) {
          should_nop = true;
        } else if (cond > 0 && cond < bound) {
          // 保护铁律：凡条件依赖贴图采样的 Alpha Cutout，绝对保留，严禁 NOP
          if (depends_on_sample[cond]) {
            should_nop = false;
          } else if (is_dither_noise[cond]) {
            should_nop = true;
          }
        }

        if (should_nop) {
          for (uint16_t k = 0; k < length && (i + k) < word_count; ++k) {
            spirv_code[i + k] = (1 << 16) | SPV_OP_NOP;
          }
          noped_count++;
        } else {
          preserved_cutout_count++;
        }
      }

      i += length;
    }

    if ((noped_count + preserved_cutout_count) > 0) {
      const char* category = (noped_count > 0) ? "角色网点/虚化(Toon Dither/Noise)" : (has_any_sample ? "植被/材质镂空(Alpha Cutout)" : "几何裁剪");
      game_logger::log_msg("[反虚化驱动层-TOF-DXVK] 着色器: 0x%08x | 字长: %zu | 消除虚化: %u 处 | 保留正常裁剪: %u 处 | 类别: %s\n",
                           shader_hash, word_count, noped_count, preserved_cutout_count, category);
    }

    if (game_logger::g_dump_enabled && (noped_count > 0 || preserved_cutout_count > 0)) {
      game_logger::dump_shader_bundle(orig_copy.data(), orig_copy.size(),
                                      spirv_code, word_count,
                                      shader_hash, noped_count, preserved_cutout_count,
                                      has_any_frag_coord, has_any_sample);
    }
  }

} // namespace tof_dxvk
