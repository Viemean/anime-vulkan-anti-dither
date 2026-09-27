#pragma once

#include "../logger.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>
#include <unordered_set>
#include <unordered_map>
#include <string_view>

namespace zmd_vulkan {

  constexpr uint32_t SPV_HEADER_MAGIC                   = 0x07230203;
  constexpr uint32_t SPV_OP_NOP                         = 0x00010000;
  constexpr uint32_t SPV_OP_EXT_INST                    = 12;
  constexpr uint32_t SPV_OP_ENTRY_POINT                 = 15;
  constexpr uint32_t SPV_OP_CONSTANT                    = 43;
  constexpr uint32_t SPV_OP_CONSTANT_COMPOSITE          = 44;
  constexpr uint32_t SPV_OP_FUNCTION                    = 54;
  constexpr uint32_t SPV_OP_FUNCTION_PARAMETER          = 55;
  constexpr uint32_t SPV_OP_FUNCTION_END                = 56;
  constexpr uint32_t SPV_OP_FUNCTION_CALL               = 57;
  constexpr uint32_t SPV_OP_VARIABLE                    = 59;
  constexpr uint32_t SPV_OP_LOAD                        = 61;
  constexpr uint32_t SPV_OP_STORE                       = 62;
  constexpr uint32_t SPV_OP_ACCESS_CHAIN                = 65;
  constexpr uint32_t SPV_OP_DECORATE                    = 71;
  constexpr uint32_t SPV_OP_MEMBER_DECORATE             = 72;
  constexpr uint32_t SPV_OP_COMPOSITE_CONSTRUCT         = 80;
  constexpr uint32_t SPV_OP_COMPOSITE_EXTRACT           = 81;
  constexpr uint32_t SPV_OP_COPY_OBJECT                 = 83;
  constexpr uint32_t SPV_OP_DOT                         = 148;
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
   * @brief 《明日方舟：终末地》（Arknights: Endfield / ZMD）原生 Vulkan 片段着色器反虚化处理
   *
   * 特性：
   * 1. 采用原生 Vulkan / Unity GfxDeviceVK 架构；
   * 2. 识别 Bayer 4x4 / 8x8 抖动矩阵与 IGN 噪声特征；
   * 3. 严格隔离贴图采样（Alpha Cutout），保护大世界铁丝网（Fence）与植被；
   * 4. 在开启 ANTI_DITHER_DUMP 时完整导出供进一步逆向。
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
      game_logger::log_msg("[反虚化驱动层-ZMD-VULKAN] 着色器 0x%08x 在黑名单中，跳过处理\n", shader_hash);
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
    std::vector<uint8_t> is_dither_const(bound, 0);
    std::vector<uint8_t> is_dither_noise(bound, 0);
    std::vector<uint32_t> cond_for_label(bound, 0);
    std::vector<uint8_t> is_bayer_fraction(bound, 0);

    bool has_any_frag_coord = false;
    bool has_any_sample = false;
    bool has_dither_signature = false;

    // Pass 1: 扫描 FragCoord、Bayer 矩阵特征 (17.0f, 1/17.0f, 1/16.0f) 或 IGN 噪声
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
          if (name == "icb" || name.find("Bayer") != std::string_view::npos || name.find("Dither") != std::string_view::npos) {
            uint32_t target_id = spirv_code[i + 1];
            if (target_id < bound) {
              is_dither_const[target_id] = 1;
              is_dither_noise[target_id] = 1;
              has_dither_signature = true;
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

          // 4x4 / 8x8 Bayer 矩阵特征常数 (17.0f, 1/17.0f, 1/16.0f, 1/64.0f)
          if ((fu.f >= 16.999f && fu.f <= 17.001f) ||
              (fu.f >= 0.05882f && fu.f <= 0.05883f) || // 1/17.0
              (fu.f >= 0.06249f && fu.f <= 0.06251f) || // 1/16.0
              (fu.f >= 0.015624f && fu.f <= 0.015626f)) { // 1/64.0
            if (res_id < bound) {
              is_bayer_fraction[res_id] = 1;
              is_dither_const[res_id] = 1;
              has_dither_signature = true;
            }
          }
          // IGN 噪声常数
          else if ((fu.f >= 0.06711f && fu.f <= 0.06712f) ||
                   (fu.f >= 0.005837f && fu.f <= 0.005838f) ||
                   (fu.f >= 52.982f && fu.f <= 52.983f)) {
            if (res_id < bound) {
              is_dither_const[res_id] = 1;
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
              is_dither_const[res_id] = 1;
              is_dither_noise[res_id] = 1;
              has_dither_signature = true;
            }
          }
        }
        i += length;
      }
    }

    // 若无特征且非强制，则安全放行并 Dump 用于首发分析
    if (!has_dither_signature && !is_force_mode) {
      if (game_logger::g_dump_enabled && has_any_discard) {
        game_logger::dump_shader_bundle(spirv_code, word_count,
                                        spirv_code, word_count,
                                        shader_hash, 0, 0,
                                        has_any_frag_coord, has_any_sample);
        game_logger::log_msg("[反虚化驱动层-ZMD-VULKAN] 捕获着色器(Dump): 0x%08x | 字长: %zu | 待分析特征\n",
                             shader_hash, word_count);
      }
      return;
    }

    // Pass 2: 追踪 FragCoord 与 Dither 噪声传播，严格隔离纹理采样
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
        if (length == 0 || (i + length) > word_count) break;

        if (opcode == SPV_OP_BRANCH_CONDITIONAL && length >= 4) {
          uint32_t cond = spirv_code[i + 1];
          uint32_t true_l = spirv_code[i + 2];
          uint32_t false_l = spirv_code[i + 3];
          if (true_l < bound) cond_for_label[true_l] = cond;
          if (false_l < bound) cond_for_label[false_l] = cond;
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

        // 纹理采样边界隔离：输出绝对不标记为 Dither 噪声
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

        // 屏幕空间坐标与常数点积
        if (opcode == SPV_OP_DOT && length >= 5) {
          uint32_t op1 = spirv_code[i + 3];
          uint32_t op2 = spirv_code[i + 4];
          if (op1 < bound && op2 < bound) {
            if ((is_dither_const[op1] || is_dither_const[op2]) && (depends_on_frag_coord[op1] || depends_on_frag_coord[op2])) {
              if (res_id < bound && !is_dither_noise[res_id]) {
                is_dither_noise[res_id] = 1;
                has_dither_signature = true;
                changed = true;
              }
            }
          }
        }

        // 传播依赖：采样产物不继承噪声
        if (res_id > 0 && res_id < bound) {
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

    // Pass 3: 收集分支与丢弃结构并安全中和 (重定向条件分支以保持 CFG 基本块 Terminator 合法)
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

    size_t i = 5;
    uint32_t current_label = 0;
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
      } else if (opcode == SPV_OP_DEMOTE_TO_HELPER_INVOCATION || opcode == 5380 /* OpDemoteToHelperInvocationEXT */) {
        uint32_t cond = (current_label < bound) ? cond_for_label[current_label] : 0;
        demote_insts.push_back({i, length, cond});
      }

      i += length;
    }

    uint32_t noped_count = 0;
    uint32_t preserved_cutout_count = 0;

    // 1. 条件分支重定向: 跳过 Dither 对应的 OpKill 基本块，保持基本块 Terminator 完整
    for (const auto& br : branch_insts) {
      bool true_has_kill = (br.true_label < bound && label_has_kill[br.true_label]);
      bool false_has_kill = (br.false_label < bound && label_has_kill[br.false_label]);
      if (!true_has_kill && !false_has_kill) continue;

      uint32_t cond = br.cond;
      bool is_dither = false;
      if (is_force_mode) {
        is_dither = true;
      } else if (cond > 0 && cond < bound) {
        if (is_dither_noise[cond] && !depends_on_sample[cond]) {
          is_dither = true;
        }
      }

      if (true_has_kill) {
        if (is_dither) {
          spirv_code[br.offset + 2] = br.false_label;
          noped_count++;
        } else {
          preserved_cutout_count++;
        }
      }

      if (false_has_kill) {
        if (is_dither) {
          spirv_code[br.offset + 3] = br.true_label;
          noped_count++;
        } else {
          preserved_cutout_count++;
        }
      }
    }

    // 2. 非 Terminator 类型的 Demote 指令中和
    for (const auto& dm : demote_insts) {
      bool is_dither = false;
      if (is_force_mode) {
        is_dither = true;
      } else if (dm.cond > 0 && dm.cond < bound) {
        if (is_dither_noise[dm.cond] && !depends_on_sample[dm.cond]) {
          is_dither = true;
        }
      }

      if (is_dither) {
        for (uint16_t k = 0; k < dm.length && (dm.offset + k) < word_count; ++k) {
          spirv_code[dm.offset + k] = (1 << 16) | 0; // SPV_OP_NOP
        }
        noped_count++;
      } else {
        preserved_cutout_count++;
      }
    }

    if ((noped_count + preserved_cutout_count) > 0) {
      const char* category = has_dither_signature ? "角色点阵(Bayer Dither)" : "材质/铁丝网裁剪";
      game_logger::log_msg("[反虚化驱动层-ZMD-VULKAN] 着色器: 0x%08x | 字长: %zu | 消除虚化: %u 处 | 保留正常裁剪: %u 处 | 类别: %s\n",
                           shader_hash, word_count, noped_count, preserved_cutout_count, category);
    }

    if (game_logger::g_dump_enabled && (noped_count > 0 || preserved_cutout_count > 0)) {
      game_logger::dump_shader_bundle(orig_copy.data(), orig_copy.size(),
                                      spirv_code, word_count,
                                      shader_hash, noped_count, preserved_cutout_count,
                                      has_any_frag_coord, has_any_sample);
    }
  }

} // namespace zmd_vulkan
