#pragma once

#include "../logger.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>
#include <string_view>

namespace star_dxvk {

  constexpr uint32_t SPV_HEADER_MAGIC                   = 0x07230203;
  constexpr uint32_t SPV_OP_NOP                         = 0x00010000;
  constexpr uint32_t SPV_OP_EXT_INST                    = 12;
  constexpr uint32_t SPV_OP_ENTRY_POINT                 = 15;
  constexpr uint32_t SPV_OP_CONSTANT                    = 43;
  constexpr uint32_t SPV_OP_CONSTANT_COMPOSITE          = 44;
  constexpr uint32_t SPV_OP_VARIABLE                    = 59;
  constexpr uint32_t SPV_OP_LOAD                        = 61;
  constexpr uint32_t SPV_OP_ACCESS_CHAIN                = 65;
  constexpr uint32_t SPV_OP_DECORATE                    = 71;
  constexpr uint32_t SPV_OP_COMPOSITE_EXTRACT           = 81;
  constexpr uint32_t SPV_OP_LABEL                       = 248;
  constexpr uint32_t SPV_OP_BRANCH                      = 249;
  constexpr uint32_t SPV_OP_BRANCH_CONDITIONAL          = 250;
  constexpr uint32_t SPV_OP_KILL                        = 252;
  constexpr uint32_t SPV_OP_RETURN                      = 253;
  constexpr uint32_t SPV_OP_RETURN_VALUE                = 254;
  constexpr uint32_t SPV_OP_TERMINATE_INVOCATION        = 4416;
  constexpr uint32_t SPV_OP_DEMOTE_TO_HELPER_INVOCATION = 5380;

  constexpr uint32_t SPV_DECORATION_BUILTIN             = 11;
  constexpr uint32_t SPV_BUILTIN_FRAG_COORD             = 15;
  constexpr uint32_t SPV_BUILTIN_SAMPLE_POSITION        = 19;

  union FloatUint {
    float f;
    uint32_t u;
  };

  inline bool is_discard_opcode(uint16_t opcode) {
    return opcode == SPV_OP_KILL ||
           opcode == SPV_OP_TERMINATE_INVOCATION ||
           opcode == SPV_OP_DEMOTE_TO_HELPER_INVOCATION;
  }

  inline bool is_image_sample_opcode(uint16_t opcode) {
    return (opcode >= 87 && opcode <= 107) || (opcode >= 305 && opcode <= 318) || opcode == 86;
  }

  /**
   * @brief 《星痕共鸣》（Star / Unity 2022.3）DXVK 片段着色器反虚化处理
   *
   * 算法核心：
   * 1. 多特征锚点嗅探：覆盖 4x4 / 8x8 Bayer 矩阵 (17.0f, 1/17.0f, 1/16.0f, 16.0f)、
   *    DXVK Immediate Constant Buffer (icb) 以及屏幕坐标 FragCoord；
   * 2. SSA 双向数据流追踪：严格跟踪纹理采样依赖 (depends_on_sample)，100% 保护草地植被与镂空材质；
   * 3. CFG 安全中和：重定向 OpKill 前驱分支跳转目标以维持 Terminator 完整性，OpDemote 替换为 NOP。
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
      game_logger::log_msg("[反虚化驱动层-STAR-DXVK] 着色器: 0x%08x | 字长: %zu | 在黑名单中，跳过处理\n",
                           shader_hash, word_count);
      return;
    }

    bool is_force_mode = (game_logger::g_force_hashes.count(shader_hash) > 0);

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
    std::vector<uint8_t> is_dither_noise(bound, 0);
    std::vector<uint8_t> is_bayer_fraction(bound, 0);
    std::vector<uint8_t> is_icb_ptr(bound, 0);
    std::vector<uint32_t> cond_for_label(bound, 0);
    std::vector<std::vector<uint32_t>> ssa_operands(bound);

    bool has_any_frag_coord = false;
    bool has_any_sample = false;
    bool has_dither_signature = false;
    uint32_t c17_id = 0;

    // 阶段 1: 扫描常量、FragCoord、icb 以及 Bayer 特征
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
          while (str_len < max_bytes && str_ptr[str_len] != '\0') str_len++;
          std::string_view name(str_ptr, str_len);
          uint32_t target_id = spirv_code[i + 1];
          if (target_id < bound) {
            if (name == "icb" || name == "icb_buf") {
              is_icb_ptr[target_id] = 1;
              is_dither_noise[target_id] = 1;
              has_dither_signature = true;
            } else if (name.find("Bayer") != std::string_view::npos || name.find("Dither") != std::string_view::npos) {
              is_dither_noise[target_id] = 1;
              has_dither_signature = true;
            }
          }
        } else if (opcode == SPV_OP_DECORATE && length >= 4) {
          uint32_t target_id = spirv_code[i + 1];
          uint32_t decoration = spirv_code[i + 2];
          uint32_t builtin = spirv_code[i + 3];
          if (decoration == SPV_DECORATION_BUILTIN) {
            if (builtin == SPV_BUILTIN_FRAG_COORD || builtin == SPV_BUILTIN_SAMPLE_POSITION) {
              if (target_id < bound) {
                is_frag_coord_var[target_id] = 1;
                depends_on_frag_coord[target_id] = 1;
                has_any_frag_coord = true;
              }
            }
          }
        } else if (opcode == SPV_OP_CONSTANT && length >= 4) {
          uint32_t res_id = spirv_code[i + 2];
          FloatUint fu;
          fu.u = spirv_code[i + 3];

          // 1. 4x4 Bayer 矩阵模数 17.0f / 16.0f
          if (fu.f >= 16.999f && fu.f <= 17.001f) {
            if (res_id < bound) {
              c17_id = res_id;
              is_dither_noise[res_id] = 1;
              has_dither_signature = true;
            }
          } else if (fu.f >= 15.999f && fu.f <= 16.001f) {
            if (res_id < bound) {
              is_dither_noise[res_id] = 1;
              has_dither_signature = true;
            }
          }
          // 2. Bayer 归一化倒数: 1/17.0f (0.0588235f) 与 1/16.0f (0.0625f)
          else if ((fu.f >= 0.05882f && fu.f <= 0.05883f) ||
                   (fu.f >= 0.06249f && fu.f <= 0.06251f)) {
            if (res_id < bound) {
              is_bayer_fraction[res_id] = 1;
              is_dither_noise[res_id] = 1;
              has_dither_signature = true;
            }
          }
          // 3. 经典 IGN 噪声常数
          else if ((fu.f >= 0.06711f && fu.f <= 0.06712f) ||
                   (fu.f >= 52.982f && fu.f <= 52.983f)) {
            if (res_id < bound) {
              is_dither_noise[res_id] = 1;
              has_dither_signature = true;
            }
          }
        } else if (opcode == SPV_OP_CONSTANT_COMPOSITE && length >= 4) {
          uint32_t res_id = spirv_code[i + 2];
          uint32_t bayer_matches = 0;
          for (uint16_t c = 3; c < length; ++c) {
            uint32_t cid = spirv_code[i + c];
            if (cid < bound && is_bayer_fraction[cid]) bayer_matches++;
          }
          if (bayer_matches >= 3) {
            if (res_id < bound) {
              is_dither_noise[res_id] = 1;
              has_dither_signature = true;
            }
          }
        }

        i += length;
      }
    }

    // 阶段 2: 构建 SSA 依赖拓扑并标记纹理采样依赖
    {
      size_t i = 5;
      while (i < word_count) {
        uint32_t word = spirv_code[i];
        uint16_t opcode = word & 0xFFFF;
        uint16_t length = (word >> 16) & 0xFFFF;
        if (length == 0 || (i + length) > word_count)
          break;

        if (is_image_sample_opcode(opcode)) {
          has_any_sample = true;
          if (length >= 3) {
            uint32_t res_id = spirv_code[i + 2];
            if (res_id < bound) {
              depends_on_sample[res_id] = 1;
            }
          }
        } else if (opcode == SPV_OP_BRANCH_CONDITIONAL && length >= 4) {
          uint32_t cond = spirv_code[i + 1];
          uint32_t true_l = spirv_code[i + 2];
          uint32_t false_l = spirv_code[i + 3];
          if (true_l < bound) cond_for_label[true_l] = cond;
          if (false_l < bound) cond_for_label[false_l] = cond;
        } else if (opcode == SPV_OP_LOAD && length >= 4) {
          uint32_t res_id = spirv_code[i + 2];
          uint32_t ptr_id = spirv_code[i + 3];
          if (res_id < bound && ptr_id < bound) {
            if (is_frag_coord_var[ptr_id] || depends_on_frag_coord[ptr_id]) {
              depends_on_frag_coord[res_id] = 1;
            }
            if (is_icb_ptr[ptr_id] || is_dither_noise[ptr_id]) {
              is_dither_noise[res_id] = 1;
            }
            ssa_operands[res_id].push_back(ptr_id);
          }
        } else if (opcode == SPV_OP_ACCESS_CHAIN && length >= 4) {
          uint32_t res_id = spirv_code[i + 2];
          uint32_t base_ptr = spirv_code[i + 3];
          if (res_id < bound && base_ptr < bound) {
            if (is_icb_ptr[base_ptr] || is_dither_noise[base_ptr]) {
              is_icb_ptr[res_id] = 1;
              is_dither_noise[res_id] = 1;
            }
            ssa_operands[res_id].push_back(base_ptr);
          }
        } else if (opcode == SPV_OP_EXT_INST && length >= 5) {
          uint32_t res_id = spirv_code[i + 2];
          if (res_id < bound) {
            ssa_operands[res_id].assign(spirv_code + i + 5, spirv_code + i + length);
          }
        } else if (
            opcode == SPV_OP_COMPOSITE_EXTRACT ||
            (opcode >= 127 && opcode <= 136) /* FNeg, FAdd, FSub, FMul, FDiv, ... */ ||
            (opcode >= 169 && opcode <= 171) /* OpSelect, OpIEqual, OpLogicalAnd */ ||
            (opcode >= 180 && opcode <= 188) /* Comparisons: FOrdLessThan, etc. */) {
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

    // 阶段 3: 级联迭代传播 depends_on_sample 与 depends_on_frag_coord
    for (int iter = 0; iter < 12; ++iter) {
      bool changed = false;
      for (uint32_t id = 1; id < bound; ++id) {
        if (ssa_operands[id].empty()) continue;

        if (!depends_on_sample[id]) {
          for (uint32_t op : ssa_operands[id]) {
            if (op < bound && depends_on_sample[op]) {
              depends_on_sample[id] = 1;
              changed = true;
              break;
            }
          }
        }

        if (!depends_on_frag_coord[id]) {
          for (uint32_t op : ssa_operands[id]) {
            if (op < bound && depends_on_frag_coord[op]) {
              depends_on_frag_coord[id] = 1;
              changed = true;
              break;
            }
          }
        }

        if (!is_dither_noise[id]) {
          for (uint32_t op : ssa_operands[id]) {
            if (op < bound && is_dither_noise[op]) {
              is_dither_noise[id] = 1;
              changed = true;
              break;
            }
          }
        }
      }
      if (!changed) break;
    }

    // 辅助闭包: 判定条件是否依赖 Bayer 矩阵、噪波或 FragCoord
    auto is_dither_condition = [&](uint32_t cond) -> bool {
      if (cond == 0 || cond >= bound) return false;
      if (depends_on_sample[cond]) return false; // 严禁误伤纹理采样

      if (is_force_mode) return true;
      if (is_dither_noise[cond]) return true;
      if (has_dither_signature && depends_on_frag_coord[cond]) return true;

      // 反向可达性检索
      std::vector<uint8_t> visited(bound, 0);
      std::vector<uint32_t> stack;
      stack.reserve(32);
      stack.push_back(cond);

      while (!stack.empty()) {
        uint32_t curr = stack.back();
        stack.pop_back();

        if (curr >= bound || visited[curr]) continue;
        visited[curr] = 1;

        if (is_dither_noise[curr] || (c17_id > 0 && curr == c17_id)) {
          return true;
        }

        for (uint32_t op : ssa_operands[curr]) {
          if (op > 0 && op < bound && !visited[op]) {
            if (depends_on_sample[op]) {
              return false; // 链路上发现采样依赖，直接判定非虚化
            }
            if (is_dither_noise[op] || (c17_id > 0 && op == c17_id)) {
              return true;
            }
            stack.push_back(op);
          }
        }
      }

      return false;
    };

    // 阶段 4: 收集控制流块与丢弃指令
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
    {
      size_t i = 5;
      while (i < word_count) {
        uint32_t word = spirv_code[i];
        uint16_t opcode = word & 0xFFFF;
        uint16_t length = (word >> 16) & 0xFFFF;
        if (length == 0 || (i + length) > word_count) break;

        if (opcode == SPV_OP_LABEL && length >= 2) {
          current_label = spirv_code[i + 1];
        } else if (opcode == SPV_OP_BRANCH_CONDITIONAL && length >= 4) {
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
    }

    uint32_t dither_nops = 0;
    uint32_t preserved = 0;

    // 1. 条件分支重定向: 保持 Terminator 完整性，跳过包含 Kill 的网点丢弃块
    for (const auto& br : branch_insts) {
      bool true_has_kill = (br.true_label < bound && label_has_kill[br.true_label]);
      bool false_has_kill = (br.false_label < bound && label_has_kill[br.false_label]);
      if (!true_has_kill && !false_has_kill) continue;

      uint32_t cond = br.cond;
      bool is_dither = is_dither_condition(cond);

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

    // 2. Demote 指令定向中和
    for (const auto& dm : demote_insts) {
      bool is_dither = is_dither_condition(dm.cond);
      if (is_dither) {
        for (uint16_t k = 0; k < dm.length && (dm.offset + k) < word_count; ++k) {
          spirv_code[dm.offset + k] = SPV_OP_NOP;
        }
        dither_nops++;
      } else {
        preserved++;
      }
    }

    if (dither_nops > 0 || preserved > 0) {
      game_logger::log_msg("[反虚化驱动层-STAR-DXVK] 着色器: 0x%08x | 字长: %zu | 消除虚化: %u 处 | 保留正常裁剪: %u 处 | 类别: %s\n",
                           shader_hash, word_count, dither_nops, preserved,
                           has_dither_signature ? "4x4/8x8 Bayer 角色网点" : (has_any_frag_coord ? "屏幕空间消融" : "材质裁剪"));

      if (game_logger::g_dump_enabled && !orig_copy.empty()) {
        game_logger::dump_shader_bundle(orig_copy.data(), orig_copy.size(),
                                        spirv_code, word_count,
                                        shader_hash, dither_nops, preserved,
                                        has_any_frag_coord, has_any_sample);
      }
    }
  }

} // namespace star_dxvk
