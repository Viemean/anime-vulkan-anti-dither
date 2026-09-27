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

namespace tof_vkd3d {

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
    return (opcode >= 87 && opcode <= 107) || (opcode >= 305 && opcode <= 318) || opcode == 86;
  }

  /*
   * 幻塔 (Tower of Fantasy - UE4 / VKD3D-Proton) 反虚化驱动模块
   *
   * 处理逻辑：
   * 1. 材质分类定界与保护底线：
   *    - 大世界植被与树叶材质：保持纹理透明度镂空（Alpha Cutout），凡条件依赖纹理采样的 Demote 100% 保护，绝不中和。
   *    - 角色卡通材质与消隐分支：上游解耦 NMin/FMin 与 FAdd 中的 DitherTemporalAA / Bayer 点阵，下游中和 1000.0f 噪声与点阵 Demote。
   * 2. 执行策略：
   *    - 基于 SSA 数据流双向传播追踪，区分贴图采样与屏幕空间噪声；
   *    - 上游解耦彻底剥离 GBuffer 属性与透明通道中的棋盘格；
   *    - 下游定向中和消除像素丢弃。
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
      game_logger::log_msg("[反虚化驱动层-TOF-VKD3D] 着色器 0x%08x 在黑名单中，跳过处理\n", shader_hash);
      return;
    }

    bool is_force_mode = (game_logger::g_force_hashes.count(shader_hash) > 0);

    // 仅处理 VKD3D 转译的游戏着色器 (Tool ID 30017)，排除驱动内部元着色器
    uint32_t generator = spirv_code[2];
    uint32_t tool_id = (generator >> 16);
    if (tool_id != 30017 && !is_force_mode) {
      return;
    }

    // 着色器若不含 discard/demote 指令且不含 NMin/FMin 上游解耦潜力，提前退出
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
    std::vector<uint8_t> is_dither_noise(bound, 0);
    std::vector<uint32_t> cond_for_label(bound, 0);

    bool has_any_frag_coord = false;
    bool has_any_sample = false;
    bool has_dither_noise_sig = false;
    uint32_t demote_count = 0;

    // Pass 1: 扫描 FragCoord、常量与特征标记
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

          // 相机近景消隐 1000.0f 与 0.001f 常量
          if ((fu.f >= 999.0f && fu.f <= 1001.0f) || (fu.f >= 0.0009f && fu.f <= 0.0011f)) {
            if (res_id < bound) {
              is_dither_const[res_id] = 1;
              is_dither_noise[res_id] = 1;
              has_dither_noise_sig = true;
            }
          }
          // DitherTemporalAA / Bayer 点阵常量特征
          else if ((std::abs(fu.f - 0.166666672f) < 0.0001f) ||
                   (std::abs(fu.f - 0.015625f) < 0.0001f) ||
                   (std::abs(fu.f - 0.0625f) < 0.0001f) ||
                   (std::abs(fu.f - 0.25f) < 0.0001f) ||
                   (std::abs(fu.f - 0.333299994f) < 0.0001f)) {
            if (res_id < bound) {
              is_dither_const[res_id] = 1;
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

    // Pass 2: SSA 数据流级联依赖传播
    bool changed = true;
    uint32_t iteration = 0;
    constexpr uint32_t max_iterations = 24;

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

        // 识别 DitherTemporalAA 节点取模运算 (% 5)
        if (opcode == SPV_OP_UMOD && length >= 5) {
          if (res_id < bound && !is_dither_noise[res_id]) {
            is_dither_noise[res_id] = 1;
            has_dither_noise_sig = true;
            changed = true;
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
            if (is_dither_const[base_ptr] && res_id < bound && !is_dither_const[res_id]) {
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
                has_dither_noise_sig = true;
                changed = true;
              }
            }
          }
        }

        // 通用数据流级联传播；排除 AccessChain 避免索引污染，贴图采样产物不继承噪声属性
        if (res_id > 0 && res_id < bound && opcode != SPV_OP_ACCESS_CHAIN && opcode != 66) {
          uint16_t start_op = (opcode == SPV_OP_EXT_INST) ? 5 : 3;
          uint16_t end_op = length;
          if (opcode == SPV_OP_COMPOSITE_EXTRACT) end_op = 4;

          for (uint16_t k = start_op; k < end_op && (i + k) < word_count; ++k) {
            uint32_t op_val = spirv_code[i + k];
            if (op_val < bound) {
              if (!is_image_sample_opcode(opcode) && is_dither_noise[op_val] && !is_dither_noise[res_id]) {
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

    // 排除小字长非游戏着色器 Quad
    if (!has_any_sample && !has_dither_noise_sig && word_count < 800 && !is_force_mode) {
      if (game_logger::g_dump_enabled) {
        game_logger::dump_shader_bundle(orig_copy.data(), orig_copy.size(),
                                        spirv_code, word_count,
                                        shader_hash, 0, demote_count,
                                        has_any_frag_coord, has_any_sample);
      }
      return;
    }

    // Pass 3: 上游解耦与下游中和
    size_t i = 5;
    uint32_t current_label = 0;
    uint32_t noped_count = 0;
    uint32_t preserved_cutout_count = 0;

    while (i < word_count) {
      uint32_t word = spirv_code[i];
      uint16_t opcode = word & 0xFFFF;
      uint16_t length = (word >> 16) & 0xFFFF;
      if (length == 0 || (i + length) > word_count)
        break;

      if (opcode == SPV_OP_LABEL && length >= 2) {
        current_label = spirv_code[i + 1];
      }
      // 上游极值解耦：剥离 NMin/FMin 中的 Dither 噪声操作数
      else if (opcode == SPV_OP_EXT_INST && length >= 7) {
        uint32_t inst = spirv_code[i + 4];
        if (inst == 37 || inst == 79) { // GLSL.std.450 FMin (37) 或 NMin (79)
          uint32_t op1 = spirv_code[i + 5];
          uint32_t op2 = spirv_code[i + 6];
          if (op2 < bound && is_dither_noise[op2] && !is_dither_noise[op1]) {
            spirv_code[i + 6] = op1;
            noped_count++;
          } else if (op1 < bound && is_dither_noise[op1] && !is_dither_noise[op2]) {
            spirv_code[i + 5] = op2;
            noped_count++;
          }
        }
      }
      // 下游 Demote 精准决策与保护铁律
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
      game_logger::log_msg("[反虚化驱动层-TOF-VKD3D] 着色器: 0x%08x | 字长: %zu | 消除虚化: %u 处 | 保留正常裁剪: %u 处 | 类别: %s\n",
                           shader_hash, word_count, noped_count, preserved_cutout_count, category);
    }

    if (game_logger::g_dump_enabled && (noped_count > 0 || preserved_cutout_count > 0)) {
      game_logger::dump_shader_bundle(orig_copy.data(), orig_copy.size(),
                                      spirv_code, word_count,
                                      shader_hash, noped_count, preserved_cutout_count,
                                      has_any_frag_coord, has_any_sample);
    }
  }

} // namespace tof_vkd3d
