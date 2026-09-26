#pragma once

#include "../logger.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>
#include <unordered_set>
#include <unordered_map>
#include <string_view>

namespace nte_vkd3d {

  constexpr uint32_t SPV_HEADER_MAGIC                       = 0x07230203;
  constexpr uint32_t SPV_OP_NOP                             = 0;
  constexpr uint32_t SPV_OP_NAME                            = 5;
  constexpr uint32_t SPV_OP_EXT_INST                        = 12;
  constexpr uint32_t SPV_OP_ENTRY_POINT                     = 15;
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

  /*
   * NTE (UE 5.5 / VKD3D-Proton) 反虚化驱动模块
   *
   * 处理逻辑：
   * 1. 材质分类定界：
   *    - 角色卡通材质：清除屏幕空间 Bayer 点阵虚化逻辑。
   *    - 大世界与植被材质：保持纹理透明度镂空（Alpha Cutout），避免几何面片实心化。
   * 2. 执行策略：
   *    - 基于物料字长规模与数据流依赖进行范围隔离与噪声剥离。
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
      game_logger::log_msg("[NTE-VKD3D] 着色器 0x%08x 在黑名单中，跳过处理\n", shader_hash);
      return;
    }

    bool is_force_mode = (game_logger::g_force_hashes.count(shader_hash) > 0);

    // 排除角色眼眶阴影与面部贴花遮罩（Eye Socket Decal Mask，保护眼部镂空避免黑色空洞）
    static const std::unordered_set<uint32_t> k_eye_socket_decal_hashes = {
      0x08b19454, 0xd583ec38, 0x717a7d1a, 0x86b5f8ee,
      0x50e17c06, 0x9ed97ff9, 0xe5147a5f, 0xdc232b9a, 0x9d19e033
    };
    if (k_eye_socket_decal_hashes.find(shader_hash) != k_eye_socket_decal_hashes.end() && !is_force_mode) {
      return;
    }

    // 仅处理 VKD3D 转译的游戏着色器 (Tool ID 30017)，排除内部元着色器
    uint32_t generator = spirv_code[2];
    uint32_t tool_id = (generator >> 16);
    if (tool_id != 30017 && !is_force_mode) {
      return;
    }

    // 角色卡通材质字长分布于 [1100, 1500] DW 区间；大世界环境与植被材质字长通常 >= 1800 DW
    if ((word_count < 1100 || word_count > 1500) && !is_force_mode) {
      return;
    }

    // 着色器不含 discard/demote 指令时跳过
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
    std::vector<uint32_t> cond_for_label(bound, 0);

    bool has_any_frag_coord = false;
    bool has_any_sample = false;
    (void)has_any_frag_coord;
    (void)has_any_sample;
    bool has_dither_signature = false;
    bool is_niagara_or_particle = false;
    bool is_foliage_or_vegetation = false;

    std::vector<uint8_t> is_bayer_fraction(bound, 0);

    // Pass 1: 扫描 FragCoord、Bayer/IGN 常量及材质标记
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
          if (name.find("centroid") != std::string_view::npos ||
              name.find("Centroid") != std::string_view::npos) {
            is_foliage_or_vegetation = true;
          }
        } else if (opcode == SPV_OP_DECORATE && length >= 3) {
          uint32_t target_id = spirv_code[i + 1];
          uint32_t decoration = spirv_code[i + 2];
          if (decoration == 10 /* SpvDecorationCentroid */) {
            is_foliage_or_vegetation = true;
          }
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

          // 3x3 Bayer 矩阵分数值特征
          if ((fu.f >= 0.1110f && fu.f <= 0.1112f) ||
              (fu.f >= 0.2221f && fu.f <= 0.2223f) ||
              (fu.f >= 0.3332f && fu.f <= 0.3334f) ||
              (fu.f >= 0.4443f && fu.f <= 0.4445f) ||
              (fu.f >= 0.5554f && fu.f <= 0.5556f) ||
              (fu.f >= 0.7776f && fu.f <= 0.7779f) ||
              (fu.f >= 0.8887f && fu.f <= 0.8890f)) {
            if (res_id < bound) is_bayer_fraction[res_id] = 1;
          }
          // IGN 与 UE 5.5 Stipple 噪声常量特征
          else if ((fu.f >= 0.06711f && fu.f <= 0.06712f) ||
                   (fu.f >= 0.005837f && fu.f <= 0.005838f) ||
                   (fu.f >= 52.982f && fu.f <= 52.983f) ||
                   (fu.f >= 0.015624f && fu.f <= 0.015626f) ||
                   (fu.f >= 0.16664f && fu.f <= 0.16667f)) {
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

    // 若未匹配到任何 Bayer 点阵或 IGN 噪声特征，直接跳过，避免误杀眼部/贴花等正常遮罩
    if (!has_dither_signature && !is_force_mode) {
      return;
    }

    // 排除 Niagara 与粒子特效
    if (is_niagara_or_particle && !is_force_mode) {
      return;
    }

    // 排除包含 Centroid 修饰的大世界植被着色器
    if (is_foliage_or_vegetation && !is_force_mode) {
      return;
    }

    // Pass 2: 追踪 FragCoord、贴图采样与 Dither 噪声传播
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

        // 结合 FragCoord 与 Dither 常量的点积运算标记为噪声源
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

        // 传播数据流依赖；贴图采样产物不继承噪声属性
        if (current_func > 0 && res_id > 0 && res_id < bound) {
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

    // Pass 3: 执行上游解耦与下游中和
    size_t i = 5;
    uint32_t current_label = 0;
    uint32_t noped_count = 0;
    uint32_t preserved_cutout_count = 0;

    while (i < word_count) {
      uint32_t word = spirv_code[i];
      uint16_t opcode = word & 0xFFFF;
      uint16_t length = (word >> 16) & 0xFFFF;
      if (length == 0 || (i + length) > word_count) break;

      if (opcode == SPV_OP_LABEL && length >= 2) {
        current_label = spirv_code[i + 1];
      }
      // 上游分支解耦：若条件依赖 Dither 且不依赖贴图采样，将 0.0 替换为 1.0
      else if (opcode == 169 /* OpSelect */ && length >= 6) {
        uint32_t cond = spirv_code[i + 3];
        if (cond < bound && is_dither_noise[cond] && !depends_on_sample[cond]) {
          spirv_code[i + 4] = spirv_code[i + 5];
          noped_count++;
        }
      }
      // 上游极值解耦：剥离 NMin/FMin 中的 Dither 噪声操作数
      else if (opcode == SPV_OP_EXT_INST && length >= 7) {
        uint32_t inst = spirv_code[i + 4];
        if (inst == 37 || inst == 79) { // GLSL.std.450 FMin (37) 或 NMin (79)
          uint32_t op1 = spirv_code[i + 5];
          uint32_t op2 = spirv_code[i + 6];
          if (op2 < bound && is_dither_noise[op2] && !is_dither_noise[op1]) {
            spirv_code[i + 6] = op1;
            noped_count++;
          } else if (op1 < bound && is_dither_noise[op1] && !is_dither_noise[op2]) {
            spirv_code[i + 5] = op2;
            noped_count++;
          }
        }
      }
      // 下游中和：消除依赖 Dither 噪声的 demote 函数调用
      else if (opcode == SPV_OP_FUNCTION_CALL && length >= 5) {
        uint32_t func_id = spirv_code[i + 3];
        uint32_t arg_id = spirv_code[i + 4];

        if (func_id < bound && is_demote_func[func_id]) {
          bool should_nop = false;

          if (is_force_mode) {
            should_nop = true;
          } else if (arg_id < bound) {
            // 保护带贴图采样的 Alpha 镂空：若条件依赖纹理采样，绝不能 NOP demote，否则会导致眼眶/布料/树叶镂空失效变成黑方块
            if (is_dither_noise[arg_id] && !depends_on_sample[arg_id]) {
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
      // 下游中和：消除依赖 Dither 噪声的直接 discard 指令
      else if (is_discard_opcode(opcode)) {
        uint32_t cond = (current_label < bound) ? cond_for_label[current_label] : 0;
        bool should_nop = false;

        if (is_force_mode) {
          should_nop = true;
        } else if (cond > 0 && cond < bound) {
          // 保护带贴图采样的 Alpha 镂空
          if (is_dither_noise[cond] && !depends_on_sample[cond]) {
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

      i += length;
    }

    if ((noped_count + preserved_cutout_count) > 0) {
      const char* category = has_dither_signature ? "角色卡通网点(Toon Bayer)" : (has_any_frag_coord ? "屏幕空间计算" : "特效/材质裁剪");
      game_logger::log_msg("[反虚化驱动层-NTE-VKD3D] 着色器: 0x%08x | 字长: %zu | 消除虚化: %u 处 | 保留正常裁剪: %u 处 | 类别: %s\n",
                           shader_hash, word_count, noped_count, preserved_cutout_count, category);
    }

    if (game_logger::g_dump_enabled && (noped_count > 0 || preserved_cutout_count > 0)) {
      game_logger::dump_shader_bundle(orig_copy.data(), orig_copy.size(),
                                      spirv_code, word_count,
                                      shader_hash, noped_count, preserved_cutout_count,
                                      has_any_frag_coord, has_any_sample);
    }
  }

} // namespace nte_vkd3d
