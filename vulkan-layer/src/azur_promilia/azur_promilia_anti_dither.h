#pragma once

#include "../logger.h"

#include <cstddef>
#include <cstdint>
#include <vector>
#include <unordered_set>
#include <unordered_map>
#include <string_view>

namespace azur_promilia_layer {

  constexpr uint32_t SPV_HEADER_MAGIC                   = 0x07230203;
  constexpr uint32_t SPV_OP_NOP                         = 0x00010000;
  constexpr uint32_t SPV_OP_COPY_OBJECT                 = 83;
  constexpr uint32_t SPV_OP_KILL                        = 252;
  constexpr uint32_t SPV_OP_TERMINATE_INVOCATION        = 4416;
  constexpr uint32_t SPV_OP_DEMOTE_TO_HELPER_INVOCATION = 5380;

  inline bool is_discard_opcode(uint16_t opcode) {
    return opcode == SPV_OP_KILL ||
           opcode == SPV_OP_TERMINATE_INVOCATION ||
           opcode == SPV_OP_DEMOTE_TO_HELPER_INVOCATION;
  }

  /**
   * @brief 蓝色星原 DXVK (DirectX 11) 片段着色器反虚化与颜色解耦处理
   *
   * 蓝色星原角色相机虚化采用散斑伪随机噪声算法:
   *   Noise = Fract(Fract(dot(SV_POSITION.xy, v)) * 52.9829178)
   * 引擎在半虚化状态下会计算平滑淡出权重 FadeWeight 并乘入漫反射光照颜色中。
   * 本函数基于 SSA 拓扑追溯将 FadeThreshold 变量安全解耦置为 1.0f，同时中和散斑丢弃分支，
   * 彻底根治半虚化状态下的网点残留与角色全身发黑变色，并 100% 保护非散斑的材质常规镂空。
   */
  inline void process_spirv_anti_dither(uint32_t* spirv_code, size_t word_count) {
    if (!spirv_code || word_count < 5)
      return;

    if (!game_logger::is_active())
      return;

    if (spirv_code[0] != SPV_HEADER_MAGIC)
      return;

    uint32_t shader_hash = game_logger::compute_spirv_hash(spirv_code, word_count);

    if (game_logger::g_exclude_hashes.count(shader_hash) > 0) {
      return;
    }

    struct InstView {
      uint16_t op;
      uint16_t len;
      size_t offset;
    };
    std::vector<InstView> instructions;
    instructions.reserve(word_count / 4);

    bool has_any_discard = false;
    bool has_frag_coord = false;
    uint32_t c_noise_id = 0; // 52.9829178f (0x4253ee82)
    uint32_t c_1_id = 0;     // 1.0f (0x3f800000)

    size_t i = 5;
    while (i < word_count) {
      uint32_t word = spirv_code[i];
      uint16_t opcode = word & 0xFFFF;
      uint16_t length = (word >> 16) & 0xFFFF;

      if (length == 0 || (i + length) > word_count)
        break;

      instructions.push_back({opcode, length, i});

      if (is_discard_opcode(opcode)) {
        has_any_discard = true;
      } else if (opcode == 71 /* OpDecorate */ && length >= 4) {
        if (spirv_code[i + 2] == 11 /* BuiltIn */ && spirv_code[i + 3] == 15 /* FragCoord */) {
          has_frag_coord = true;
        }
      } else if (opcode == 43 /* OpConstant */ && length >= 4) {
        uint32_t val = spirv_code[i + 3];
        if (val == 0x4253ee82u) {
          c_noise_id = spirv_code[i + 2];
        } else if (val == 0x3f800000u) {
          c_1_id = spirv_code[i + 2];
        }
      }

      i += length;
    }

    // 若无散斑噪声特征常数，则非角色网点着色器，完整保护全部裁剪（农田作物、树叶、阴影深度）
    if (c_noise_id == 0) {
      if (game_logger::g_dump_enabled && has_any_discard) {
        game_logger::dump_shader_bundle(spirv_code, word_count, spirv_code, word_count,
                                        shader_hash, 0, 0, has_frag_coord, false);
      }
      return;
    }

    // 1. SSA 追踪散斑噪声节点 Noise = Fract(FMul(x, 52.9829178))
    std::unordered_set<uint32_t> fmul_noise_ids;
    for (const auto& inst : instructions) {
      const uint32_t* p = &spirv_code[inst.offset];
      if (inst.op == 133 /* OpFMul */ && inst.len >= 5) {
        if (p[3] == c_noise_id || p[4] == c_noise_id) {
          fmul_noise_ids.insert(p[2]);
        }
      }
    }

    std::unordered_set<uint32_t> fract_noise_ids;
    for (const auto& inst : instructions) {
      const uint32_t* p = &spirv_code[inst.offset];
      if (inst.op == 12 /* OpExtInst */ && inst.len >= 6) {
        // GLSL.std.450 Fract = 10
        if (p[4] == 10 && fmul_noise_ids.count(p[5])) {
          fract_noise_ids.insert(p[2]);
        }
      }
    }

    // 2. 追踪 FadeThreshold 变量与减法比较条件
    std::unordered_set<uint32_t> fade_ids;
    std::unordered_set<uint32_t> dither_cond_ids;

    for (const auto& inst : instructions) {
      const uint32_t* p = &spirv_code[inst.offset];
      if (inst.op == 131 /* OpFSub */ && inst.len >= 5) {
        if (fract_noise_ids.count(p[4])) {
          fade_ids.insert(p[3]);
          dither_cond_ids.insert(p[2]); // Sub 结果
        }
      }
    }

    // 级联传播散斑比较条件
    bool changed = true;
    while (changed) {
      changed = false;
      for (const auto& inst : instructions) {
        const uint32_t* p = &spirv_code[inst.offset];
        uint16_t op = inst.op;
        uint16_t len = inst.len;

        if ((op == 180 || op == 182 || op == 184 || op == 186) && len >= 5) {
          if ((dither_cond_ids.count(p[3]) || dither_cond_ids.count(p[4])) && !dither_cond_ids.count(p[2])) {
            dither_cond_ids.insert(p[2]);
            changed = true;
          }
        } else if ((op == 164 || op == 165) && len >= 5) { // OpLogicalAnd / OpLogicalOr
          if ((dither_cond_ids.count(p[3]) || dither_cond_ids.count(p[4])) && !dither_cond_ids.count(p[2])) {
            dither_cond_ids.insert(p[2]);
            changed = true;
          }
        }
      }
    }

    // 记录原始 SPIR-V 用于 Dump 诊断
    std::vector<uint32_t> orig_spv;
    if (game_logger::g_dump_enabled) {
      orig_spv.assign(spirv_code, spirv_code + word_count);
    }

    uint32_t fade_decoupled_count = 0;
    uint32_t dither_nop_count = 0;
    uint32_t preserved_cutout_count = 0;

    // 3. 核心解耦: 将 FadeThreshold 变量安全重写为 1.0f (彻底根治半虚化发黑变色)
    if (c_1_id != 0 && !fade_ids.empty()) {
      for (const auto& inst : instructions) {
        uint32_t* p = &spirv_code[inst.offset];
        if (inst.op == 61 /* OpLoad */ && inst.len >= 4) {
          uint32_t res_id = p[2];
          if (fade_ids.count(res_id)) {
            // 将 OpLoad 转换为 OpCopyObject %type %res_id %c_1_id
            p[0] = (4 << 16) | SPV_OP_COPY_OBJECT;
            p[3] = c_1_id;
            for (uint16_t k = 4; k < inst.len; ++k) {
              p[k] = SPV_OP_NOP;
            }
            fade_decoupled_count++;
          }
        }
      }
    }

    // 4. 双重保障: 消除由散斑噪声控制的丢弃指令，保留常规材质镂空
    std::unordered_map<uint32_t, uint32_t> label_to_cond;
    for (const auto& inst : instructions) {
      const uint32_t* p = &spirv_code[inst.offset];
      if (inst.op == 250 /* OpBranchConditional */ && inst.len >= 4) {
        label_to_cond[p[2]] = p[1];
      }
    }

    uint32_t cur_label = 0;
    for (const auto& inst : instructions) {
      uint32_t* p = &spirv_code[inst.offset];
      if (inst.op == 248 /* OpLabel */ && inst.len >= 2) {
        cur_label = p[1];
      } else if (is_discard_opcode(inst.op)) {
        auto it = label_to_cond.find(cur_label);
        bool is_dither_discard = false;
        if (it != label_to_cond.end() && dither_cond_ids.count(it->second)) {
          is_dither_discard = true;
        }

        if (is_dither_discard) {
          for (uint16_t k = 0; k < inst.len; ++k) {
            p[k] = SPV_OP_NOP;
          }
          dither_nop_count++;
        } else {
          preserved_cutout_count++;
        }
      }
    }

    // 5. 标准化日志与 Dump 框架集成
    if (fade_decoupled_count > 0 || dither_nop_count > 0 || preserved_cutout_count > 0) {
      game_logger::log_msg("[反虚化驱动层-AZUR_PROMILIA-DXVK] 着色器: 0x%08x | 字长: %zu | 消除虚化: %u 处 | 保留正常裁剪: %u 处 | 类别: 散斑Fade解耦与网点中和\n",
                           shader_hash, word_count, dither_nop_count, preserved_cutout_count);
    }

    if (game_logger::g_dump_enabled) {
      game_logger::dump_shader_bundle(orig_spv.empty() ? spirv_code : orig_spv.data(),
                                      orig_spv.empty() ? word_count : orig_spv.size(),
                                      spirv_code, word_count,
                                      shader_hash, dither_nop_count, preserved_cutout_count,
                                      has_frag_coord, false);
    }
  }

} // namespace azur_promilia_layer
