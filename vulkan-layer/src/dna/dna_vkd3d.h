#pragma once

#include "../logger.h"

#include <cstddef>
#include <cstdint>
#include <vector>
#include <cmath>

namespace dna_vkd3d {

  constexpr uint32_t SPV_HEADER_MAGIC = 0x07230203;

  // SPIR-V Core & Extended opcodes
  constexpr uint16_t SPV_OP_NOP = 0;
  constexpr uint16_t SPV_OP_EXT_INST = 12;
  constexpr uint16_t SPV_OP_CONSTANT = 43;
  constexpr uint16_t SPV_OP_CONSTANT_COMPOSITE = 44;
  constexpr uint16_t SPV_OP_VARIABLE = 59;
  constexpr uint16_t SPV_OP_LOAD = 61;
  constexpr uint16_t SPV_OP_ACCESS_CHAIN = 65;
  constexpr uint16_t SPV_OP_DECORATE = 71;
  constexpr uint16_t SPV_OP_VECTOR_SHUFFLE = 79;
  constexpr uint16_t SPV_OP_COMPOSITE_CONSTRUCT = 80;
  constexpr uint16_t SPV_OP_COMPOSITE_EXTRACT = 81;
  constexpr uint16_t SPV_OP_COPY_OBJECT = 83;
  constexpr uint16_t SPV_OP_FMUL = 133;
  constexpr uint16_t SPV_OP_FADD = 129;
  constexpr uint16_t SPV_OP_FSUB = 131;
  constexpr uint16_t SPV_OP_FDIV = 135;
  constexpr uint16_t SPV_OP_DOT = 148;
  constexpr uint16_t SPV_OP_LOGICAL_OR = 166;
  constexpr uint16_t SPV_OP_LOGICAL_AND = 167;
  constexpr uint16_t SPV_OP_LOGICAL_NOT = 168;
  constexpr uint16_t SPV_OP_FORDERED_LESS_THAN = 184;
  constexpr uint16_t SPV_OP_LABEL = 248;
  constexpr uint16_t SPV_OP_BRANCH_CONDITIONAL = 250;
  constexpr uint16_t SPV_OP_KILL = 252;
  constexpr uint16_t SPV_OP_DEMOTE_TO_HELPER = 5380;

  constexpr uint32_t SPV_DECORATION_BUILTIN = 11;
  constexpr uint32_t SPV_BUILTIN_FRAG_COORD = 15;
  constexpr uint32_t SPV_BUILTIN_SAMPLE_POSITION = 16;

  union FloatUint {
    float f;
    uint32_t u;
  };

  inline bool is_discard_opcode(uint16_t opcode) {
    return opcode == SPV_OP_DEMOTE_TO_HELPER || opcode == SPV_OP_KILL;
  }

  inline bool is_image_sample_opcode(uint16_t opcode) {
    return (opcode >= 87 && opcode <= 93) || (opcode >= 95 && opcode <= 104) ||
           (opcode >= 106 && opcode <= 112);
  }

  /**
   * 二重螺旋 (Duet Night Abyss / DNA) VKD3D-Proton 反虚化核心算法
   * 基于 SSA 数据流双向传播追踪：
   * 1. 采集 FragCoord BuiltIn 与 UE4 PseudoRandom 噪声算法特征常量 (347.8345f, 3343.2837f, 1000.0f, 0.001f)；
   * 2. 追踪计算图中的屏幕空间 Bayer/PseudoRandom 噪声派生流与贴图采样流；
   * 3. 区分并消除角色透视网点虚化 (Demote/Kill)，100% 保护非角色物体 (植被 Alpha Cutout、树木、技能与环境特效)。
   */
  inline void process_spirv(uint32_t* spirv_code, size_t word_count) {
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
      game_logger::log_msg("[反虚化驱动层-DNA-VKD3D] 着色器 0x%08x 在黑名单中，跳过处理\n", shader_hash);
      return;
    }

    bool in_force_range = (shader_hash >= game_logger::g_force_hash_min && shader_hash <= game_logger::g_force_hash_max);
    bool is_force_mode = (game_logger::g_force_all && in_force_range) || (game_logger::g_force_hashes.count(shader_hash) > 0);

    // 仅处理 VKD3D 转译的游戏着色器 (Tool ID 30017)
    uint32_t generator = spirv_code[2];
    uint32_t tool_id = (generator >> 16);
    if (tool_id != 30017 && !is_force_mode) {
      return;
    }

    // 门禁：检查是否包含 discard / demote 指令
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
    std::vector<uint16_t> def_opcode(bound, 0);
    std::vector<uint32_t> not_operand(bound, 0);
    std::vector<std::pair<uint32_t, uint32_t>> and_operands(bound, {0, 0});
    std::vector<uint8_t> is_cmp_opcode_id(bound, 0);
    std::vector<uint8_t> is_inverted_mask(bound, 0);

    bool has_any_frag_coord = false;
    bool has_any_sample = false;
    bool has_char_dither_feature = false;
    uint32_t demote_count = 0;

    // Pass 1: 扫描 FragCoord 与 UE PseudoRandom 常量特征标记
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

          // UE 角色相机网点虚化标志性特征常量：347.8345f、3343.2837f（剔除通用的 1000.0f）
          if ((fu.f >= 347.83f && fu.f <= 347.84f) ||
              (fu.f >= 3343.28f && fu.f <= 3343.29f)) {
            has_char_dither_feature = true;
            if (res_id < bound) {
              is_dither_const[res_id] = 1;
              is_dither_noise[res_id] = 1;
            }
          }
        } else if (opcode == SPV_OP_CONSTANT_COMPOSITE && length >= 4) {
          uint32_t res_id = spirv_code[i + 2];
          for (uint16_t c = 3; c < length; ++c) {
            uint32_t cid = spirv_code[i + c];
            if (cid < bound && is_dither_const[cid]) {
              if (res_id < bound) {
                is_dither_const[res_id] = 1;
                is_dither_noise[res_id] = 1;
              }
              break;
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
            opcode != SPV_OP_DECORATE && opcode != 72 /* MemberDecorate */ &&
            opcode != 62 /* Store */ && opcode != 249 /* Branch */ &&
            opcode != SPV_OP_BRANCH_CONDITIONAL && opcode != SPV_OP_KILL &&
            opcode != 253 /* Return */ && opcode != 254 /* ReturnValue */ &&
            opcode != SPV_OP_DEMOTE_TO_HELPER && opcode != SPV_OP_LABEL) {
          res_id = (opcode >= 19 && opcode <= 39) ? spirv_code[i + 1] : spirv_code[i + 2];
          if (res_id < bound) {
            def_opcode[res_id] = opcode;
            if (opcode >= 180 && opcode <= 190) {
              is_cmp_opcode_id[res_id] = 1;
            }
            if (opcode == SPV_OP_LOGICAL_NOT && length >= 4) {
              uint32_t op1 = spirv_code[i + 3];
              if (op1 < bound) {
                not_operand[res_id] = op1;
                // 标记反向贴图消隐：严格限定为对贴图采样比较的取反 (Not(SampleAlpha <= Cutoff))
                if (is_cmp_opcode_id[op1] && depends_on_sample[op1]) {
                  if (!is_inverted_mask[res_id]) {
                    is_inverted_mask[res_id] = 1;
                    changed = true;
                  }
                }
              }
            } else if (opcode == SPV_OP_LOGICAL_AND && length >= 5) {
              uint32_t op1 = spirv_code[i + 3];
              uint32_t op2 = spirv_code[i + 4];
              if (op1 < bound && op2 < bound) and_operands[res_id] = {op1, op2};
            }
            if (opcode == SPV_OP_LOGICAL_OR || opcode == SPV_OP_LOGICAL_AND || opcode == SPV_OP_LOGICAL_NOT) {
              if (!is_dither_noise[res_id]) {
                is_dither_noise[res_id] = 1;
                changed = true;
              }
            }
          }
        }

        // 标记纹理采样输出
        if (is_image_sample_opcode(opcode) && length >= 5) {
          has_any_sample = true;
          if (res_id < bound && !depends_on_sample[res_id]) {
            depends_on_sample[res_id] = 1;
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
        } else if ((opcode == SPV_OP_ACCESS_CHAIN || opcode == 66 /* InBoundsAccessChain */) && length >= 4) {
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
        } else if ((opcode >= 137 && opcode <= 141) && length >= 5) { // OpUMod / OpSRem 等屏幕空间点阵取模
          uint32_t op1 = spirv_code[i + 3];
          if (op1 < bound && depends_on_frag_coord[op1] && res_id < bound && !is_dither_noise[res_id]) {
            is_dither_noise[res_id] = 1;
            changed = true;
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
              if (is_inverted_mask[op_val] && !is_inverted_mask[res_id]) {
                is_inverted_mask[res_id] = 1;
                changed = true;
              }
            }
          }
        }

        i += length;
      }
    }

    // Pass 2.5: Demote 切片逆向追溯与上游复合 Dither 噪声解耦 (Upstream Uncoupling)
    // 借鉴 WUWA / TOF / NTE 成熟架构：当材质公式中存在 OpFMul(TextureAlpha, DitherNoise) 或 FMin/NMin 时，
    // 将其改写为 OpCopyObject(TextureAlpha)，彻底剥离近景相机点阵渗透，同时 100% 保护材质原始 Cutout！
    std::vector<uint8_t> is_in_demote_slice(bound, 0);
    {
      size_t k = 5;
      uint32_t cur_lbl = 0;
      while (k < word_count) {
        uint32_t w = spirv_code[k];
        uint16_t op = w & 0xFFFF;
        uint16_t l = (w >> 16) & 0xFFFF;
        if (l == 0 || (k + l) > word_count) break;

        if (op == SPV_OP_LABEL && l >= 2) {
          cur_lbl = spirv_code[k + 1];
        } else if (is_discard_opcode(op)) {
          if (cur_lbl < bound) {
            uint32_t cid = cond_for_label[cur_lbl];
            if (cid > 0 && cid < bound) is_in_demote_slice[cid] = 1;
          }
        }
        k += l;
      }

      // 向上回溯 8 级依赖切片
      for (int step = 0; step < 8; ++step) {
        k = 5;
        while (k < word_count) {
          uint32_t w = spirv_code[k];
          uint16_t op = w & 0xFFFF;
          uint16_t l = (w >> 16) & 0xFFFF;
          if (l == 0 || (k + l) > word_count) break;

          uint32_t res_id = 0;
          if (l >= 3 && op != SPV_OP_DECORATE && op != 72 &&
              op != 62 && op != 249 && op != SPV_OP_BRANCH_CONDITIONAL) {
            res_id = (op >= 19 && op <= 39) ? spirv_code[k + 1] : spirv_code[k + 2];
          }

          if (res_id > 0 && res_id < bound && is_in_demote_slice[res_id]) {
            uint16_t start_op = (op == SPV_OP_EXT_INST) ? 5 : 3;
            uint16_t end_op = (op == SPV_OP_COMPOSITE_EXTRACT) ? 4 : l;
            for (uint16_t j = start_op; j < end_op && (k + j) < word_count; ++j) {
              uint32_t opnd = spirv_code[k + j];
              if (opnd < bound) {
                is_in_demote_slice[opnd] = 1;
              }
            }
          }
          k += l;
        }
      }
    }

    uint32_t uncoupled_count = 0;
    // 执行精准上游解耦
    {
      size_t k = 5;
      while (k < word_count) {
        uint32_t w = spirv_code[k];
        uint16_t op = w & 0xFFFF;
        uint16_t l = (w >> 16) & 0xFFFF;
        if (l == 0 || (k + l) > word_count) break;

        // OpFMul(TextureAlpha, DitherNoise) 乘积解耦
        if (op == SPV_OP_FMUL && l >= 5) {
          uint32_t res_id = spirv_code[k + 2];
          uint32_t op1 = spirv_code[k + 3];
          uint32_t op2 = spirv_code[k + 4];

          if (res_id < bound && is_in_demote_slice[res_id] && op1 < bound && op2 < bound) {
            bool op1_sample = depends_on_sample[op1];
            bool op2_sample = depends_on_sample[op2];
            bool op1_noise = is_dither_noise[op1] || (depends_on_frag_coord[op1] && !op1_sample);
            bool op2_noise = is_dither_noise[op2] || (depends_on_frag_coord[op2] && !op2_sample);

            if ((op1_sample && op2_noise) || (op2_sample && op1_noise)) {
              uint32_t survivor = op1_sample ? op1 : op2;
              spirv_code[k] = (4 << 16) | SPV_OP_COPY_OBJECT;
              spirv_code[k + 3] = survivor;
              for (uint16_t p = 4; p < l && (k + p) < word_count; ++p) {
                spirv_code[k + p] = (1 << 16) | SPV_OP_NOP;
              }
              uncoupled_count++;
            }
          }
        }
        // OpExtInst FMin / NMin 极值解耦
        else if (op == SPV_OP_EXT_INST && l >= 7) {
          uint32_t inst = spirv_code[k + 4];
          if (inst == 37 || inst == 79 || inst == 28) {
            uint32_t res_id = spirv_code[k + 2];
            uint32_t op1 = spirv_code[k + 5];
            uint32_t op2 = spirv_code[k + 6];

            if (res_id < bound && is_in_demote_slice[res_id] && op1 < bound && op2 < bound) {
              bool op1_sample = depends_on_sample[op1];
              bool op2_sample = depends_on_sample[op2];
              bool op1_noise = is_dither_noise[op1] || (depends_on_frag_coord[op1] && !op1_sample);
              bool op2_noise = is_dither_noise[op2] || (depends_on_frag_coord[op2] && !op2_sample);

              if ((op1_sample && op2_noise) || (op2_sample && op1_noise)) {
                uint32_t survivor = op1_sample ? op1 : op2;
                spirv_code[k] = (4 << 16) | SPV_OP_COPY_OBJECT;
                spirv_code[k + 3] = survivor;
                for (uint16_t p = 4; p < l && (k + p) < word_count; ++p) {
                  spirv_code[k + p] = (1 << 16) | SPV_OP_NOP;
                }
                uncoupled_count++;
              }
            }
          }
        }
        // OpLogicalOr(FadeMask, And(!FadeMask, Cutout)) 复合消隐解耦（粉碎头部穹顶网罩）
        else if (op == SPV_OP_LOGICAL_OR && l >= 5) {
          uint32_t res_id = spirv_code[k + 2];
          uint32_t op1 = spirv_code[k + 3];
          uint32_t op2 = spirv_code[k + 4];

          if (res_id < bound && is_in_demote_slice[res_id] && op1 < bound && op2 < bound) {
            uint32_t survivor = 0;
            // 模式 1: Or(!A, And(B, A)) 或 Or(!A, And(A, B)) -> 化简为 Cutout 条件 B
            if (def_opcode[op1] == SPV_OP_LOGICAL_NOT && def_opcode[op2] == SPV_OP_LOGICAL_AND) {
              uint32_t not_src = not_operand[op1];
              uint32_t a1 = and_operands[op2].first;
              uint32_t a2 = and_operands[op2].second;
              if (a1 == not_src) survivor = a2;
              else if (a2 == not_src) survivor = a1;
            }
            // 模式 2: Or(And(B, A), !A) 或 Or(And(A, B), !A) -> 化简为 Cutout 条件 B
            else if (def_opcode[op2] == SPV_OP_LOGICAL_NOT && def_opcode[op1] == SPV_OP_LOGICAL_AND) {
              uint32_t not_src = not_operand[op2];
              uint32_t a1 = and_operands[op1].first;
              uint32_t a2 = and_operands[op1].second;
              if (a1 == not_src) survivor = a2;
              else if (a2 == not_src) survivor = a1;
            }

            if (survivor > 0 && survivor < bound) {
              spirv_code[k] = (4 << 16) | SPV_OP_COPY_OBJECT;
              spirv_code[k + 3] = survivor;
              for (uint16_t p = 4; p < l && (k + p) < word_count; ++p) {
                spirv_code[k + p] = (1 << 16) | SPV_OP_NOP;
              }
              uncoupled_count++;
            }
          }
        }
        k += l;
      }
    }

    // Pass 3: 下游 Demote 指令级精准定界（消隐 NOP vs 镂空保护）
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
      } else if (is_discard_opcode(opcode)) {
        uint32_t cond = (current_label < bound) ? cond_for_label[current_label] : 0;
        bool should_nop = false;

        if (is_force_mode) {
          should_nop = true;
        } else if (cond > 0 && cond < bound) {
          if (is_inverted_mask[cond]) {
            // 角色近景防穿模反向消隐（Not(Alpha <= Cutoff)，裸露身体皮肤与头发）：100% 消除虚化与身体穿透！
            // 实体角色着色器执行 NOP，极小纯遮罩网格保留丢弃
            if (!has_any_sample && word_count < 1000) {
              should_nop = false;
            } else {
              should_nop = true;
            }
          } else if (depends_on_sample[cond]) {
            // 大世界植被环境 Alpha Cutout：严格保留贴图自然镂空，树叶草木绝非方块
            should_nop = false;
          } else if (is_dither_noise[cond] || depends_on_frag_coord[cond] || has_char_dither_feature) {
            // 纯遮罩几何体网格（无贴图采样且字长极小）：保持丢弃，防止反向画出浮空遮罩实体
            if (!has_any_sample && word_count < 1000) {
              should_nop = false;
            } else {
              should_nop = true;
            }
          } else {
            // 保守策略：环境物体的几何裁剪保持保留
            should_nop = false;
          }
        } else if (has_char_dither_feature) {
          // 角色网格无显式条件标签时的丢弃：实体消除虚化，纯遮罩几何体保留丢弃
          if (!has_any_sample && word_count < 1000) {
            should_nop = false;
          } else {
            should_nop = true;
          }
        } else {
          should_nop = false;
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

    if ((noped_count + uncoupled_count + preserved_cutout_count) > 0) {
      const char* category = has_char_dither_feature ? "角色网点/虚化(Toon Dither/Noise)" : (has_any_sample ? "植被/材质镂空(Alpha Cutout)" : "几何裁剪");
      game_logger::log_msg("[反虚化驱动层-DNA-VKD3D] 着色器: 0x%08x | 字长: %zu | 消除虚化: %u 处 | 保留正常裁剪: %u 处 | 类别: %s\n",
                           shader_hash, word_count, noped_count + uncoupled_count, preserved_cutout_count, category);
    }

    if (game_logger::g_dump_enabled && (noped_count > 0 || preserved_cutout_count > 0)) {
      game_logger::dump_shader_bundle(orig_copy.data(), orig_copy.size(),
                                      spirv_code, word_count,
                                      shader_hash, noped_count, preserved_cutout_count,
                                      has_any_frag_coord, has_any_sample);
    }
  }

} // namespace dna_vkd3d
