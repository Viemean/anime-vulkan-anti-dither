#pragma once

#include "../logger.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>
#include <unordered_set>
#include <unordered_map>
#include <string_view>

namespace silver_palace_vkd3d {

  constexpr uint32_t SPV_HEADER_MAGIC                       = 0x07230203;
  constexpr uint32_t SPV_OP_NOP                             = 0;
  constexpr uint32_t SPV_OP_NAME                            = 5;
  constexpr uint32_t SPV_OP_EXT_INST                        = 12;
  constexpr uint32_t SPV_OP_ENTRY_POINT                     = 15;
  constexpr uint32_t SPV_OP_CONSTANT_TRUE                   = 41;
  constexpr uint32_t SPV_OP_CONSTANT_FALSE                  = 42;
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
  constexpr uint32_t SPV_OP_COPY_OBJECT                    = 83;
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

  /**
   * @brief 《白银之城》(Silver Palace) VKD3D-Proton (DirectX 12 / UE 5.4.1) 片段着色器反虚化处理。
   *
   * @param spirv_code 欲处理的 SPIR-V 32 位字数组指针。
   * @param word_count 字节码包含的 32 位字数量。
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
      game_logger::log_msg("[SilverPalace-VKD3D] 着色器 0x%08x 在黑名单中，跳过处理\n", shader_hash);
      return;
    }

    bool is_force_mode = (game_logger::g_force_hashes.count(shader_hash) > 0);

    // 仅处理 VKD3D 转译的游戏着色器 (Tool ID 30017)
    uint32_t generator = spirv_code[2];
    uint32_t tool_id = (generator >> 16);
    if (tool_id != 30017 && !is_force_mode) {
      return;
    }

    // 着色器基础规模过滤 (角色卡通材质在 [400, 8500] 区间，排除 >12000 的全屏光照与后处理 UberShader)
    if ((word_count < 20 || word_count > 12000) && !is_force_mode) {
      return;
    }

    // 检查是否存在 discard 或 demote
    bool has_any_discard_opcode = false;
    for (size_t k = 5; k < word_count; ) {
      uint32_t w = spirv_code[k];
      uint16_t op = w & 0xFFFF;
      uint16_t l = (w >> 16) & 0xFFFF;
      if (l == 0 || (k + l) > word_count) break;
      if (is_discard_opcode(op)) {
        has_any_discard_opcode = true;
        break;
      }
      k += l;
    }
    if (!has_any_discard_opcode && !is_force_mode) {
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
    std::vector<uint8_t> is_demote_func(bound, 0);
    std::vector<uint8_t> has_parameters(bound, 0);
    std::vector<uint32_t> cond_for_label(bound, 0);

    bool has_any_frag_coord = false;
    bool has_any_sample = false;
    (void)has_any_frag_coord;
    (void)has_any_sample;
    bool has_dither_signature = false;
    bool is_niagara_or_particle = false;

    std::vector<uint8_t> is_bayer_fraction(bound, 0);
    std::vector<uint8_t> is_fade_bias_const(bound, 0);
    std::vector<uint8_t> is_depth_cutoff_const(bound, 0);
    std::vector<uint8_t> depends_on_fade(bound, 0);
    std::vector<uint8_t> is_fabs_result(bound, 0);
    std::vector<uint8_t> is_depth_cutoff_val(bound, 0);
    std::vector<uint8_t> is_depth_cutoff_cond(bound, 0);

    bool has_soft_fade_099 = false;
    bool has_soft_fade_15 = false;
    std::vector<uint32_t> candidate_fade_0833_ids;

    uint32_t zero_float_id = 0;
    uint32_t false_const_id = 0;

    // 阶段 1: 扫描 FragCoord、Bayer/IGN 常量及材质标记
    {
      size_t i = 5;
      while (i < word_count) {
        uint32_t word = spirv_code[i];
        uint16_t opcode = word & 0xFFFF;
        uint16_t length = (word >> 16) & 0xFFFF;
        if (length == 0 || (i + length) > word_count)
          break;

        if (opcode == SPV_OP_NAME && length >= 3) {
          size_t max_bytes = static_cast<size_t>(length - 2) * sizeof(uint32_t);
          const char* str_ptr = reinterpret_cast<const char*>(&spirv_code[i + 2]);
          size_t str_len = 0;
          while (str_len < max_bytes && str_ptr[str_len] != '\0') str_len++;
          std::string_view name(str_ptr, str_len);
          if (name.find("NIAGARA") != std::string_view::npos ||
              name.find("PARTICLE") != std::string_view::npos ||
              name.find("Niagara") != std::string_view::npos ||
              name.find("Particle") != std::string_view::npos) {
            is_niagara_or_particle = true;
          }
        } else if (opcode == SPV_OP_CONSTANT_FALSE && length >= 3) {
          uint32_t res_id = spirv_code[i + 2];
          if (res_id < bound) {
            false_const_id = res_id;
          }
        } else if (opcode == SPV_OP_DECORATE && length >= 3) {
          uint32_t target_id = spirv_code[i + 1];
          uint32_t decoration = spirv_code[i + 2];
          if (length >= 4) {
            uint32_t builtin = spirv_code[i + 3];
            if (decoration == SPV_DECORATION_BUILTIN &&
                (builtin == SPV_BUILTIN_FRAG_COORD || builtin == SPV_BUILTIN_SAMPLE_POSITION)) {
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

          if (fu.u == 0 || fu.f == 0.0f) {
            if (res_id < bound) {
              zero_float_id = res_id;
            }
          }

          // UE5 角色软淡出过渡常数 (0.99000001 与 1.5)
          if (fu.u == 0x3f7d70a4 || (fu.f >= 0.9899f && fu.f <= 0.9901f)) {
            has_soft_fade_099 = true;
          } else if (fu.u == 0x3fc00000 || (fu.f >= 1.4999f && fu.f <= 1.5001f)) {
            has_soft_fade_15 = true;
          }
          // UE5 核心 DitherTemporalAA 常量 (0.166649997)
          else if (fu.u == 0x3e2aa64c || (fu.f >= 0.16664f && fu.f <= 0.16666f)) {
            if (res_id < bound) {
              is_dither_const[res_id] = 1;
              is_dither_noise[res_id] = 1;
              has_dither_signature = true;
            }
          }
          // UE5 角色相机淡出偏置常量 (-0.333299994)
          else if (fu.u == 0xbeaaa64c || (fu.f >= -0.3334f && fu.f <= -0.3332f)) {
            if (res_id < bound) {
              is_fade_bias_const[res_id] = 1;
              depends_on_fade[res_id] = 1;
              has_dither_signature = true;
            }
          }
          // 候选角色淡出偏置常量 (-0.8333f，涵盖 0xbf553f7d 与 0xbf555326)
          else if (fu.f >= -0.8334f && fu.f <= -0.8332f) {
            if (res_id < bound) {
              candidate_fade_0833_ids.push_back(res_id);
            }
          }
          // 角色近景深度裁切阈值常量 (10.0f)
          else if (fu.u == 0x41200000 || (fu.f >= 9.999f && fu.f <= 10.001f)) {
            if (res_id < bound) {
              is_depth_cutoff_const[res_id] = 1;
            }
          }
          // 《白银之城》散斑余弦哈希特征常量: 347.834503, 3343.28369
          else if ((fu.f >= 347.834f && fu.f <= 347.835f) ||
                   (fu.f >= 3343.28f && fu.f <= 3343.29f)) {
            if (res_id < bound) {
              is_dither_const[res_id] = 1;
              is_dither_noise[res_id] = 1;
              has_dither_signature = true;
            }
          }
          // 严格 3x3 Bayer 矩阵循环小数特征
          else if ((fu.f >= 0.1110f && fu.f <= 0.1112f) || // 1/9
                   (fu.f >= 0.2221f && fu.f <= 0.2223f) || // 2/9
                   (fu.f >= 0.4443f && fu.f <= 0.4445f) || // 4/9
                   (fu.f >= 0.5554f && fu.f <= 0.5556f) || // 5/9
                   (fu.f >= 0.7776f && fu.f <= 0.7779f) || // 7/9
                   (fu.f >= 0.8887f && fu.f <= 0.8890f)) { // 8/9
            if (res_id < bound) {
              is_bayer_fraction[res_id] = 1;
            }
          }
          // 交错梯度噪声 (IGN) 特征常量
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
          // 仅当复合常量数组包含至少 3 个 Bayer 分数时判定为 Bayer 矩阵
          if (bayer_matches >= 3) {
            if (res_id < bound) {
              is_dither_const[res_id] = 1;
              is_dither_noise[res_id] = 1;
              has_dither_signature = true;
            }
          }
        } else if (opcode == SPV_OP_VARIABLE && length >= 5) {
          uint32_t res_id = spirv_code[i + 2];
          uint32_t init_id = spirv_code[i + 4];
          if (init_id < bound && is_dither_const[init_id]) {
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

    // 角色动态近身软淡出正交判定：仅当同时包含 0.99f 与 1.5f 软淡出插值常数时，激活 -0.8333f 淡出中和
    if (has_soft_fade_099 && has_soft_fade_15) {
      for (uint32_t cid : candidate_fade_0833_ids) {
        if (cid < bound) {
          is_fade_bias_const[cid] = 1;
          depends_on_fade[cid] = 1;
          has_dither_signature = true;
        }
      }
    }

    // 排除 Niagara 与粒子特效
    if (is_niagara_or_particle && !is_force_mode) {
      return;
    }

    // 阶段 2: 追踪 FragCoord、贴图采样与网点噪声传播
    bool changed = true;
    uint32_t iteration = 0;
    constexpr uint32_t max_iterations = 24;

    while (changed && iteration < max_iterations) {
      changed = false;
      iteration++;
      size_t i = 5;
      uint32_t current_func = 0;

      while (i < word_count) {
        uint32_t word = spirv_code[i];
        uint16_t opcode = word & 0xFFFF;
        uint16_t length = (word >> 16) & 0xFFFF;
        if (length == 0 || (i + length) > word_count) break;

        if (opcode == SPV_OP_FUNCTION && length >= 3) {
          current_func = spirv_code[i + 2];
        } else if (opcode == SPV_OP_FUNCTION_PARAMETER) {
          if (current_func > 0 && current_func < bound) has_parameters[current_func] = 1;
        } else if (opcode == SPV_OP_FUNCTION_END) {
          current_func = 0;
        } else if (is_discard_opcode(opcode)) {
          if (current_func > 0 && current_func < bound) is_demote_func[current_func] = 1;
        } else if (opcode == SPV_OP_BRANCH_CONDITIONAL && length >= 4) {
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

        // 标记非 FragCoord 采样的纹理输出
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

        // 向量构造传播 Dither 常量与噪声
        if (opcode == SPV_OP_COMPOSITE_CONSTRUCT && length >= 4) {
          if (res_id < bound) {
            for (uint16_t c = 3; c < length; ++c) {
              uint32_t cid = spirv_code[i + c];
              if (cid < bound) {
                if (is_dither_const[cid] && !is_dither_const[res_id]) {
                  is_dither_const[res_id] = 1;
                  is_dither_noise[res_id] = 1;
                  has_dither_signature = true;
                  changed = true;
                }
                if (is_dither_noise[cid] && !is_dither_noise[res_id]) {
                  is_dither_noise[res_id] = 1;
                  has_dither_signature = true;
                  changed = true;
                }
              }
            }
          }
        }

        // 结合 Dither 常量、Bayer 分数或噪声的点积运算标记为噪声源
        if (opcode == SPV_OP_DOT && length >= 5) {
          uint32_t op1 = spirv_code[i + 3];
          uint32_t op2 = spirv_code[i + 4];
          if (op1 < bound && op2 < bound) {
            if ((is_bayer_fraction[op1] || is_dither_const[op1] || is_dither_noise[op1]) && depends_on_frag_coord[op2]) {
              if (res_id < bound && !is_dither_noise[res_id]) {
                is_dither_noise[res_id] = 1;
                has_dither_signature = true;
                changed = true;
              }
            } else if ((is_bayer_fraction[op2] || is_dither_const[op2] || is_dither_noise[op2]) && depends_on_frag_coord[op1]) {
              if (res_id < bound && !is_dither_noise[res_id]) {
                is_dither_noise[res_id] = 1;
                has_dither_signature = true;
                changed = true;
              }
            }
          }
        }
        // 捕获屏幕坐标取模运算：(x + 2*y) % 5 DitherTemporalAA 噪声生成
        else if ((opcode >= 137 && opcode <= 141) && length >= 5) {
          uint32_t op1 = spirv_code[i + 3];
          if (op1 < bound && depends_on_frag_coord[op1]) {
            if (res_id < bound && !is_dither_noise[res_id]) {
              is_dither_noise[res_id] = 1;
              has_dither_signature = true;
              changed = true;
            }
          }
        }

        // 捕获 GLSL.std.450 FAbs (4)
        else if (opcode == SPV_OP_EXT_INST && length >= 6) {
          uint32_t inst = spirv_code[i + 4];
          if (inst == 4 && res_id < bound && !is_fabs_result[res_id]) {
            is_fabs_result[res_id] = 1;
            changed = true;
          }
        }
        // 捕获 OpFSub (131): 10.0f - FAbs 近景深度裁切差值
        else if (opcode == 131 && length >= 5) {
          uint32_t op1 = spirv_code[i + 3];
          uint32_t op2 = spirv_code[i + 4];
          if (op1 < bound && op2 < bound && res_id < bound) {
            if ((is_depth_cutoff_const[op1] && is_fabs_result[op2]) ||
                (is_depth_cutoff_const[op2] && is_fabs_result[op1])) {
              if (!is_depth_cutoff_val[res_id]) {
                is_depth_cutoff_val[res_id] = 1;
                changed = true;
              }
            }
          }
        }
        // 捕获比较指令: 深度裁切判定与相机淡出判定
        else if ((opcode >= 172 && opcode <= 193) && length >= 5) {
          uint32_t op1 = spirv_code[i + 3];
          uint32_t op2 = spirv_code[i + 4];
          if (op1 < bound && op2 < bound && res_id < bound) {
            if (is_depth_cutoff_val[op1] || is_depth_cutoff_val[op2]) {
              if (!is_depth_cutoff_cond[res_id]) {
                is_depth_cutoff_cond[res_id] = 1;
                has_dither_signature = true;
                changed = true;
              }
            }
            if (depends_on_fade[op1] || depends_on_fade[op2]) {
              if (!depends_on_fade[res_id]) {
                depends_on_fade[res_id] = 1;
                has_dither_signature = true;
                changed = true;
              }
            }
          }
        }

        // 传播数据流依赖；贴图采样产物不继承噪声属性
        if (current_func > 0 && res_id > 0 && res_id < bound) {
          uint16_t start_op = (opcode == SPV_OP_EXT_INST) ? 5 : 3;
          uint16_t end_op = length;
          if (opcode == SPV_OP_COMPOSITE_EXTRACT) end_op = 4;

          for (uint16_t k = start_op; k < end_op && (i + k) < word_count; ++k) {
            uint32_t op_val = spirv_code[i + k];
            if (op_val < bound) {
              if (!is_image_sample_opcode(opcode)) {
                if ((is_dither_const[op_val] || is_dither_noise[op_val]) && !is_dither_noise[res_id]) {
                  is_dither_noise[res_id] = 1;
                  changed = true;
                }
              }
              if (depends_on_fade[op_val] && !depends_on_fade[res_id]) {
                depends_on_fade[res_id] = 1;
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

    // 阶段 2 数据流传播结束后，若未检测到任何网点签名，直接安全退出（零修改放行普通材质）
    if (!has_dither_signature && !is_force_mode) {
      if (game_logger::g_dump_enabled && has_any_discard_opcode) {
        game_logger::dump_shader_bundle(orig_copy.data(), orig_copy.size(),
                                        spirv_code, word_count,
                                        shader_hash, 0, 0,
                                        has_any_frag_coord, has_any_sample);
      }
      return;
    }

    // 阶段 2.5: 上游解耦（消除加法/极值中混入贴图采样的 Dither 噪声，保护树叶等 Alpha Cutout）
    uint32_t uncoupled_count = 0;
    size_t scan_i = 5;
    while (scan_i < word_count) {
      uint32_t word = spirv_code[scan_i];
      uint16_t opcode = word & 0xFFFF;
      uint16_t length = (word >> 16) & 0xFFFF;
      if (length == 0 || (scan_i + length) > word_count) break;

      // 针对 OpFAdd (129) 和 OpFSub (131)
      if ((opcode == 129 || opcode == 131) && length >= 5) {
        uint32_t res_id = spirv_code[scan_i + 2];
        uint32_t op1 = spirv_code[scan_i + 3];
        uint32_t op2 = spirv_code[scan_i + 4];
        if (op1 < bound && op2 < bound && res_id < bound) {
          bool op1_sample = depends_on_sample[op1] && !is_dither_noise[op1];
          bool op2_sample = depends_on_sample[op2] && !is_dither_noise[op2];
          bool op1_dither = is_dither_noise[op1];
          bool op2_dither = is_dither_noise[op2];

          uint32_t survivor_op = 0;
          if (op1_sample && op2_dither) {
            survivor_op = op1;
          } else if (op2_sample && op1_dither) {
            survivor_op = op2;
          }

          if (survivor_op > 0) {
            spirv_code[scan_i] = (4 << 16) | SPV_OP_COPY_OBJECT;
            spirv_code[scan_i + 3] = survivor_op;
            for (uint16_t k = 4; k < length && (scan_i + k) < word_count; ++k) {
              spirv_code[scan_i + k] = (1 << 16) | SPV_OP_NOP;
            }
            is_dither_noise[res_id] = 0;
            depends_on_sample[res_id] = 1;
            uncoupled_count++;
          }
        }
      }
      // 针对 GLSL.std.450 NMin (79) / FMin (28)
      else if (opcode == SPV_OP_EXT_INST && length >= 7) {
        uint32_t inst = spirv_code[scan_i + 4];
        if (inst == 79 || inst == 28) {
          uint32_t res_id = spirv_code[scan_i + 2];
          uint32_t op1 = spirv_code[scan_i + 5];
          uint32_t op2 = spirv_code[scan_i + 6];
          if (op1 < bound && op2 < bound && res_id < bound) {
            bool op1_sample = depends_on_sample[op1] && !is_dither_noise[op1];
            bool op2_sample = depends_on_sample[op2] && !is_dither_noise[op2];
            bool op1_dither = is_dither_noise[op1];
            bool op2_dither = is_dither_noise[op2];

            uint32_t survivor_op = 0;
            if (op1_sample && op2_dither) {
              survivor_op = op1;
            } else if (op2_sample && op1_dither) {
              survivor_op = op2;
            }

            if (survivor_op > 0) {
              spirv_code[scan_i] = (4 << 16) | SPV_OP_COPY_OBJECT;
              spirv_code[scan_i + 3] = survivor_op;
              for (uint16_t k = 4; k < length && (scan_i + k) < word_count; ++k) {
                spirv_code[scan_i + k] = (1 << 16) | SPV_OP_NOP;
              }
              is_dither_noise[res_id] = 0;
              depends_on_sample[res_id] = 1;
              uncoupled_count++;
            }
          }
        }
      }
      scan_i += length;
    }

    // 若发生解耦，下游重新传播依赖，清除树木材质条件中被污染的 is_dither_noise
    if (uncoupled_count > 0) {
      for (uint32_t iter = 0; iter < 8; ++iter) {
        scan_i = 5;
        while (scan_i < word_count) {
          uint32_t word = spirv_code[scan_i];
          uint16_t opcode = word & 0xFFFF;
          uint16_t length = (word >> 16) & 0xFFFF;
          if (length == 0 || (scan_i + length) > word_count) break;

          uint32_t res_id = 0;
          if (length >= 3 && opcode != SPV_OP_DECORATE && opcode != SPV_OP_MEMBER_DECORATE &&
              opcode != SPV_OP_STORE && opcode != SPV_OP_BRANCH && opcode != SPV_OP_BRANCH_CONDITIONAL) {
            res_id = (opcode == SPV_OP_VARIABLE && length >= 3) ? spirv_code[scan_i + 2] :
                     (opcode >= 19 && opcode <= 54 && opcode != 59) ? spirv_code[scan_i + 1] : spirv_code[scan_i + 2];
          }

          if (res_id > 0 && res_id < bound) {
            if (opcode == SPV_OP_COPY_OBJECT && length >= 4) {
              uint32_t src = spirv_code[scan_i + 3];
              if (src < bound) {
                is_dither_noise[res_id] = is_dither_noise[src];
                depends_on_sample[res_id] = depends_on_sample[src];
                depends_on_fade[res_id] = depends_on_fade[src];
              }
            } else if (!is_dither_const[res_id]) {
              bool still_has_dither = false;
              uint16_t start_op = (opcode == SPV_OP_EXT_INST) ? 5 : 3;
              for (uint16_t k = start_op; k < length && (scan_i + k) < word_count; ++k) {
                uint32_t op_val = spirv_code[scan_i + k];
                if (op_val < bound && is_dither_noise[op_val]) {
                  still_has_dither = true;
                  break;
                }
              }
              if (!still_has_dither && is_dither_noise[res_id]) {
                is_dither_noise[res_id] = 0;
              }
            }
          }
          scan_i += length;
        }
      }
    }

    // 阶段 3: 执行下游中和（角色纯网点虚化条件改写为 false，彻底消除黑线斜纹；树叶 Cutout 绝对保留）
    // 步骤 3.1: 将纯粹由 Dither 噪声驱动的比较判定直接改写为 false（不进入任何 discard / 写 0 分支）
    scan_i = 5;
    while (scan_i < word_count) {
      uint32_t word = spirv_code[scan_i];
      uint16_t opcode = word & 0xFFFF;
      uint16_t length = (word >> 16) & 0xFFFF;
      if (length == 0 || (scan_i + length) > word_count) break;

      // 浮点比较指令: OpFOrdLessThan(184), OpFOrdLessThanEqual(186), OpFOrdGreaterThan(188), OpFOrdGreaterThanEqual(190)
      // 整数比较指令: OpSLessThan(177), OpULessThan(175)
      if ((opcode >= 172 && opcode <= 193) && length >= 5) {
        uint32_t res_id = spirv_code[scan_i + 2];
        if (res_id < bound) {
          bool is_fade_or_depth = depends_on_fade[res_id] || is_depth_cutoff_cond[res_id];
          bool is_pure_dither = is_dither_noise[res_id] && !depends_on_sample[res_id];
          if (is_fade_or_depth || is_pure_dither) {
            if (false_const_id > 0 && false_const_id < bound) {
              spirv_code[scan_i] = (4 << 16) | SPV_OP_COPY_OBJECT;
              spirv_code[scan_i + 3] = false_const_id;
              for (uint16_t k = 4; k < length && (scan_i + k) < word_count; ++k) {
                spirv_code[scan_i + k] = (1 << 16) | SPV_OP_NOP;
              }
            } else if (zero_float_id > 0 && zero_float_id < bound && opcode == 184) {
              spirv_code[scan_i + 3] = zero_float_id;
              spirv_code[scan_i + 4] = zero_float_id;
            }
          }
        }
      }
      scan_i += length;
    }

    // 步骤 3.2: 消除 demote_cond 调用与纯 Dither 分支内的原生 discard
    size_t i = 5;
    uint32_t current_label = 0;
    uint32_t current_func = 0;
    (void)current_label;
    uint32_t noped_count = 0;
    uint32_t preserved_cutout_count = 0;

    while (i < word_count) {
      uint32_t word = spirv_code[i];
      uint16_t opcode = word & 0xFFFF;
      uint16_t length = (word >> 16) & 0xFFFF;
      if (length == 0 || (i + length) > word_count) break;

      if (opcode == SPV_OP_FUNCTION && length >= 3) {
        current_func = spirv_code[i + 2];
      } else if (opcode == SPV_OP_FUNCTION_END) {
        current_func = 0;
      } else if (opcode == SPV_OP_LABEL && length >= 2) {
        current_label = spirv_code[i + 1];
      }
      // 下游中和：消除 demote_cond 调用
      else if (opcode == SPV_OP_FUNCTION_CALL && length >= 5) {
        uint32_t func_id = spirv_code[i + 3];
        uint32_t arg_id = spirv_code[i + 4];

        if (func_id < bound && is_demote_func[func_id]) {
          bool should_nop = false;
          if (is_force_mode) {
            should_nop = true;
          } else if (arg_id < bound) {
            bool is_fade_or_depth = depends_on_fade[arg_id] || is_depth_cutoff_cond[arg_id];
            bool is_pure_dither = is_dither_noise[arg_id] && !depends_on_sample[arg_id];
            if (is_fade_or_depth || is_pure_dither) {
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
      }
      // 下游中和：消除直接 discard / demote 指令
      else if (is_discard_opcode(opcode)) {
        if (current_func > 0 && current_func < bound && is_demote_func[current_func] && has_parameters[current_func]) {
          // 保留 demote_cond 封装函数体内的原生硬件裁切
        } else {
          bool should_nop = false;
          if (is_force_mode) {
            should_nop = true;
          } else if (current_label < bound) {
            uint32_t cond = cond_for_label[current_label];
            if (cond > 0 && cond < bound) {
              bool is_fade_or_depth = depends_on_fade[cond] || is_depth_cutoff_cond[cond];
              bool is_pure_dither = is_dither_noise[cond] && !depends_on_sample[cond];
              if (is_fade_or_depth || is_pure_dither) {
                should_nop = true;
              }
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
      }

      i += length;
    }

    if ((noped_count + preserved_cutout_count) > 0) {
      const char* category = has_dither_signature ? "角色卡通网点(Toon Bayer)" : (has_any_frag_coord ? "屏幕空间计算" : "特效/材质裁剪");
      game_logger::log_msg("[反虚化驱动层-SilverPalace-VKD3D] 着色器: 0x%08x | 字长: %zu | 消除虚化: %u 处 | 保留正常裁剪: %u 处 | 类别: %s\n",
                           shader_hash, word_count, noped_count, preserved_cutout_count, category);
    }

    if (game_logger::g_dump_enabled && (noped_count > 0 || preserved_cutout_count > 0)) {
      game_logger::dump_shader_bundle(orig_copy.data(), orig_copy.size(),
                                      spirv_code, word_count,
                                      shader_hash, noped_count, preserved_cutout_count,
                                      has_any_frag_coord, has_any_sample);
    }
  }

} // namespace silver_palace_vkd3d
