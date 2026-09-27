#pragma once

#include "../logger.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>
#include <string_view>

namespace gf2_dxvk {

  constexpr uint32_t SPV_HEADER_MAGIC                   = 0x07230203;
  constexpr uint32_t SPV_OP_NOP                         = 0x00010000;
  constexpr uint32_t SPV_OP_EXT_INST                    = 12;
  constexpr uint32_t SPV_OP_ENTRY_POINT                 = 15;
  constexpr uint32_t SPV_OP_CONSTANT                    = 43;
  constexpr uint32_t SPV_OP_CONSTANT_COMPOSITE          = 44;
  constexpr uint32_t SPV_OP_FUNCTION                    = 54;
  constexpr uint32_t SPV_OP_FUNCTION_PARAMETER          = 55;
  constexpr uint32_t SPV_OP_FUNCTION_END                = 56;
  constexpr uint32_t SPV_OP_VARIABLE                    = 59;
  constexpr uint32_t SPV_OP_LOAD                        = 61;
  constexpr uint32_t SPV_OP_STORE                       = 62;
  constexpr uint32_t SPV_OP_ACCESS_CHAIN                = 65;
  constexpr uint32_t SPV_OP_DECORATE                    = 71;
  constexpr uint32_t SPV_OP_MEMBER_DECORATE             = 72;
  constexpr uint32_t SPV_OP_COMPOSITE_CONSTRUCT         = 80;
  constexpr uint32_t SPV_OP_COMPOSITE_EXTRACT           = 81;
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
   * @brief 《少女前线2：追放》（Girls' Frontline 2: Exilium / GF2）DXVK (DirectX 11) 片段着色器反虚化处理
   *
   * 算法核心：
   * 1. 识别 Unity 引擎在少前2中使用的多维相机淡出与近身消融逻辑：
   *    - 头部/特定部位：FragCoord 屏幕空间坐标与 4x4 / 8x8 Bayer 矩阵点阵；
   *    - 角色身体/皮肤：3D Hash 噪波 (0.1031 与 31.32 散列) 消融；
   *    - 角色衣物/配件：Uniform 缓冲区动态消融阈值 (NMin/FMin) 及 cb2 Cutoff 动态裁剪；
   * 2. 严格保护环境物体：植被树木与铁丝网等基于固定常数 (如 0.5f) 的 Alpha Cutout 100% 完整保留；
   * 3. 针对命中条件丢弃指令：OpKill 执行分支重定向保持 Terminator，OpDemote 替换为 NOP，解决穿透消失问题。
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
      game_logger::log_msg("[反虚化驱动层-GF2-DXVK] 着色器: 0x%08x | 字长: %zu | 在黑名单中，跳过处理\n",
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
    std::vector<uint8_t> is_uniform_ptr(bound, 0);
    std::vector<uint8_t> is_uniform_val(bound, 0);
    std::vector<uint8_t> is_cb2_ptr(bound, 0);
    std::vector<uint8_t> depends_on_cb2(bound, 0);
    std::vector<uint8_t> is_fade_threshold(bound, 0);
    std::vector<uint8_t> is_dither_noise(bound, 0);
    std::vector<uint32_t> cond_for_label(bound, 0);
    std::vector<uint8_t> is_bayer_fraction(bound, 0);
    std::vector<uint8_t> is_05_const(bound, 0);
    std::vector<uint8_t> is_cutout_sub(bound, 0);
    std::vector<uint8_t> is_alpha_cutout_cond(bound, 0);
    std::vector<uint8_t> is_boundary_cutout_cond(bound, 0);

    std::vector<uint32_t> var_binding(bound, 0xFFFFFFFF);
    std::vector<uint32_t> uniform_vars;

    bool has_any_frag_coord = false;
    bool has_front_face = false;
    bool has_any_sample = false;
    bool has_dither_signature = false;
    bool has_cb2 = false;

    // Pass 1: 扫描 FragCoord、FrontFacing、Bayer 矩阵、3D Hash 噪波、IGN 噪波及 cb2 角色材质常量缓冲
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
            if (name == "cb2" || name == "cb2_buf") {
              is_cb2_ptr[target_id] = 1;
              depends_on_cb2[target_id] = 1;
              is_uniform_ptr[target_id] = 1;
              has_cb2 = true;
              has_dither_signature = true;
            } else if (name == "cb0" || name == "cb0_buf" || name == "cb1" || name == "cb1_buf" || name == "icb" || name == "icb_buf") {
              is_uniform_ptr[target_id] = 1;
            } else if (name.find("Bayer") != std::string_view::npos || name.find("Dither") != std::string_view::npos) {
              is_dither_noise[target_id] = 1;
              has_dither_signature = true;
            }
          }
        } else if (opcode == 59 /* OpVariable */ && length >= 4) {
          uint32_t res_id = spirv_code[i + 2];
          uint32_t storage_class = spirv_code[i + 3];
          if (res_id < bound) {
            if (storage_class == 2 /* StorageClassUniform */ || storage_class == 0 /* UniformConstant */) {
              is_uniform_ptr[res_id] = 1;
              uniform_vars.push_back(res_id);
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
                has_dither_signature = true;
              }
            } else if (builtin == 17 /* SpvBuiltInFrontFacing */) {
              has_front_face = true;
            }
          } else if (decoration == 33 /* SpvDecorationBinding */) {
            if (target_id < bound) {
              var_binding[target_id] = builtin;
            }
          }
        } else if (opcode == SPV_OP_CONSTANT && length >= 4) {
          uint32_t res_id = spirv_code[i + 2];
          FloatUint fu;
          fu.u = spirv_code[i + 3];

          // 0. 纹理镂空标准阈值常数 (0.5f，用于植物树叶、镂空等 Alpha Test 保护)
          if (fu.f >= 0.499f && fu.f <= 0.501f) {
            if (res_id < bound) {
              is_05_const[res_id] = 1;
            }
          }
          // 1. 3D Hash 噪波特征常数 (少前2角色近身消融: 0.103100002 与 31.3199997)
          else if ((fu.f >= 0.10309f && fu.f <= 0.10311f) ||
                   (fu.f >= 31.319f && fu.f <= 31.321f)) {
            if (res_id < bound) {
              is_dither_noise[res_id] = 1;
              has_dither_signature = true;
            }
          }
          // 2. 4x4 / 8x8 Bayer 矩阵特征常数 (17.0f, 1/17.0f, 1/16.0f, 1/64.0f)
          else if ((fu.f >= 16.999f && fu.f <= 17.001f) ||
                   (fu.f >= 0.05882f && fu.f <= 0.05883f) ||
                   (fu.f >= 0.06249f && fu.f <= 0.06251f) ||
                   (fu.f >= 0.015624f && fu.f <= 0.015626f)) {
            if (res_id < bound) {
              is_bayer_fraction[res_id] = 1;
              is_dither_noise[res_id] = 1;
              has_dither_signature = true;
            }
          }
          // 3. IGN 噪声常数
          else if ((fu.f >= 0.06711f && fu.f <= 0.06712f) ||
                   (fu.f >= 0.005837f && fu.f <= 0.005838f) ||
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
        } else if (opcode == SPV_OP_ACCESS_CHAIN && length >= 4) {
          uint32_t base_ptr = spirv_code[i + 3];
          uint32_t res_id = spirv_code[i + 2];
          if (res_id < bound) {
            if (base_ptr < bound && (is_cb2_ptr[base_ptr] || depends_on_cb2[base_ptr])) {
              is_cb2_ptr[res_id] = 1;
              depends_on_cb2[res_id] = 1;
              is_uniform_ptr[res_id] = 1;
              has_cb2 = true;
              has_dither_signature = true;
            } else if (base_ptr < bound && is_uniform_ptr[base_ptr]) {
              is_uniform_ptr[res_id] = 1;
            } else {
              is_uniform_ptr[res_id] = 1;
            }
          }
        } else if (opcode == SPV_OP_LOAD && length >= 4) {
          uint32_t ptr_id = spirv_code[i + 3];
          uint32_t res_id = spirv_code[i + 2];
          if (res_id < bound && ptr_id < bound) {
            if (is_uniform_ptr[ptr_id]) {
              is_uniform_val[res_id] = 1;
            }
            if (is_cb2_ptr[ptr_id] || depends_on_cb2[ptr_id]) {
              depends_on_cb2[res_id] = 1;
              is_fade_threshold[res_id] = 1;
              has_cb2 = true;
              has_dither_signature = true;
            }
          }
        } else if (opcode == SPV_OP_EXT_INST && length >= 7) {
          uint32_t ext_inst = spirv_code[i + 4];
          uint32_t res_id = spirv_code[i + 2];
          // GLSL.std.450 NMin = 79, FMin = 37
          if (ext_inst == 79 || ext_inst == 37) {
            uint32_t op1 = spirv_code[i + 5];
            uint32_t op2 = spirv_code[i + 6];
            if (op1 < bound && op2 < bound) {
              if (is_uniform_val[op1] && is_uniform_val[op2]) {
                if (res_id < bound) {
                  is_fade_threshold[res_id] = 1;
                  has_dither_signature = true;
                }
              }
            }
          }
        }
        i += length;
      }
    }

    // 加固: 若着色器被剥离调试符号 (Strip OpName) 导致未匹配到 "cb2"，
    // 依据 D3D11 常量缓冲槽位映射到 Binding 递增的规范兜底定位 cb2 (槽位 2 为第 3 个 uniform buffer)
    if (!has_cb2 && uniform_vars.size() >= 3) {
      std::sort(uniform_vars.begin(), uniform_vars.end(), [&](uint32_t a, uint32_t b) {
        return var_binding[a] < var_binding[b];
      });
      uint32_t cb2_target = uniform_vars[2];
      if (cb2_target < bound && var_binding[cb2_target] != 0xFFFFFFFF) {
        is_cb2_ptr[cb2_target] = 1;
        depends_on_cb2[cb2_target] = 1;
        has_cb2 = true;
        has_dither_signature = true;
      }
    }

    // 判定是否属于角色模型着色器 (二次元卡通渲染必须依赖 FrontFacing 判断正反面与描边，或依赖 FragCoord 计算屏幕网点)
    bool is_character_shader = (has_front_face || has_any_frag_coord);

    // 若非角色物体 (植物/树叶/大树/盆栽/草地/建筑/特效绝不包含 FrontFacing 与 FragCoord)，100% 严格保护，放行不修改
    if (!is_character_shader && !is_force_mode) {
      if (game_logger::g_dump_enabled && has_any_discard) {
        game_logger::dump_shader_bundle(spirv_code, word_count,
                                        spirv_code, word_count,
                                        shader_hash, 0, 0,
                                        has_any_frag_coord, has_any_sample);
        game_logger::log_msg("[反虚化驱动层-GF2-DXVK] 环境/植被物体保护: 0x%08x | 字长: %zu | 保留镂空\n",
                             shader_hash, word_count);
      }
      return;
    }

    // 若无特征且非强制模式，Dump 并安全放行
    if (!has_dither_signature && !is_force_mode) {
      if (game_logger::g_dump_enabled && has_any_discard) {
        game_logger::dump_shader_bundle(spirv_code, word_count,
                                        spirv_code, word_count,
                                        shader_hash, 0, 0,
                                        has_any_frag_coord, has_any_sample);
        game_logger::log_msg("[反虚化驱动层-GF2-DXVK] 捕获着色器(Dump): 0x%08x | 字长: %zu | 待分析特征\n",
                             shader_hash, word_count);
      }
      return;
    }

    // Pass 2: 追踪 FragCoord、Hash 噪波、cb2 参数与相机淡出阈值传播，同时隔离普通纹理采样
    bool changed = true;
    uint32_t iteration = 0;
    constexpr uint32_t max_iterations = 32;

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

        // 纹理采样标记
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

        if (opcode == SPV_OP_ACCESS_CHAIN && length >= 4) {
          uint32_t base_ptr = spirv_code[i + 3];
          if (base_ptr < bound && res_id < bound) {
            if (is_cb2_ptr[base_ptr] || depends_on_cb2[base_ptr]) {
              if (!depends_on_cb2[res_id]) {
                depends_on_cb2[res_id] = 1;
                is_cb2_ptr[res_id] = 1;
                changed = true;
              }
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
            if (is_dither_noise[ptr_id] && res_id < bound && !is_dither_noise[res_id]) {
              is_dither_noise[res_id] = 1;
              changed = true;
            }
            if (is_fade_threshold[ptr_id] && res_id < bound && !is_fade_threshold[res_id]) {
              is_fade_threshold[res_id] = 1;
              changed = true;
            }
            if (depends_on_cb2[ptr_id] && res_id < bound && !depends_on_cb2[res_id]) {
              depends_on_cb2[res_id] = 1;
              changed = true;
            }
          }
        }

        // 屏幕空间坐标点积
        if (opcode == SPV_OP_DOT && length >= 5) {
          uint32_t op1 = spirv_code[i + 3];
          uint32_t op2 = spirv_code[i + 4];
          if (op1 < bound && op2 < bound) {
            if ((is_dither_noise[op1] || is_dither_noise[op2]) && (depends_on_frag_coord[op1] || depends_on_frag_coord[op2])) {
              if (res_id < bound && !is_dither_noise[res_id]) {
                is_dither_noise[res_id] = 1;
                changed = true;
              }
            }
          }
        }

        // 识别减去 0.5f 的纯静态 Alpha Cutout 算术结构 (OpFSub = 131)
        if (opcode == 131 /* OpFSub */ && length >= 5) {
          uint32_t op1 = spirv_code[i + 3];
          uint32_t op2 = spirv_code[i + 4];
          if ((op2 < bound && is_05_const[op2]) || (op1 < bound && is_05_const[op1])) {
            if (res_id < bound && !is_cutout_sub[res_id]) {
              is_cutout_sub[res_id] = 1;
              changed = true;
            }
          }
        }

        // 识别比较指令 (OpFOrdLessThan = 184 等)
        if ((opcode >= 180 && opcode <= 190) && length >= 5) {
          uint32_t op1 = spirv_code[i + 3];
          uint32_t op2 = spirv_code[i + 4];
          if ((op1 < bound && (is_cutout_sub[op1] || is_05_const[op1])) ||
              (op2 < bound && (is_cutout_sub[op2] || is_05_const[op2]))) {
            if (res_id < bound && !is_alpha_cutout_cond[res_id]) {
              is_alpha_cutout_cond[res_id] = 1;
              changed = true;
            }
          }
        }

        // 数据流传播
        if (res_id > 0 && res_id < bound) {
          uint16_t start_op = (opcode == SPV_OP_EXT_INST) ? 5 : 3;
          uint16_t end_op = length;
          if (opcode == SPV_OP_COMPOSITE_EXTRACT) end_op = 4;

          for (uint16_t k = start_op; k < end_op && (i + k) < word_count; ++k) {
            uint32_t op_val = spirv_code[i + k];
            if (op_val < bound) {
              if (is_alpha_cutout_cond[op_val] && !is_alpha_cutout_cond[res_id]) {
                is_alpha_cutout_cond[res_id] = 1;
                changed = true;
              }
              if (!is_image_sample_opcode(opcode) && is_dither_noise[op_val] && !is_dither_noise[res_id]) {
                is_dither_noise[res_id] = 1;
                changed = true;
              }
              if (is_fade_threshold[op_val] && !is_fade_threshold[res_id]) {
                is_fade_threshold[res_id] = 1;
                changed = true;
              }
              if (depends_on_cb2[op_val] && !depends_on_cb2[res_id]) {
                depends_on_cb2[res_id] = 1;
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

    // Pass 3: 收集丢弃结构并安全中和
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

    auto should_neutralize = [&](uint32_t cond) -> bool {
      if (is_force_mode) return true;
      if (cond == 0 || cond >= bound) return false;

      // 角色布料上的纯静态 Alpha Cutout 镂空 (包含 0.5f 静态常数阈值比较)，绝对保护
      if (is_alpha_cutout_cond[cond]) {
        return false;
      }

      // 中和规则 1: 依赖 FragCoord 的屏幕空间 Bayer/点阵虚化 (头部面部、长发)
      if (depends_on_frag_coord[cond] && !depends_on_sample[cond]) {
        return true;
      }
      // 中和规则 2: 命中相机淡出阈值或点阵噪声
      if (is_fade_threshold[cond] || is_dither_noise[cond]) {
        return true;
      }
      // 中和规则 3: 角色模型 (衣服、身体、帽子、首饰)，消隐条件依赖动态 cb2
      if (is_character_shader && has_cb2) {
        if (depends_on_cb2[cond] || is_fade_threshold[cond]) {
          return true;
        }
      }
      return false;
    };

    // 1. 条件分支重定向: 跳过相机虚化对应的 OpKill 块，CFG 保持合法
    for (const auto& br : branch_insts) {
      bool true_has_kill = (br.true_label < bound && label_has_kill[br.true_label]);
      bool false_has_kill = (br.false_label < bound && label_has_kill[br.false_label]);
      if (!true_has_kill && !false_has_kill) continue;

      uint32_t cond = br.cond;
      if (should_neutralize(cond)) {
        if (true_has_kill && !false_has_kill) {
          spirv_code[br.offset + 2] = br.false_label;
          noped_count++;
        } else if (false_has_kill && !true_has_kill) {
          spirv_code[br.offset + 3] = br.true_label;
          noped_count++;
        }
      } else {
        preserved_cutout_count++;
      }
    }

    // 2. OpDemoteToHelperInvocation 处理: 置为 NOP
    for (const auto& dinfo : demote_insts) {
      uint32_t cond = dinfo.cond;
      if (should_neutralize(cond)) {
        for (uint16_t w = 0; w < dinfo.length; ++w) {
          spirv_code[dinfo.offset + w] = SPV_OP_NOP;
        }
        noped_count++;
      } else {
        preserved_cutout_count++;
      }
    }

    if (noped_count > 0) {
      game_logger::log_msg("[反虚化驱动层-GF2-DXVK] 着色器: 0x%08x | 字长: %zu | 消除虚化: %u 处 | 保留正常裁剪: %u 处 | 类别: %s\n",
                           shader_hash, word_count, noped_count, preserved_cutout_count, "角色全网格/相机穿透淡出消除");

      if (game_logger::g_dump_enabled && !orig_copy.empty()) {
        game_logger::dump_shader_bundle(orig_copy.data(), orig_copy.size(),
                                        spirv_code, word_count,
                                        shader_hash, noped_count, preserved_cutout_count,
                                        has_any_frag_coord, has_any_sample);
      }
    }
  }

} // namespace gf2_dxvk
