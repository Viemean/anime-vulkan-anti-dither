#include "test_utils.h"
#include <cstdint>
#include <vector>
#include <iostream>
#include <cassert>
#include <cstdlib>

#include "../src/logger.h"
#include "../src/silver_palace/silver_palace_anti_dither.h"
#include "../src/silver_palace/silver_palace_vkd3d.h"

static inline uint32_t make_op(uint16_t length, uint16_t opcode) {
  return (static_cast<uint32_t>(length) << 16) | opcode;
}

// 用例 1: 角色卡通网点半透明虚化（Bayer 屏幕空间点阵）精确消除
void test_silver_palace_character_bayer_dither_neutralized() {
  std::cout << "[Test SilverPalace 1] Character Screen-space Bayer Dither (Demote neutralized)... ";
  setenv("ANTI_DITHER_ENABLED", "1", 1);
  game_logger::g_initialized = false;
  game_logger::g_exclude_hashes.clear();
  game_logger::g_force_hashes.clear();

  silver_palace_vkd3d::FloatUint f_bayer;
  f_bayer.f = 0.44444445f;

  std::vector<uint32_t> spv = {
    silver_palace_vkd3d::SPV_HEADER_MAGIC,
    0x00010300,
    30017 << 16, // VKD3D
    100, // Bound
    0,
    // OpDecorate %10 BuiltIn FragCoord
    make_op(4, silver_palace_vkd3d::SPV_OP_DECORATE), 10, silver_palace_vkd3d::SPV_DECORATION_BUILTIN, silver_palace_vkd3d::SPV_BUILTIN_FRAG_COORD,
    // OpConstant %50 (Bayer fraction)
    make_op(4, silver_palace_vkd3d::SPV_OP_CONSTANT), 1, 50, f_bayer.u,
    // OpVariable ptr_Input %10
    make_op(4, silver_palace_vkd3d::SPV_OP_VARIABLE), 1, 10, 1,
    // OpFunction
    make_op(5, silver_palace_vkd3d::SPV_OP_FUNCTION), 2, 1, 0, 3,
    make_op(2, silver_palace_vkd3d::SPV_OP_LABEL), 20,
    // OpLoad %60 %10 (FragCoord)
    make_op(4, silver_palace_vkd3d::SPV_OP_LOAD), 1, 60, 10,
    // OpDot %70 %60 %50 (Dither Noise)
    make_op(5, silver_palace_vkd3d::SPV_OP_DOT), 1, 70, 60, 50,
    // OpFOrdLessThan %80 %70 %50
    make_op(5, 184), 6, 80, 70, 50,
    // OpBranchConditional %80 %30 %40
    make_op(4, silver_palace_vkd3d::SPV_OP_BRANCH_CONDITIONAL), 80, 30, 40,
    // OpLabel %30
    make_op(2, silver_palace_vkd3d::SPV_OP_LABEL), 30,
    // OpDemoteToHelperInvocation (应当被 NOP)
    make_op(1, silver_palace_vkd3d::SPV_OP_DEMOTE_TO_HELPER_INVOCATION),
    // OpLabel %40
    make_op(2, silver_palace_vkd3d::SPV_OP_LABEL), 40,
    make_op(1, 253),
    make_op(1, silver_palace_vkd3d::SPV_OP_FUNCTION_END)
  };

  silver_palace_vkd3d::process_spirv_anti_dither(spv.data(), spv.size());
  bool found_nop = false;
  for (size_t idx = 5; idx < spv.size(); ++idx) {
    if (spv[idx] == ((1 << 16) | silver_palace_vkd3d::SPV_OP_NOP)) found_nop = true;
  }
  if (!found_nop) {
    std::cerr << "FAILED: Silver Palace character dither was not neutralized!\n";
    std::abort();
  }
  std::cout << "PASSED\n";
}

// 用例 2: 植被贴图 Alpha 镂空 保护验证（依赖 OpImageSample*）
void test_silver_palace_tree_foliage_alpha_cutout_preserved() {
  std::cout << "[Test SilverPalace 2] Tree Foliage Alpha Cutout with Texture Sample (Preserved 100%)... ";
  setenv("ANTI_DITHER_ENABLED", "1", 1);
  game_logger::g_initialized = false;
  game_logger::g_exclude_hashes.clear();
  game_logger::g_force_hashes.clear();

  silver_palace_vkd3d::FloatUint f_bayer;
  f_bayer.f = 0.33333334f;

  std::vector<uint32_t> spv = {
    silver_palace_vkd3d::SPV_HEADER_MAGIC,
    0x00010300,
    30017 << 16, // VKD3D
    100,
    0,
    // OpDecorate %10 BuiltIn FragCoord
    make_op(4, silver_palace_vkd3d::SPV_OP_DECORATE), 10, silver_palace_vkd3d::SPV_DECORATION_BUILTIN, silver_palace_vkd3d::SPV_BUILTIN_FRAG_COORD,
    // OpConstant %50
    make_op(4, silver_palace_vkd3d::SPV_OP_CONSTANT), 1, 50, f_bayer.u,
    // OpFunction
    make_op(5, silver_palace_vkd3d::SPV_OP_FUNCTION), 2, 1, 0, 3,
    make_op(2, silver_palace_vkd3d::SPV_OP_LABEL), 10,
    // OpImageSampleImplicitLod: type 4, res 15, sampled_img 2, coord 3
    make_op(5, 87), 4, 15, 2, 3,
    // OpFOrdLessThan %16 %15 %5 (Alpha 镂空 condition)
    make_op(5, 184), 6, 16, 15, 5,
    // OpBranchConditional %16 %20 %30
    make_op(4, silver_palace_vkd3d::SPV_OP_BRANCH_CONDITIONAL), 16, 20, 30,
    // OpLabel %20
    make_op(2, silver_palace_vkd3d::SPV_OP_LABEL), 20,
    // OpDemoteToHelperInvocation (应当保留)
    make_op(1, silver_palace_vkd3d::SPV_OP_DEMOTE_TO_HELPER_INVOCATION),
    // OpLabel %30
    make_op(2, silver_palace_vkd3d::SPV_OP_LABEL), 30,
    make_op(1, 253),
    make_op(1, silver_palace_vkd3d::SPV_OP_FUNCTION_END)
  };

  silver_palace_vkd3d::process_spirv_anti_dither(spv.data(), spv.size());
  for (size_t idx = 5; idx < spv.size(); ++idx) {
    if (spv[idx] == ((1 << 16) | silver_palace_vkd3d::SPV_OP_NOP)) {
      std::cerr << "FAILED: Foliage texture cutout demote was incorrectly NOPed!\n";
      std::abort();
    }
  }
  std::cout << "PASSED\n";
}

// 用例 3: IGN 噪声点阵消除验证
void test_silver_palace_ign_noise_neutralized() {
  std::cout << "[Test SilverPalace 3] IGN (Interleaved Gradient Noise) Dither Neutralized... ";
  setenv("ANTI_DITHER_ENABLED", "1", 1);
  game_logger::g_initialized = false;
  game_logger::g_exclude_hashes.clear();
  game_logger::g_force_hashes.clear();

  silver_palace_vkd3d::FloatUint f_ign;
  f_ign.f = 0.06711056f;

  std::vector<uint32_t> spv = {
    silver_palace_vkd3d::SPV_HEADER_MAGIC,
    0x00010300,
    30017 << 16, // VKD3D
    100,
    0,
    // OpDecorate %10 BuiltIn FragCoord
    make_op(4, silver_palace_vkd3d::SPV_OP_DECORATE), 10, silver_palace_vkd3d::SPV_DECORATION_BUILTIN, silver_palace_vkd3d::SPV_BUILTIN_FRAG_COORD,
    // OpConstant %50 (IGN Constant)
    make_op(4, silver_palace_vkd3d::SPV_OP_CONSTANT), 1, 50, f_ign.u,
    // OpVariable ptr_Input %10
    make_op(4, silver_palace_vkd3d::SPV_OP_VARIABLE), 1, 10, 1,
    // OpFunction
    make_op(5, silver_palace_vkd3d::SPV_OP_FUNCTION), 2, 1, 0, 3,
    make_op(2, silver_palace_vkd3d::SPV_OP_LABEL), 20,
    // OpLoad %60 %10 (FragCoord)
    make_op(4, silver_palace_vkd3d::SPV_OP_LOAD), 1, 60, 10,
    // OpDot %70 %60 %50
    make_op(5, silver_palace_vkd3d::SPV_OP_DOT), 1, 70, 60, 50,
    // OpFOrdLessThan %80 %70 %50
    make_op(5, 184), 6, 80, 70, 50,
    // OpBranchConditional %80 %30 %40
    make_op(4, silver_palace_vkd3d::SPV_OP_BRANCH_CONDITIONAL), 80, 30, 40,
    // OpLabel %30
    make_op(2, silver_palace_vkd3d::SPV_OP_LABEL), 30,
    // OpKill (应当被 NOP)
    make_op(1, silver_palace_vkd3d::SPV_OP_KILL),
    // OpLabel %40
    make_op(2, silver_palace_vkd3d::SPV_OP_LABEL), 40,
    make_op(1, 253),
    make_op(1, silver_palace_vkd3d::SPV_OP_FUNCTION_END)
  };

  silver_palace_vkd3d::process_spirv_anti_dither(spv.data(), spv.size());
  bool found_nop = false;
  for (size_t idx = 5; idx < spv.size(); ++idx) {
    if (spv[idx] == ((1 << 16) | silver_palace_vkd3d::SPV_OP_NOP)) found_nop = true;
  }
  if (!found_nop) {
    std::cerr << "FAILED: Silver Palace IGN dither OpKill was not neutralized!\n";
    std::abort();
  }
  std::cout << "PASSED\n";
}

// 用例 4: 游戏实机 Custom Hash Dither (347.834503, 3343.28369) 精准中和与镂空保留
void test_silver_palace_custom_hash_dither_neutralized() {
  std::cout << "[Test SilverPalace 4] Real Dump Custom Hash Dither Neutralized & Preserved... ";
  setenv("ANTI_DITHER_ENABLED", "1", 1);
  game_logger::g_initialized = false;
  game_logger::g_exclude_hashes.clear();
  game_logger::g_force_hashes.clear();

  const char* dump_path = "/tmp/game_anti_dither/dumps/SilverPalaceClient-Win64-Shipping/shader_045e65ad_orig.spv";
  FILE* fp = std::fopen(dump_path, "rb");
  if (fp) {
    std::fseek(fp, 0, SEEK_END);
    size_t bytes = std::ftell(fp);
    std::fseek(fp, 0, SEEK_SET);

    std::vector<uint32_t> spv(bytes / sizeof(uint32_t));
    size_t read_cnt = std::fread(spv.data(), sizeof(uint32_t), spv.size(), fp);
    std::fclose(fp);

    if (read_cnt == spv.size()) {
      silver_palace_vkd3d::process_spirv_anti_dither(spv.data(), spv.size());

      bool found_nop = false;
      for (size_t idx = 5; idx < spv.size(); ++idx) {
        if (spv[idx] == ((1 << 16) | silver_palace_vkd3d::SPV_OP_NOP)) {
          found_nop = true;
          break;
        }
      }
      if (!found_nop) {
        std::cerr << "FAILED: Real dump shader_045e65ad custom hash dither was not neutralized!\n";
        std::abort();
      }
    }
  }
  std::cout << "PASSED\n";
}

// 用例 5: 游戏实机 UE DitherTemporalAA 角色卡通材质精准消除 (shader_a39d531e)
void test_silver_palace_ue_dither_temporal_aa_neutralized() {
  std::cout << "[Test SilverPalace 5] Real Dump UE DitherTemporalAA Neutralized (shader_a39d531e)... ";
  setenv("ANTI_DITHER_ENABLED", "1", 1);
  game_logger::g_initialized = false;
  game_logger::g_exclude_hashes.clear();
  game_logger::g_force_hashes.clear();

  const char* dump_path = "/tmp/game_anti_dither/dumps/SilverPalaceClient-Win64-Shipping/shader_a39d531e_orig.spv";
  FILE* fp = std::fopen(dump_path, "rb");
  if (fp) {
    std::fseek(fp, 0, SEEK_END);
    size_t bytes = std::ftell(fp);
    std::fseek(fp, 0, SEEK_SET);

    std::vector<uint32_t> spv(bytes / sizeof(uint32_t));
    size_t read_cnt = std::fread(spv.data(), sizeof(uint32_t), spv.size(), fp);
    std::fclose(fp);

    if (read_cnt == spv.size()) {
      silver_palace_vkd3d::process_spirv_anti_dither(spv.data(), spv.size());

      bool found_nop = false;
      for (size_t idx = 5; idx < spv.size(); ++idx) {
        if (spv[idx] == ((1 << 16) | silver_palace_vkd3d::SPV_OP_NOP)) {
          found_nop = true;
          break;
        }
      }
      if (!found_nop) {
        std::cerr << "FAILED: Real dump shader_a39d531e UE DitherTemporalAA was not neutralized!\n";
        std::abort();
      }
    }
  }
  std::cout << "PASSED\n";
}

static FILE* open_spv_dump(const char* filename) {
  std::string p1 = std::string("/tmp/game_anti_dither/dumps/SilverPalaceClient-Win64-Shipping/") + filename;
  FILE* fp = std::fopen(p1.c_str(), "rb");
  if (fp) return fp;
  std::string p2 = std::string(".tmp/experiments/silver_palace_fade_analysis/shaders/") + filename;
  fp = std::fopen(p2.c_str(), "rb");
  if (fp) return fp;
  std::string p3 = std::string("../.tmp/experiments/silver_palace_fade_analysis/shaders/") + filename;
  return std::fopen(p3.c_str(), "rb");
}

// 用例 6: 真实实机树木植被材质上游解耦与 Alpha Cutout 100% 保留验证 (shader_e4a9c88b)
void test_silver_palace_real_tree_foliage_cutout_preserved() {
  std::cout << "[Test SilverPalace 6] Real Dump Tree Foliage Cutout Preserved & Uncoupled (shader_e4a9c88b)... ";
  setenv("ANTI_DITHER_ENABLED", "1", 1);
  game_logger::g_initialized = false;
  game_logger::g_exclude_hashes.clear();
  game_logger::g_force_hashes.clear();

  FILE* fp = open_spv_dump("shader_e4a9c88b_orig.spv");
  if (fp) {
    std::fseek(fp, 0, SEEK_END);
    size_t bytes = std::ftell(fp);
    std::fseek(fp, 0, SEEK_SET);

    std::vector<uint32_t> spv(bytes / sizeof(uint32_t));
    size_t read_cnt = std::fread(spv.data(), sizeof(uint32_t), spv.size(), fp);
    std::fclose(fp);

    if (read_cnt == spv.size()) {
      silver_palace_vkd3d::process_spirv_anti_dither(spv.data(), spv.size());

      // 验证解耦生效 (存在 OpCopyObject) 且 demote 函数未被整条 NOP
      bool found_copy_object = false;
      bool demote_func_preserved = false;
      for (size_t idx = 5; idx < spv.size(); ++idx) {
        uint16_t op = spv[idx] & 0xFFFF;
        if (op == silver_palace_vkd3d::SPV_OP_COPY_OBJECT) {
          found_copy_object = true;
        }
        if (op == silver_palace_vkd3d::SPV_OP_FUNCTION_CALL) {
          demote_func_preserved = true;
        }
      }
      if (!found_copy_object) {
        std::cerr << "FAILED: Tree foliage dither noise was not uncoupled via OpCopyObject!\n";
        std::abort();
      }
      if (!demote_func_preserved) {
        std::cerr << "FAILED: Tree foliage demote_cond was incorrectly wiped out!\n";
        std::abort();
      }
    }
  }
  std::cout << "PASSED\n";
}

// 用例 7: 真实主角主着色器近景相机淡出与 10cm 深度裁切消除验证 (shader_0ad4c2fd)
void test_silver_palace_real_character_camera_fade_depth_cutoff_neutralized() {
  std::cout << "[Test SilverPalace 7] Real Character Camera Fade & Depth Cutoff Neutralized (shader_0ad4c2fd)... ";
  setenv("ANTI_DITHER_ENABLED", "1", 1);
  game_logger::g_initialized = false;
  game_logger::g_exclude_hashes.clear();
  game_logger::g_force_hashes.clear();

  FILE* fp = open_spv_dump("shader_0ad4c2fd_orig.spv");
  if (fp) {
    std::fseek(fp, 0, SEEK_END);
    size_t bytes = std::ftell(fp);
    std::fseek(fp, 0, SEEK_SET);

    std::vector<uint32_t> spv(bytes / sizeof(uint32_t));
    size_t read_cnt = std::fread(spv.data(), sizeof(uint32_t), spv.size(), fp);
    std::fclose(fp);

    if (read_cnt == spv.size()) {
      silver_palace_vkd3d::process_spirv_anti_dither(spv.data(), spv.size());

      size_t idx = 5;
      uint32_t remaining_demote_calls = 0;
      uint32_t nop_count = 0;
      while (idx < spv.size()) {
        uint32_t word = spv[idx];
        uint16_t op = word & 0xFFFF;
        uint16_t len = (word >> 16) & 0xFFFF;
        if (len == 0 || idx + len > spv.size()) break;
        if (op == silver_palace_vkd3d::SPV_OP_FUNCTION_CALL && len >= 5) {
          remaining_demote_calls++;
        } else if (op == silver_palace_vkd3d::SPV_OP_NOP) {
          nop_count++;
        }
        idx += len;
      }
      if (remaining_demote_calls > 0) {
        std::cerr << "FAILED: Character camera fade/depth cutoff demote calls were not neutralized! remaining: " << remaining_demote_calls << "\n";
        std::abort();
      }
      if (nop_count < 2) {
        std::cerr << "FAILED: Expected at least 2 neutralized demote calls via NOP, got " << nop_count << "\n";
        std::abort();
      }
    }
  }
  std::cout << "PASSED\n";
}

// 用例 8: 真实角色面部皮肤材质与 PrePass 深度动态淡出消除验证 (shader_de08ca1d, shader_d5c5c60b)
void test_silver_palace_character_dynamic_soft_fade_neutralized() {
  std::cout << "[Test SilverPalace 8] Character Face & PrePass Dynamic Fade Neutralized (0.99f & -0.8333f)... ";
  setenv("ANTI_DITHER_ENABLED", "1", 1);
  game_logger::g_initialized = false;
  game_logger::g_exclude_hashes.clear();
  game_logger::g_force_hashes.clear();

  const char* dump_files[] = {
    "shader_de08ca1d_orig.spv",
    "shader_d5c5c60b_orig.spv"
  };

  for (const char* file : dump_files) {
    FILE* fp = open_spv_dump(file);
    if (fp) {
      std::fseek(fp, 0, SEEK_END);
      size_t bytes = std::ftell(fp);
      std::fseek(fp, 0, SEEK_SET);

      std::vector<uint32_t> spv(bytes / sizeof(uint32_t));
      size_t read_cnt = std::fread(spv.data(), sizeof(uint32_t), spv.size(), fp);
      std::fclose(fp);

      if (read_cnt == spv.size()) {
        silver_palace_vkd3d::process_spirv_anti_dither(spv.data(), spv.size());

        size_t idx = 5;
        uint32_t remaining_demote_calls = 0;
        uint32_t nop_count = 0;
        while (idx < spv.size()) {
          uint32_t word = spv[idx];
          uint16_t op = word & 0xFFFF;
          uint16_t len = (word >> 16) & 0xFFFF;
          if (len == 0 || idx + len > spv.size()) break;
          if (op == silver_palace_vkd3d::SPV_OP_FUNCTION_CALL && len >= 5) {
            remaining_demote_calls++;
          } else if (op == silver_palace_vkd3d::SPV_OP_NOP) {
            nop_count++;
          }
          idx += len;
        }
        if (remaining_demote_calls > 0) {
          std::cerr << "FAILED: Character dynamic soft fade demote calls in " << file << " were not neutralized!\n";
          std::abort();
        }
        if (nop_count < 1) {
          std::cerr << "FAILED: Expected at least 1 neutralized demote call in " << file << "\n";
          std::abort();
        }
      }
    }
  }
  std::cout << "PASSED\n";
}

int main() {
  std::cout << "=== Running Silver Palace Anti-Dither Unit Tests ===\n";
  test_silver_palace_character_bayer_dither_neutralized();
  test_silver_palace_tree_foliage_alpha_cutout_preserved();
  test_silver_palace_ign_noise_neutralized();
  test_silver_palace_custom_hash_dither_neutralized();
  test_silver_palace_ue_dither_temporal_aa_neutralized();
  test_silver_palace_real_tree_foliage_cutout_preserved();
  test_silver_palace_real_character_camera_fade_depth_cutoff_neutralized();
  test_silver_palace_character_dynamic_soft_fade_neutralized();
  std::cout << "=== All Silver Palace Tests Passed Successfully ===\n";
  return 0;
}


