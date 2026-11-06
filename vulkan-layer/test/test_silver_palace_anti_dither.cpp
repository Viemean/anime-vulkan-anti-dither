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
    make_op(4, silver_palace_vkd3d::SPV_OP_DECORATE), 10, silver_palace_vkd3d::SPV_DECORATION_BUILTIN, silver_palace_vkd3d::SPV_BUILTIN_FRAG_COORD,
    make_op(4, silver_palace_vkd3d::SPV_OP_CONSTANT), 1, 50, f_bayer.u,
    make_op(4, silver_palace_vkd3d::SPV_OP_VARIABLE), 1, 10, 1,
    make_op(5, silver_palace_vkd3d::SPV_OP_FUNCTION), 2, 1, 0, 3,
    make_op(2, silver_palace_vkd3d::SPV_OP_LABEL), 20,
    make_op(4, silver_palace_vkd3d::SPV_OP_LOAD), 1, 60, 10,
    make_op(5, silver_palace_vkd3d::SPV_OP_DOT), 1, 70, 60, 50,
    make_op(5, 184), 6, 80, 70, 50,
    make_op(4, silver_palace_vkd3d::SPV_OP_BRANCH_CONDITIONAL), 80, 30, 40,
    make_op(2, silver_palace_vkd3d::SPV_OP_LABEL), 30,
    make_op(1, silver_palace_vkd3d::SPV_OP_DEMOTE_TO_HELPER_INVOCATION),
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
    make_op(4, silver_palace_vkd3d::SPV_OP_DECORATE), 10, silver_palace_vkd3d::SPV_DECORATION_BUILTIN, silver_palace_vkd3d::SPV_BUILTIN_FRAG_COORD,
    make_op(4, silver_palace_vkd3d::SPV_OP_CONSTANT), 1, 50, f_bayer.u,
    make_op(5, silver_palace_vkd3d::SPV_OP_FUNCTION), 2, 1, 0, 3,
    make_op(2, silver_palace_vkd3d::SPV_OP_LABEL), 10,
    make_op(5, 87), 4, 15, 2, 3,
    make_op(5, 184), 6, 16, 15, 5,
    make_op(4, silver_palace_vkd3d::SPV_OP_BRANCH_CONDITIONAL), 16, 20, 30,
    make_op(2, silver_palace_vkd3d::SPV_OP_LABEL), 20,
    make_op(1, silver_palace_vkd3d::SPV_OP_DEMOTE_TO_HELPER_INVOCATION),
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
    make_op(4, silver_palace_vkd3d::SPV_OP_DECORATE), 10, silver_palace_vkd3d::SPV_DECORATION_BUILTIN, silver_palace_vkd3d::SPV_BUILTIN_FRAG_COORD,
    make_op(4, silver_palace_vkd3d::SPV_OP_CONSTANT), 1, 50, f_ign.u,
    make_op(4, silver_palace_vkd3d::SPV_OP_VARIABLE), 1, 10, 1,
    make_op(5, silver_palace_vkd3d::SPV_OP_FUNCTION), 2, 1, 0, 3,
    make_op(2, silver_palace_vkd3d::SPV_OP_LABEL), 20,
    make_op(4, silver_palace_vkd3d::SPV_OP_LOAD), 1, 60, 10,
    make_op(5, silver_palace_vkd3d::SPV_OP_DOT), 1, 70, 60, 50,
    make_op(5, 184), 6, 80, 70, 50,
    make_op(4, silver_palace_vkd3d::SPV_OP_BRANCH_CONDITIONAL), 80, 30, 40,
    make_op(2, silver_palace_vkd3d::SPV_OP_LABEL), 30,
    make_op(1, silver_palace_vkd3d::SPV_OP_KILL),
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

// 用例 4: 定制散斑 Hash 着色器网点中和 (347.834503, 3343.28369)
void test_silver_palace_custom_hash_dither_neutralized() {
  std::cout << "[Test SilverPalace 4] Custom Hash Dither (347.8345f) Neutralized... ";
  setenv("ANTI_DITHER_ENABLED", "1", 1);
  game_logger::g_initialized = false;
  game_logger::g_exclude_hashes.clear();
  game_logger::g_force_hashes.clear();

  silver_palace_vkd3d::FloatUint f_hash;
  f_hash.f = 347.834503f;

  std::vector<uint32_t> spv = {
    silver_palace_vkd3d::SPV_HEADER_MAGIC,
    0x00010300,
    30017 << 16,
    100,
    0,
    make_op(4, silver_palace_vkd3d::SPV_OP_DECORATE), 10, silver_palace_vkd3d::SPV_DECORATION_BUILTIN, silver_palace_vkd3d::SPV_BUILTIN_FRAG_COORD,
    make_op(4, silver_palace_vkd3d::SPV_OP_CONSTANT), 1, 50, f_hash.u,
    make_op(4, silver_palace_vkd3d::SPV_OP_VARIABLE), 1, 10, 1,
    make_op(5, silver_palace_vkd3d::SPV_OP_FUNCTION), 2, 1, 0, 3,
    make_op(2, silver_palace_vkd3d::SPV_OP_LABEL), 20,
    make_op(4, silver_palace_vkd3d::SPV_OP_LOAD), 1, 60, 10,
    make_op(5, 133 /* OpFMul */), 1, 70, 60, 50,
    make_op(5, 184), 6, 80, 70, 50,
    make_op(4, silver_palace_vkd3d::SPV_OP_BRANCH_CONDITIONAL), 80, 30, 40,
    make_op(2, silver_palace_vkd3d::SPV_OP_LABEL), 30,
    make_op(1, silver_palace_vkd3d::SPV_OP_DEMOTE_TO_HELPER_INVOCATION),
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
    std::cerr << "FAILED: Custom hash dither was not neutralized!\n";
    std::abort();
  }
  std::cout << "PASSED\n";
}

// 用例 5: UE DitherTemporalAA 核心常数消除 (0.166649997f)
void test_silver_palace_ue_dither_temporal_aa_neutralized() {
  std::cout << "[Test SilverPalace 5] UE DitherTemporalAA (0.16665f) Neutralized... ";
  setenv("ANTI_DITHER_ENABLED", "1", 1);
  game_logger::g_initialized = false;
  game_logger::g_exclude_hashes.clear();
  game_logger::g_force_hashes.clear();

  silver_palace_vkd3d::FloatUint f_taa;
  f_taa.u = 0x3e2aa64c; // 0.166649997f

  std::vector<uint32_t> spv = {
    silver_palace_vkd3d::SPV_HEADER_MAGIC,
    0x00010300,
    30017 << 16,
    100,
    0,
    make_op(4, silver_palace_vkd3d::SPV_OP_DECORATE), 10, silver_palace_vkd3d::SPV_DECORATION_BUILTIN, silver_palace_vkd3d::SPV_BUILTIN_FRAG_COORD,
    make_op(4, silver_palace_vkd3d::SPV_OP_CONSTANT), 1, 50, f_taa.u,
    make_op(4, silver_palace_vkd3d::SPV_OP_VARIABLE), 1, 10, 1,
    make_op(5, silver_palace_vkd3d::SPV_OP_FUNCTION), 2, 1, 0, 3,
    make_op(2, silver_palace_vkd3d::SPV_OP_LABEL), 20,
    make_op(4, silver_palace_vkd3d::SPV_OP_LOAD), 1, 60, 10,
    make_op(5, 133 /* OpFMul */), 1, 70, 60, 50,
    make_op(5, 184), 6, 80, 70, 50,
    make_op(4, silver_palace_vkd3d::SPV_OP_BRANCH_CONDITIONAL), 80, 30, 40,
    make_op(2, silver_palace_vkd3d::SPV_OP_LABEL), 30,
    make_op(1, silver_palace_vkd3d::SPV_OP_DEMOTE_TO_HELPER_INVOCATION),
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
    std::cerr << "FAILED: UE DitherTemporalAA was not neutralized!\n";
    std::abort();
  }
  std::cout << "PASSED\n";
}

// 用例 6: 树木植被贴图采样与网点噪声上游解耦 (OpFMul -> OpCopyObject) 且保留镂空
void test_silver_palace_real_tree_foliage_cutout_preserved() {
  std::cout << "[Test SilverPalace 6] Tree Foliage Upstream Uncoupling (OpCopyObject) & Cutout Preserved... ";
  setenv("ANTI_DITHER_ENABLED", "1", 1);
  game_logger::g_initialized = false;
  game_logger::g_exclude_hashes.clear();
  game_logger::g_force_hashes.clear();

  silver_palace_vkd3d::FloatUint f_bayer;
  f_bayer.f = 0.44444445f;

  std::vector<uint32_t> spv = {
    silver_palace_vkd3d::SPV_HEADER_MAGIC,
    0x00010300,
    30017 << 16,
    100,
    0,
    make_op(4, silver_palace_vkd3d::SPV_OP_DECORATE), 10, silver_palace_vkd3d::SPV_DECORATION_BUILTIN, silver_palace_vkd3d::SPV_BUILTIN_FRAG_COORD,
    make_op(4, silver_palace_vkd3d::SPV_OP_CONSTANT), 1, 50, f_bayer.u,
    make_op(4, silver_palace_vkd3d::SPV_OP_VARIABLE), 1, 10, 1,
    make_op(5, silver_palace_vkd3d::SPV_OP_FUNCTION), 2, 1, 0, 3,
    make_op(2, silver_palace_vkd3d::SPV_OP_LABEL), 10,
    // OpImageSampleImplicitLod: res 15 (Texture Alpha)
    make_op(5, 87), 4, 15, 2, 3,
    // OpLoad FragCoord %60
    make_op(4, silver_palace_vkd3d::SPV_OP_LOAD), 1, 60, 10,
    // OpDot %70 %60 %50 (Dither Noise %70)
    make_op(5, silver_palace_vkd3d::SPV_OP_DOT), 1, 70, 60, 50,
    // OpFSub %80 %15 %70 (TextureAlpha mixed with DitherNoise -> 必须被解耦为 OpCopyObject %80 %15)
    make_op(5, 131 /* OpFSub */), 1, 80, 15, 70,
    // OpFOrdLessThan %90 %80 %50 (Cutout condition)
    make_op(5, 184), 6, 90, 80, 50,
    make_op(4, silver_palace_vkd3d::SPV_OP_BRANCH_CONDITIONAL), 90, 20, 30,
    make_op(2, silver_palace_vkd3d::SPV_OP_LABEL), 20,
    // OpDemoteToHelperInvocation (植被正常镂空，解耦后必须 100% 保留)
    make_op(1, silver_palace_vkd3d::SPV_OP_DEMOTE_TO_HELPER_INVOCATION),
    make_op(2, silver_palace_vkd3d::SPV_OP_LABEL), 30,
    make_op(1, 253),
    make_op(1, silver_palace_vkd3d::SPV_OP_FUNCTION_END)
  };

  silver_palace_vkd3d::process_spirv_anti_dither(spv.data(), spv.size());

  bool found_copy_object = false;
  bool demote_preserved = false;
  for (size_t idx = 5; idx < spv.size(); ++idx) {
    uint16_t op = spv[idx] & 0xFFFF;
    if (op == silver_palace_vkd3d::SPV_OP_COPY_OBJECT) {
      found_copy_object = true;
    }
    if (op == silver_palace_vkd3d::SPV_OP_DEMOTE_TO_HELPER_INVOCATION) {
      demote_preserved = true;
    }
  }
  if (!found_copy_object) {
    std::cerr << "FAILED: Tree foliage dither noise was not uncoupled via OpCopyObject!\n";
    std::abort();
  }
  if (!demote_preserved) {
    std::cerr << "FAILED: Tree foliage demote was incorrectly wiped out!\n";
    std::abort();
  }
  std::cout << "PASSED\n";
}

// 用例 7: 近景 10.0f 深度截断与 demote 操作数清零中和
void test_silver_palace_real_character_camera_fade_depth_cutoff_neutralized() {
  std::cout << "[Test SilverPalace 7] Near Depth Cutoff (10.0f) Neutralized... ";
  setenv("ANTI_DITHER_ENABLED", "1", 1);
  game_logger::g_initialized = false;
  game_logger::g_exclude_hashes.clear();
  game_logger::g_force_hashes.clear();

  silver_palace_vkd3d::FloatUint f_zero;
  f_zero.f = 0.0f;
  silver_palace_vkd3d::FloatUint f_10;
  f_10.f = 10.0f;

  std::vector<uint32_t> spv = {
    silver_palace_vkd3d::SPV_HEADER_MAGIC,
    0x00010300,
    30017 << 16,
    100,
    0,
    make_op(4, silver_palace_vkd3d::SPV_OP_DECORATE), 10, silver_palace_vkd3d::SPV_DECORATION_BUILTIN, silver_palace_vkd3d::SPV_BUILTIN_FRAG_COORD,
    make_op(4, silver_palace_vkd3d::SPV_OP_CONSTANT), 1, 50, f_zero.u, // %50 zero float
    make_op(4, silver_palace_vkd3d::SPV_OP_CONSTANT), 1, 51, f_10.u,   // %51 10.0f cutoff const
    make_op(4, silver_palace_vkd3d::SPV_OP_VARIABLE), 1, 10, 1,
    make_op(5, silver_palace_vkd3d::SPV_OP_FUNCTION), 2, 1, 0, 3,
    make_op(2, silver_palace_vkd3d::SPV_OP_LABEL), 20,
    // GLSL.std.450 (12) FAbs (4) %60 %10 -> is_fabs_result[%60] = 1
    make_op(6, 12), 1, 60, 200, 4, 10,
    // OpFSub (131) %65 %51 %60 -> is_depth_cutoff_val[%65] = 1
    make_op(5, 131), 1, 65, 51, 60,
    // OpFOrdLessThan %70 %65 %50 -> is_depth_cutoff_cond[%70] = 1
    make_op(5, 184), 6, 70, 65, 50,
    // OpBranchConditional %70 %30 %40
    make_op(4, silver_palace_vkd3d::SPV_OP_BRANCH_CONDITIONAL), 70, 30, 40,
    make_op(2, silver_palace_vkd3d::SPV_OP_LABEL), 30,
    // OpDemoteToHelperInvocation (应当被 NOP)
    make_op(1, silver_palace_vkd3d::SPV_OP_DEMOTE_TO_HELPER_INVOCATION),
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
    std::cerr << "FAILED: Depth cutoff demote was not neutralized!\n";
    std::abort();
  }
  std::cout << "PASSED\n";
}

// 用例 8: 相机近身软淡出 (0.99f, 1.5f, -0.8333f) 消除验证
void test_silver_palace_character_dynamic_soft_fade_neutralized() {
  std::cout << "[Test SilverPalace 8] Character Dynamic Soft Fade (0.99f & -0.8333f) Neutralized... ";
  setenv("ANTI_DITHER_ENABLED", "1", 1);
  game_logger::g_initialized = false;
  game_logger::g_exclude_hashes.clear();
  game_logger::g_force_hashes.clear();

  silver_palace_vkd3d::FloatUint f_099, f_15, f_bias;
  f_099.u = 0x3f7d70a4; // 0.99000001f
  f_15.u  = 0x3fc00000; // 1.5f
  f_bias.u = 0xbf555326; // -0.8333f

  std::vector<uint32_t> spv = {
    silver_palace_vkd3d::SPV_HEADER_MAGIC,
    0x00010300,
    30017 << 16,
    100,
    0,
    make_op(4, silver_palace_vkd3d::SPV_OP_DECORATE), 10, silver_palace_vkd3d::SPV_DECORATION_BUILTIN, silver_palace_vkd3d::SPV_BUILTIN_FRAG_COORD,
    make_op(4, silver_palace_vkd3d::SPV_OP_CONSTANT), 1, 50, f_099.u,
    make_op(4, silver_palace_vkd3d::SPV_OP_CONSTANT), 1, 51, f_15.u,
    make_op(4, silver_palace_vkd3d::SPV_OP_CONSTANT), 1, 52, f_bias.u,
    make_op(4, silver_palace_vkd3d::SPV_OP_VARIABLE), 1, 10, 1,
    make_op(5, silver_palace_vkd3d::SPV_OP_FUNCTION), 2, 1, 0, 3,
    make_op(2, silver_palace_vkd3d::SPV_OP_LABEL), 20,
    make_op(4, silver_palace_vkd3d::SPV_OP_LOAD), 1, 60, 10,
    // OpFAdd %70 %60 %52 (%52 is bias -> depends_on_fade[%70] = 1)
    make_op(5, 129), 1, 70, 60, 52,
    // OpFMul %75 %70 %51 (%51 is 1.5f -> depends_on_fade[%75] = 1)
    make_op(5, 133), 1, 75, 70, 51,
    // OpFOrdLessThan %80 %75 %50 (%50 is 0.99f -> depends_on_fade[%80] = 1)
    make_op(5, 184), 6, 80, 75, 50,
    // OpBranchConditional %80 %30 %40
    make_op(4, silver_palace_vkd3d::SPV_OP_BRANCH_CONDITIONAL), 80, 30, 40,
    make_op(2, silver_palace_vkd3d::SPV_OP_LABEL), 30,
    // OpDemoteToHelperInvocation (应当被 NOP)
    make_op(1, silver_palace_vkd3d::SPV_OP_DEMOTE_TO_HELPER_INVOCATION),
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
    std::cerr << "FAILED: Character soft fade demote was not neutralized!\n";
    std::abort();
  }
  std::cout << "PASSED\n";
}

// 用例 9: 次世代超大卡通着色器规模验证 (65,000 words 内存动态测试)
void test_silver_palace_rdc_capture_large_toon_and_prepass_neutralized() {
  std::cout << "[Test SilverPalace 9] Large Toon (65k words limit放宽) Neutralized in Memory... ";
  setenv("ANTI_DITHER_ENABLED", "1", 1);
  game_logger::g_initialized = false;
  game_logger::g_exclude_hashes.clear();
  game_logger::g_force_hashes.clear();

  silver_palace_vkd3d::FloatUint f_bayer;
  f_bayer.f = 0.44444445f;

  std::vector<uint32_t> spv = {
    silver_palace_vkd3d::SPV_HEADER_MAGIC,
    0x00010300,
    30017 << 16,
    100,
    0,
    make_op(4, silver_palace_vkd3d::SPV_OP_DECORATE), 10, silver_palace_vkd3d::SPV_DECORATION_BUILTIN, silver_palace_vkd3d::SPV_BUILTIN_FRAG_COORD,
    make_op(4, silver_palace_vkd3d::SPV_OP_CONSTANT), 1, 50, f_bayer.u,
    make_op(4, silver_palace_vkd3d::SPV_OP_VARIABLE), 1, 10, 1,
    make_op(5, silver_palace_vkd3d::SPV_OP_FUNCTION), 2, 1, 0, 3,
    make_op(2, silver_palace_vkd3d::SPV_OP_LABEL), 20,
    make_op(4, silver_palace_vkd3d::SPV_OP_LOAD), 1, 60, 10,
    // OpDot %70 %60 %50 (Dither Noise)
    make_op(5, silver_palace_vkd3d::SPV_OP_DOT), 1, 70, 60, 50,
    make_op(5, 184), 6, 80, 70, 50,
    make_op(4, silver_palace_vkd3d::SPV_OP_BRANCH_CONDITIONAL), 80, 30, 40,
    make_op(2, silver_palace_vkd3d::SPV_OP_LABEL), 30,
    make_op(1, silver_palace_vkd3d::SPV_OP_DEMOTE_TO_HELPER_INVOCATION),
    make_op(2, silver_palace_vkd3d::SPV_OP_LABEL), 40,
    make_op(1, 253),
    make_op(1, silver_palace_vkd3d::SPV_OP_FUNCTION_END)
  };

  // 内存动态填充至 65,000 words，验证驱动层 120,000 words 门限不会过滤它
  size_t target_size = 65000;
  spv.resize(target_size, ((1 << 16) | silver_palace_vkd3d::SPV_OP_NOP));

  silver_palace_vkd3d::process_spirv_anti_dither(spv.data(), spv.size());

  bool demote_neutralized = (spv[44] == ((1 << 16) | silver_palace_vkd3d::SPV_OP_NOP));
  for (size_t idx = 5; idx < spv.size(); ++idx) {
    if ((spv[idx] & 0xFFFF) == silver_palace_vkd3d::SPV_OP_DEMOTE_TO_HELPER_INVOCATION) {
      demote_neutralized = false;
      break;
    }
  }
  if (!demote_neutralized) {
    std::cerr << "FAILED: Large 65k words shader was erroneously skipped by driver threshold!\n";
    std::abort();
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
  test_silver_palace_rdc_capture_large_toon_and_prepass_neutralized();
  std::cout << "=== All Silver Palace Tests Passed Successfully ===\n";
  return 0;
}
