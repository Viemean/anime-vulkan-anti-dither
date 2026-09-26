#pragma once

#include "../logger.h"

#include <cstddef>
#include <cstdint>
#include <vector>
#include <unordered_map>

namespace genshin_dxvk {

  constexpr uint32_t SPV_HEADER_MAGIC                   = 0x07230203;
  constexpr uint32_t SPV_OP_NOP                         = 0;
  constexpr uint32_t SPV_OP_KILL                        = 252;
  constexpr uint32_t SPV_OP_TERMINATE_INVOCATION        = 4416;
  constexpr uint32_t SPV_OP_DEMOTE_TO_HELPER_INVOCATION = 5380;
  constexpr uint32_t SPV_DECORATION_BUILTIN             = 11;
  constexpr uint32_t SPV_BUILTIN_FRAG_COORD             = 15;

  inline bool is_discard_opcode(uint16_t opcode) {
    return opcode == SPV_OP_KILL ||
           opcode == SPV_OP_TERMINATE_INVOCATION ||
           opcode == SPV_OP_DEMOTE_TO_HELPER_INVOCATION;
  }

  /**
   * @brief 原神 DXVK (DirectX 11) 片段着色器反虚化处理流水线
   *
   * 基于 SSA 数据流溯源与 4x4 Bayer 矩阵特征，精准剥离角色网点虚化，并保留正常 Alpha Cutout。
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
      game_logger::log_msg("[GENSHIN-DXVK] 着色器 0x%08x 在黑名单中，跳过处理\n", shader_hash);
      return;
    }

    bool is_force_mode = (game_logger::g_force_hashes.count(shader_hash) > 0);

    union FloatUint {
      float f;
      uint32_t u;
    };

    uint32_t c17_id = 0;
    std::vector<uint32_t> cond_for_label(bound, 0);
    std::vector<std::vector<uint32_t>> ssa_operands(bound);

    bool has_any_discard = false;
    bool has_frag_coord = false;

    // Pass 1: 扫描丢弃指令、FragCoord、4x4 Bayer 特征常数 17.0f 及算术 SSA 关系
    {
      size_t i = 5;
      while (i < word_count) {
        uint32_t word = spirv_code[i];
        uint16_t opcode = word & 0xFFFF;
        uint16_t length = (word >> 16) & 0xFFFF;
        if (length == 0 || (i + length) > word_count) break;

        if (is_discard_opcode(opcode)) {
          has_any_discard = true;
        } else if (opcode == 71 /* OpDecorate */ && length >= 4) {
          if (spirv_code[i + 2] == SPV_DECORATION_BUILTIN &&
              spirv_code[i + 3] == SPV_BUILTIN_FRAG_COORD) {
            has_frag_coord = true;
          }
        } else if (opcode == 43 /* OpConstant */ && length >= 4) {
          FloatUint fu;
          fu.u = spirv_code[i + 3];
          // Unity 4x4 Bayer 抖动矩阵归一化模数 (17.0f)
          if (fu.f >= 16.999f && fu.f <= 17.001f) {
            c17_id = spirv_code[i + 2];
          }
        } else if (opcode == 250 /* OpBranchConditional */ && length >= 4) {
          uint32_t cond = spirv_code[i + 1];
          uint32_t true_l = spirv_code[i + 2];
          uint32_t false_l = spirv_code[i + 3];
          if (true_l < bound) cond_for_label[true_l] = cond;
          if (false_l < bound) cond_for_label[false_l] = cond;
        } else if (opcode == 12 /* OpExtInst */ && length >= 5) {
          uint32_t res_id = spirv_code[i + 2];
          if (res_id < bound) {
            ssa_operands[res_id].assign(spirv_code + i + 5, spirv_code + i + length);
          }
        } else if (
            opcode == 80 /* OpVectorShuffle */ ||
            opcode == 81 /* OpCompositeExtract */ ||
            (opcode >= 127 && opcode <= 136) /* FNeg, FAdd, FSub, FMul, FDiv */ ||
            (opcode >= 169 && opcode <= 171) /* OpSelect, OpIEqual, OpLogicalAnd */ ||
            (opcode >= 180 && opcode <= 188) /* Comparisons */) {
          if (length >= 3) {
            uint32_t res_id = spirv_code[i + 2];
            if (res_id < bound) {
              ssa_operands[res_id].assign(spirv_code + i + 3, spirv_code + i + length);
            }
          }
        }

        i += length;
      }
    }

    bool has_bayer_17 = (c17_id > 0);

    // 若无丢弃指令，且在非强制模式下无 4x4 Bayer 抖动特征
    if (!has_any_discard || (!has_bayer_17 && !is_force_mode)) {
      // 开启 dump 模式下记录诊断信息，辅助初始分析
      if (game_logger::g_dump_enabled && has_any_discard) {
        game_logger::dump_shader_bundle(spirv_code, word_count,
                                        spirv_code, word_count,
                                        shader_hash, 0, 0,
                                        has_frag_coord, false);
      }
      return;
    }

    std::vector<uint32_t> orig_copy;
    if (game_logger::g_dump_enabled) {
      orig_copy.assign(spirv_code, spirv_code + word_count);
    }

    // Pass 2: 反向追溯判定目标条件是否依赖 Bayer 17.0f 噪声
    auto is_bayer_dependent = [&](uint32_t start_cond) -> bool {
      if (start_cond == 0 || start_cond >= bound || c17_id == 0)
        return false;

      std::vector<uint8_t> visited(bound, 0);
      std::vector<uint32_t> stack;
      stack.reserve(64);
      stack.push_back(start_cond);

      while (!stack.empty()) {
        uint32_t curr = stack.back();
        stack.pop_back();

        if (curr == c17_id)
          return true;

        if (curr >= bound || visited[curr])
          continue;
        visited[curr] = 1;

        for (uint32_t op : ssa_operands[curr]) {
          if (op > 0 && op < bound && !visited[op]) {
            if (op == c17_id)
              return true;
            stack.push_back(op);
          }
        }
      }
      return false;
    };

    // Pass 3: 精准中和依赖 Bayer 噪声的 discard / demote
    uint32_t current_label = 0;
    uint32_t dither_nops = 0;
    uint32_t preserved = 0;
    size_t i = 5;

    while (i < word_count) {
      uint32_t word = spirv_code[i];
      uint16_t opcode = word & 0xFFFF;
      uint16_t length = (word >> 16) & 0xFFFF;
      if (length == 0 || (i + length) > word_count) break;

      if (opcode == 248 /* OpLabel */ && length >= 2) {
        current_label = spirv_code[i + 1];
      } else if (is_discard_opcode(opcode)) {
        uint32_t cond = (current_label < bound) ? cond_for_label[current_label] : 0;
        bool should_nop = false;

        if (is_force_mode) {
          should_nop = true;
        } else if (cond > 0 && is_bayer_dependent(cond)) {
          should_nop = true;
        }

        if (should_nop) {
          for (uint16_t k = 0; k < length && (i + k) < word_count; ++k) {
            spirv_code[i + k] = (1 << 16) | SPV_OP_NOP;
          }
          dither_nops++;
        } else {
          preserved++;
        }
      }

      i += length;
    }

    if (dither_nops > 0 || preserved > 0) {
      game_logger::log_msg("[反虚化驱动层-GENSHIN-DXVK] 着色器: 0x%08x | 字长: %zu | 消除虚化: %u 处 | 保留正常裁剪: %u 处 | 类别: %s\n",
                           shader_hash, word_count, dither_nops, preserved,
                           has_bayer_17 ? "4x4 Bayer 角色网点" : (has_frag_coord ? "屏幕空间计算" : "材质裁剪"));

      if (game_logger::g_dump_enabled && !orig_copy.empty()) {
        game_logger::dump_shader_bundle(orig_copy.data(), orig_copy.size(),
                                        spirv_code, word_count,
                                        shader_hash, dither_nops, preserved,
                                        has_frag_coord, false);
      }
    }
  }

  /**
   * @brief 原神 DXVK 顶点着色器几何坍缩消除与诊断流水线
   */
  inline void process_vertex_shader(uint32_t* spirv_code, size_t word_count) {
    if (!spirv_code || word_count < 5)
      return;

    if (!game_logger::is_active())
      return;

    if (spirv_code[0] != SPV_HEADER_MAGIC)
      return;

    uint32_t bound = spirv_code[3];
    if (bound == 0 || bound > 1048576)
      return;

    // 扫描 -99.0f 或其它特征坍缩常数
    std::vector<uint32_t> n99_ids;
    size_t i = 5;
    while (i < word_count) {
      uint32_t word = spirv_code[i];
      uint16_t opcode = word & 0xFFFF;
      uint16_t length = (word >> 16) & 0xFFFF;
      if (length == 0 || (i + length) > word_count)
        break;

      if (opcode == 43 /* OpConstant */ && length >= 4) {
        if (spirv_code[i + 3] == 0xC2C60000 /* -99.0f */) {
          n99_ids.push_back(spirv_code[i + 2]);
        }
      }
      i += length;
    }

    if (n99_ids.empty())
      return;

    std::unordered_map<uint32_t, int> cond_collapse_side;
    i = 5;
    while (i < word_count) {
      uint32_t word = spirv_code[i];
      uint16_t opcode = word & 0xFFFF;
      uint16_t length = (word >> 16) & 0xFFFF;
      if (length == 0 || (i + length) > word_count)
        break;

      if (opcode == 169 /* OpSelect */ && length == 6) {
        uint32_t cond = spirv_code[i + 3];
        uint32_t true_val = spirv_code[i + 4];
        uint32_t false_val = spirv_code[i + 5];

        for (uint32_t n99_id : n99_ids) {
          if (true_val == n99_id) {
            cond_collapse_side[cond] = 1;
            break;
          } else if (false_val == n99_id) {
            cond_collapse_side[cond] = 2;
            break;
          }
        }
      }
      i += length;
    }

    if (cond_collapse_side.empty())
      return;

    uint32_t shader_hash = game_logger::compute_spirv_hash(spirv_code, word_count);
    uint32_t patched_selects = 0;

    i = 5;
    while (i < word_count) {
      uint32_t word = spirv_code[i];
      uint16_t opcode = word & 0xFFFF;
      uint16_t length = (word >> 16) & 0xFFFF;
      if (length == 0 || (i + length) > word_count)
        break;

      if (opcode == 169 /* OpSelect */ && length == 6) {
        uint32_t cond = spirv_code[i + 3];
        auto it = cond_collapse_side.find(cond);
        if (it != cond_collapse_side.end()) {
          if (it->second == 1) {
            spirv_code[i + 4] = spirv_code[i + 5];
            patched_selects++;
          } else if (it->second == 2) {
            spirv_code[i + 5] = spirv_code[i + 4];
            patched_selects++;
          }
        }
      }
      i += length;
    }

    if (patched_selects > 0) {
      game_logger::log_msg("[反虚化驱动层-GENSHIN-DXVK] 顶点着色器: 0x%08x | 字长: %zu | 消除几何坍缩: %u 处\n",
                           shader_hash, word_count, patched_selects);
    }
  }

} // namespace genshin_dxvk
