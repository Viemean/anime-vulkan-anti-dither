#pragma once

#include "../logger.h"
#include <cstdint>
#include <cstddef>
#include <vector>
#include <unordered_set>

namespace zzz_vkd3d {

  /**
   * @brief ZZZ VKD3D (DirectX 12) 片元着色器反虚化处理
   */
  inline void process_fragment_shader(uint32_t* spirv_code, size_t word_count) {
    if (!spirv_code || word_count < 5)
      return;

    if (!game_logger::is_active())
      return;

    uint32_t shader_hash = game_logger::compute_spirv_hash(spirv_code, word_count);

    if (game_logger::g_exclude_hashes.count(shader_hash) > 0) {
      return;
    }

    // 1. 查找 Bayer 矩阵常数与变量
    // Bayer 矩阵特征常数: 0x3d70f0f1 (1.0f / 17.0f)
    uint32_t c_bayer_id = 0;
    uint32_t composite_id = 0;
    uint32_t var_id = 0;

    size_t i = 5;
    struct InstView {
      uint16_t op;
      uint16_t len;
      size_t offset;
    };
    std::vector<InstView> instructions;
    instructions.reserve(word_count / 4);

    while (i < word_count) {
      uint32_t word = spirv_code[i];
      uint16_t opcode = word & 0xFFFF;
      uint16_t length = (word >> 16) & 0xFFFF;
      if (length == 0 || (i + length) > word_count)
        break;

      instructions.push_back({opcode, length, i});

      if (opcode == 43 /* OpConstant */ && length >= 4) {
        if (spirv_code[i + 3] == 0x3d70f0f1u) {
          c_bayer_id = spirv_code[i + 2];
        }
      } else if (opcode == 44 /* OpConstantComposite */ && length >= 4) {
        if (c_bayer_id != 0 && spirv_code[i + 3] == c_bayer_id) {
          composite_id = spirv_code[i + 2];
        }
      } else if (opcode == 59 /* OpVariable */ && length >= 5) {
        if (composite_id != 0 && spirv_code[i + 4] == composite_id) {
          var_id = spirv_code[i + 2];
        }
      }

      i += length;
    }

    // 若未检测到 Bayer 4x4 抖动矩阵，则不属于网点虚化着色器，保护所有裁剪与镂空
    if (composite_id == 0 && var_id == 0) {
      if (game_logger::g_dump_enabled) {
        game_logger::dump_shader_bundle(spirv_code, word_count, spirv_code, word_count,
                                        shader_hash, 0, 0, false, false);
      }
      return;
    }

    // 2. 追踪 Bayer 数组的数据流传播
    std::unordered_set<uint32_t> bayer_derived_ids;
    if (var_id != 0) bayer_derived_ids.insert(var_id);
    if (composite_id != 0) bayer_derived_ids.insert(composite_id);

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
        } else if (op == 61 /* OpLoad */ && len >= 4) {
          uint32_t res = p[2];
          uint32_t ptr = p[3];
          if (bayer_derived_ids.count(ptr) && !bayer_derived_ids.count(res)) {
            bayer_derived_ids.insert(res);
            changed = true;
          }
        } else if ((op == 127 || op == 129 || op == 131 || op == 133 || op == 148 ||
                    op == 180 || op == 182 || op == 184 || op == 186) && len >= 4) {
          uint32_t res = p[2];
          for (uint16_t arg_idx = 3; arg_idx < len; ++arg_idx) {
            if (bayer_derived_ids.count(p[arg_idx]) && !bayer_derived_ids.count(res)) {
              bayer_derived_ids.insert(res);
              changed = true;
              break;
            }
          }
        }
      }
    }

    // 3. 识别 demote_cond 函数与 helper 调用
    std::unordered_set<uint32_t> demote_helper_funcs;
    uint32_t current_func = 0;
    for (const auto& inst : instructions) {
      const uint32_t* p = &spirv_code[inst.offset];
      if (inst.op == 54 /* OpFunction */ && inst.len >= 3) {
        current_func = p[2];
      } else if (inst.op == 56 /* OpFunctionEnd */) {
        current_func = 0;
      } else if (inst.op == 5380 /* OpDemoteToHelperInvocationEXT */) {
        if (current_func != 0) {
          demote_helper_funcs.insert(current_func);
        }
      }
    }

    // 4. 定向消除受 Bayer 条件驱动的 demote 调用，保留正常材质镂空
    uint32_t dither_nops = 0;
    uint32_t preserved_cutouts = 0;
    std::vector<uint32_t> orig_spv;
    if (game_logger::g_dump_enabled) {
      orig_spv.assign(spirv_code, spirv_code + word_count);
    }

    for (const auto& inst : instructions) {
      uint32_t* p = &spirv_code[inst.offset];
      if (inst.op == 57 /* OpFunctionCall */ && inst.len >= 4) {
        uint32_t callee = p[3];
        if (demote_helper_funcs.count(callee)) {
          bool uses_bayer = false;
          for (uint16_t a = 4; a < inst.len; ++a) {
            if (bayer_derived_ids.count(p[a])) {
              uses_bayer = true;
              break;
            }
          }

          if (uses_bayer) {
            // NOP 化该 helper 调用
            for (uint16_t n = 0; n < inst.len; ++n) {
              p[n] = 0x00010000u; // OpNop
            }
            dither_nops++;
          } else {
            preserved_cutouts++;
          }
        }
      }
    }

    // 5. 标准化日志与 Dump
    if (dither_nops > 0 || preserved_cutouts > 0) {
      game_logger::log_msg("[反虚化驱动层-ZZZ-VKD3D] 着色器: 0x%08x | 字长: %zu | 消除虚化: %u 处 | 保留正常裁剪: %u 处 | 类别: %s\n",
                           shader_hash, word_count, dither_nops, preserved_cutouts,
                           (dither_nops > 0 ? "Bayer相机虚化" : "常规材质镂空"));
    }

    if (game_logger::g_dump_enabled) {
      game_logger::dump_shader_bundle(orig_spv.empty() ? spirv_code : orig_spv.data(),
                                      orig_spv.empty() ? word_count : orig_spv.size(),
                                      spirv_code, word_count,
                                      shader_hash, dither_nops, preserved_cutouts,
                                      false, false);
    }
  }

} // namespace zzz_vkd3d
