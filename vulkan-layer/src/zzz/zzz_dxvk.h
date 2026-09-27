#pragma once

#include "../logger.h"
#include <cstdint>
#include <cstddef>
#include <vector>
#include <unordered_set>
#include <unordered_map>
#include <cstring>
#include <string_view>

namespace zzz_dxvk {

  constexpr uint32_t SPV_HEADER_MAGIC                   = 0x07230203;
  constexpr uint32_t SPV_OP_NOP                         = 0x00010000;
  constexpr uint32_t SPV_OP_KILL                        = 252;
  constexpr uint32_t SPV_OP_TERMINATE_INVOCATION        = 4416;
  constexpr uint32_t SPV_OP_DEMOTE_TO_HELPER_INVOCATION = 5380;

  inline bool is_discard_opcode(uint16_t opcode) {
    return opcode == SPV_OP_KILL ||
           opcode == SPV_OP_TERMINATE_INVOCATION ||
           opcode == SPV_OP_DEMOTE_TO_HELPER_INVOCATION;
  }

  /**
   * @brief 绝区零 DXVK (DirectX 11) 片段着色器反虚化处理
   *
   * ZZZ 在 DX11 下通过 Immediate Constant Buffer (icb) 存储 4x4 Bayer 抖动矩阵。
   * 追踪受 icb 驱动的丢弃指令 (OpDemote / OpKill) 并安全中和，同时保护材质常规 Alpha 裁剪。
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

    uint32_t icb_id = 0;
    uint32_t c17_id = 0;
    bool has_any_discard = false;

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
      } else if (opcode == 5 /* OpName */ && length >= 3) {
        size_t max_bytes = static_cast<size_t>(length - 2) * sizeof(uint32_t);
        const char* str_ptr = reinterpret_cast<const char*>(&spirv_code[i + 2]);
        size_t str_len = 0;
        while (str_len < max_bytes && str_ptr[str_len] != '\0') str_len++;
        if (std::string_view(str_ptr, str_len) == "icb") {
          icb_id = spirv_code[i + 1];
        }
      } else if (opcode == 43 /* OpConstant */ && length >= 4) {
        union { float f; uint32_t u; } fu;
        fu.u = spirv_code[i + 3];
        if (fu.f >= 16.999f && fu.f <= 17.001f) {
          c17_id = spirv_code[i + 2];
        }
      }

      i += length;
    }

    if (!has_any_discard) {
      return;
    }

    // 若既无 icb 矩阵也无 17.0f 常数，则非网点虚化着色器，保留全部裁剪
    if (icb_id == 0 && c17_id == 0) {
      if (game_logger::g_dump_enabled) {
        game_logger::dump_shader_bundle(spirv_code, word_count, spirv_code, word_count,
                                        shader_hash, 0, 0, false, false);
      }
      return;
    }

    // 1. 追踪由 icb 或 17.0f 派生的 SSA 变量
    std::unordered_set<uint32_t> bayer_derived_ids;
    if (icb_id != 0) bayer_derived_ids.insert(icb_id);
    if (c17_id != 0) bayer_derived_ids.insert(c17_id);

    bool changed = true;
    while (changed) {
      changed = false;
      for (const auto& inst : instructions) {
        const uint32_t* p = &spirv_code[inst.offset];
        uint16_t op = inst.op;
        uint16_t len = inst.len;

        if ((op == 65 /* OpAccessChain */ || op == 66 /* OpInBoundsAccessChain */) && len >= 4) {
          uint32_t res = p[2];
          uint32_t base = p[3];
          if (bayer_derived_ids.count(base) && !bayer_derived_ids.count(res)) {
            bayer_derived_ids.insert(res);
            changed = true;
          }
        } else if ((op == 61 /* OpLoad */ || op == 81 /* OpCompositeExtract */) && len >= 4) {
          uint32_t res = p[2];
          uint32_t src = p[3];
          if (bayer_derived_ids.count(src) && !bayer_derived_ids.count(res)) {
            bayer_derived_ids.insert(res);
            changed = true;
          }
        } else if ((op == 79 /* OpVectorShuffle */) && len >= 5) {
          uint32_t res = p[2];
          if ((bayer_derived_ids.count(p[3]) || bayer_derived_ids.count(p[4])) && !bayer_derived_ids.count(res)) {
            bayer_derived_ids.insert(res);
            changed = true;
          }
        } else if (op == 12 /* OpExtInst */ && len >= 5) {
          uint32_t res = p[2];
          for (uint16_t a = 4; a < len; ++a) {
            if (bayer_derived_ids.count(p[a]) && !bayer_derived_ids.count(res)) {
              bayer_derived_ids.insert(res);
              changed = true;
              break;
            }
          }
        } else if ((op == 126 || op == 127 || op == 129 || op == 130 || op == 131 ||
                    op == 133 || op == 148 || op == 180 || op == 182 || op == 184 || op == 186) && len >= 4) {
          uint32_t res = p[2];
          for (uint16_t a = 3; a < len; ++a) {
            if (bayer_derived_ids.count(p[a]) && !bayer_derived_ids.count(res)) {
              bayer_derived_ids.insert(res);
              changed = true;
              break;
            }
          }
        }
      }
    }

    // 2. 收集分支与丢弃结构并安全中和 (对 OpKill 执行分支重定向保持 Terminator，对 OpDemote 置 NOP)
    struct BranchInfo {
      size_t offset;
      uint32_t cond;
      uint32_t true_label;
      uint32_t false_label;
    };
    std::vector<BranchInfo> branch_insts;
    std::unordered_map<uint32_t, uint32_t> label_to_cond;
    std::unordered_set<uint32_t> kill_labels;

    struct DemoteInfo {
      size_t offset;
      uint16_t length;
      uint32_t cond;
    };
    std::vector<DemoteInfo> demote_insts;

    for (const auto& inst : instructions) {
      const uint32_t* p = &spirv_code[inst.offset];
      if (inst.op == 250 /* OpBranchConditional */ && inst.len >= 4) {
        BranchInfo bi;
        bi.offset = inst.offset;
        bi.cond = p[1];
        bi.true_label = p[2];
        bi.false_label = p[3];
        branch_insts.push_back(bi);
        label_to_cond[p[2]] = p[1];
        label_to_cond[p[3]] = p[1];
      }
    }

    uint32_t cur_lbl = 0;
    for (const auto& inst : instructions) {
      const uint32_t* p = &spirv_code[inst.offset];
      if (inst.op == 248 /* OpLabel */ && inst.len >= 2) {
        cur_lbl = p[1];
      } else if (inst.op == SPV_OP_KILL || inst.op == SPV_OP_TERMINATE_INVOCATION) {
        kill_labels.insert(cur_lbl);
      } else if (inst.op == SPV_OP_DEMOTE_TO_HELPER_INVOCATION) {
        auto it = label_to_cond.find(cur_lbl);
        uint32_t cond = (it != label_to_cond.end()) ? it->second : 0;
        demote_insts.push_back({inst.offset, inst.len, cond});
      }
    }

    // 3. 执行定向丢弃中和
    uint32_t dither_nops = 0;
    uint32_t preserved_cutouts = 0;
    std::vector<uint32_t> orig_spv;
    if (game_logger::g_dump_enabled) {
      orig_spv.assign(spirv_code, spirv_code + word_count);
    }

    // 1. 条件分支重定向: 跳过 Dither 对应的 OpKill 基本块
    for (const auto& br : branch_insts) {
      bool true_has_kill = kill_labels.count(br.true_label);
      bool false_has_kill = kill_labels.count(br.false_label);
      if (!true_has_kill && !false_has_kill) continue;

      bool is_bayer = (bayer_derived_ids.count(br.cond) > 0);

      if (true_has_kill) {
        if (is_bayer) {
          spirv_code[br.offset + 2] = br.false_label;
          dither_nops++;
        } else {
          preserved_cutouts++;
        }
      }

      if (false_has_kill) {
        if (is_bayer) {
          spirv_code[br.offset + 3] = br.true_label;
          dither_nops++;
        } else {
          preserved_cutouts++;
        }
      }
    }

    // 2. 非 Terminator 类型的 Demote 指令定向置 NOP
    for (const auto& dm : demote_insts) {
      bool is_bayer = (bayer_derived_ids.count(dm.cond) > 0);
      if (is_bayer) {
        for (uint16_t n = 0; n < dm.length; ++n) {
          spirv_code[dm.offset + n] = SPV_OP_NOP;
        }
        dither_nops++;
      } else {
        preserved_cutouts++;
      }
    }

    // 4. 标准化日志与 Dump
    if (dither_nops > 0 || preserved_cutouts > 0) {
      game_logger::log_msg("[反虚化驱动层-ZZZ-DXVK] 着色器: 0x%08x | 字长: %zu | 消除虚化: %u 处 | 保留正常裁剪: %u 处 | 类别: %s\n",
                           shader_hash, word_count, dither_nops, preserved_cutouts,
                           (dither_nops > 0 ? "Bayer相机虚化" : "常规材质镂空"));

      if (game_logger::g_dump_enabled) {
        game_logger::dump_shader_bundle(orig_spv.empty() ? spirv_code : orig_spv.data(),
                                        orig_spv.empty() ? word_count : orig_spv.size(),
                                        spirv_code, word_count,
                                        shader_hash, dither_nops, preserved_cutouts,
                                        false, false);
      }
    }
  }

  /**
   * @brief 绝区零 DXVK (DirectX 11) 顶点着色器几何坍缩消除分发
   */
  inline void process_vertex_shader(uint32_t* spirv_code, size_t word_count) {
    // 预留顶点坍缩处理接口
    (void)spirv_code;
    (void)word_count;
  }

} // namespace zzz_dxvk
