#pragma once

#include "../logger.h"

#include <cstddef>
#include <cstdint>
#include <vector>
#include <unordered_map>

namespace mihoyo_dxvk {

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
   * @brief 片段着色器反虚化处理
   *
   * 检查着色器中的 4x4 Bayer 矩阵特征常数，移除依赖该噪声的 discard 与 demote 指令，保留非依赖的纹理裁剪。
   */
  inline void process_spirv_anti_dither(uint32_t* spirv_code, size_t word_count, const char* game_tag = "MIHOYO") {
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
      game_logger::log_msg("[%s-DXVK] 着色器 0x%08x 在黑名单中，跳过处理\n", game_tag, shader_hash);
      return;
    }

    bool is_force_mode = (game_logger::g_force_hashes.count(shader_hash) > 0);

    bool has_any_discard = false;
    bool has_frag_coord = false;
    uint32_t c17_id = 0;

    union FloatUint {
      float f;
      uint32_t u;
    };

    // 阶段 0: 预先扫描 discard 指令与 17.0f 特征常数
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
          // 4x4 Bayer 抖动矩阵模数 17.0f
          if (fu.f >= 16.999f && fu.f <= 17.001f) {
            c17_id = spirv_code[i + 2];
          }
        }

        i += length;
      }
    }

    bool has_bayer_17 = (c17_id > 0);

    // 若无丢弃指令或无 17.0f 常数，非强制模式下直接跳过
    if (!has_any_discard || (!has_bayer_17 && !is_force_mode)) {
      if (game_logger::g_dump_enabled && has_any_discard) {
        game_logger::dump_shader_bundle(spirv_code, word_count,
                                        spirv_code, word_count,
                                        shader_hash, 0, 0,
                                        has_frag_coord, false);
      }
      return;
    }

    // 阶段 1: 收集分支条件与 SSA 操作数引用
    std::vector<uint32_t> cond_for_label(bound, 0);
    std::vector<std::vector<uint32_t>> ssa_operands(bound);

    {
      size_t i = 5;
      while (i < word_count) {
        uint32_t word = spirv_code[i];
        uint16_t opcode = word & 0xFFFF;
        uint16_t length = (word >> 16) & 0xFFFF;
        if (length == 0 || (i + length) > word_count) break;

        if (opcode == 250 /* OpBranchConditional */ && length >= 4) {
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

    std::vector<uint32_t> orig_copy;
    if (game_logger::g_dump_enabled) {
      orig_copy.assign(spirv_code, spirv_code + word_count);
    }

    // 阶段 2: 判定分支条件是否依赖 17.0f 常数
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

    // 阶段 3: 收集分支与丢弃结构并安全中和 (对 OpKill 执行分支重定向保持 Terminator，对 OpDemote 置 NOP)
    struct BranchInfo {
      size_t offset;
      uint32_t cond;
      uint32_t true_label;
      uint32_t false_label;
    };
    std::vector<BranchInfo> branch_insts;
    std::vector<uint8_t> label_has_kill(bound, 0);

    struct DemoteInfo {
      size_t offset;
      uint16_t length;
      uint32_t cond;
    };
    std::vector<DemoteInfo> demote_insts;

    uint32_t current_label = 0;
    size_t i = 5;

    while (i < word_count) {
      uint32_t word = spirv_code[i];
      uint16_t opcode = word & 0xFFFF;
      uint16_t length = (word >> 16) & 0xFFFF;
      if (length == 0 || (i + length) > word_count) break;

      if (opcode == 248 /* OpLabel */ && length >= 2) {
        current_label = spirv_code[i + 1];
      } else if (opcode == 250 /* OpBranchConditional */ && length >= 4) {
        BranchInfo bi;
        bi.offset = i;
        bi.cond = spirv_code[i + 1];
        bi.true_label = spirv_code[i + 2];
        bi.false_label = spirv_code[i + 3];
        branch_insts.push_back(bi);
      } else if (opcode == SPV_OP_KILL || opcode == SPV_OP_TERMINATE_INVOCATION) {
        if (current_label < bound) {
          label_has_kill[current_label] = 1;
        }
      } else if (opcode == SPV_OP_DEMOTE_TO_HELPER_INVOCATION) {
        uint32_t cond = (current_label < bound) ? cond_for_label[current_label] : 0;
        demote_insts.push_back({i, length, cond});
      }

      i += length;
    }

    uint32_t dither_nops = 0;
    uint32_t preserved = 0;

    // 1. 条件分支重定向: 跳过 Dither 对应的 OpKill 基本块，保持基本块 Terminator 完整
    for (const auto& br : branch_insts) {
      bool true_has_kill = (br.true_label < bound && label_has_kill[br.true_label]);
      bool false_has_kill = (br.false_label < bound && label_has_kill[br.false_label]);
      if (!true_has_kill && !false_has_kill) continue;

      uint32_t cond = br.cond;
      bool is_dither = (is_force_mode || (cond > 0 && is_bayer_dependent(cond)));

      if (true_has_kill) {
        if (is_dither) {
          spirv_code[br.offset + 2] = br.false_label;
          dither_nops++;
        } else {
          preserved++;
        }
      }

      if (false_has_kill) {
        if (is_dither) {
          spirv_code[br.offset + 3] = br.true_label;
          dither_nops++;
        } else {
          preserved++;
        }
      }
    }

    // 2. 非 Terminator 类型的 Demote 指令定向置 NOP
    for (const auto& dm : demote_insts) {
      bool is_dither = (is_force_mode || (dm.cond > 0 && is_bayer_dependent(dm.cond)));
      if (is_dither) {
        for (uint16_t k = 0; k < dm.length && (dm.offset + k) < word_count; ++k) {
          spirv_code[dm.offset + k] = (1 << 16) | SPV_OP_NOP;
        }
        dither_nops++;
      } else {
        preserved++;
      }
    }

    if (dither_nops > 0 || preserved > 0) {
      game_logger::log_msg("[反虚化驱动层-%s-DXVK] 着色器: 0x%08x | 字长: %zu | 消除虚化: %u 处 | 保留正常裁剪: %u 处 | 类别: %s\n",
                           game_tag, shader_hash, word_count, dither_nops, preserved,
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
   * @brief 顶点着色器几何坍缩消除流水线
   */
  inline void process_vertex_shader(uint32_t* spirv_code, size_t word_count, const char* game_tag = "MIHOYO") {
    if (!spirv_code || word_count < 5)
      return;

    if (!game_logger::is_active())
      return;

    if (spirv_code[0] != SPV_HEADER_MAGIC)
      return;

    uint32_t bound = spirv_code[3];
    if (bound == 0 || bound > 1048576)
      return;

    // 扫描 -99.0f 特征常数 ID
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

    // 记录导致 -99.0f 赋值的条件与其分支方向
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

    // 将选定为 -99.0f 的分支操作数替换为正常侧分支
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
      game_logger::log_msg("[反虚化驱动层-%s-DXVK] 顶点着色器: 0x%08x | 字长: %zu | 消除几何坍缩: %u 处\n",
                           game_tag, shader_hash, word_count, patched_selects);
    }
  }

} // namespace mihoyo_dxvk
