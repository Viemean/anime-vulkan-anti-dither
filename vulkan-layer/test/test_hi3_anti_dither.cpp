#include <cstdint>
#include <vector>
#include <iostream>
#include <cassert>
#include <cstdlib>

#include "../src/logger.h"
#include "../src/HI3rd/hi3_anti_dither.h"
#include "../src/HI3rd/hi3_dxvk.h"

static inline uint32_t make_op(uint16_t length, uint16_t opcode) {
  return (static_cast<uint32_t>(length) << 16) | opcode;
}

// 用例 1: 崩坏3管线着色器阶段分发判定 (Vertex vs Fragment)
void test_hi3_pipeline_stage_dispatch() {
  std::cout << "[Test HI3 1] Stage detection (Fragment vs Vertex vs Compute)... ";

  // Fragment Shader
  std::vector<uint32_t> ps_spv = {
    hi3_dxvk::SPV_HEADER_MAGIC,
    0x00010300,
    0, // Generator (DXVK)
    50,
    0,
    make_op(4, 15 /* OpEntryPoint */), 4, 4, 0x6e69616d // ExecutionModel = 4 (Fragment)
  };

  auto ps_stage = hi3_layer::detect_shader_stage(ps_spv.data(), ps_spv.size());
  (void)ps_stage;
  assert(ps_stage == hi3_layer::ShaderStage::Fragment);

  // Vertex Shader
  std::vector<uint32_t> vs_spv = {
    hi3_dxvk::SPV_HEADER_MAGIC,
    0x00010300,
    0,
    50,
    0,
    make_op(4, 15 /* OpEntryPoint */), 0, 4, 0x6e69616d // ExecutionModel = 0 (Vertex)
  };

  auto vs_stage = hi3_layer::detect_shader_stage(vs_spv.data(), vs_spv.size());
  (void)vs_stage;
  assert(vs_stage == hi3_layer::ShaderStage::Vertex);

  std::cout << "PASSED\n";
}

// 用例 2: 崩坏3角色 4x4 Bayer 网点虚化精准中和 (OpKill -> OpNop)
void test_hi3_bayer_dither_kill_neutralized() {
  std::cout << "[Test HI3 2] Character 4x4 Bayer Dither (OpKill neutralized)... ";

  setenv("ANTI_DITHER_ENABLED", "1", 1);
  game_logger::g_initialized = false;
  game_logger::g_exclude_hashes.clear();
  game_logger::g_force_hashes.clear();

  union {
    float f;
    uint32_t u;
  } f17;
  f17.f = 17.0f;

  std::vector<uint32_t> spv = {
    hi3_dxvk::SPV_HEADER_MAGIC,
    0x00010300,
    0, // Generator (DXVK)
    100, // Bound
    0,
    // OpDecorate %10 BuiltIn FragCoord
    make_op(4, 71 /* OpDecorate */), 10, hi3_dxvk::SPV_DECORATION_BUILTIN, hi3_dxvk::SPV_BUILTIN_FRAG_COORD,
    // OpConstant %50 (17.0f Bayer normalization factor)
    make_op(4, 43 /* OpConstant */), 1, 50, f17.u,
    // OpFunction
    make_op(5, 54 /* OpFunction */), 2, 1, 0, 3,
    make_op(2, 248 /* OpLabel */), 20,
    // OpFMul %60 %50 %10 (Bayer noise calculation)
    make_op(5, 133 /* OpFMul */), 1, 60, 50, 10,
    // OpFOrdLessThan %70 %60 %10
    make_op(5, 184 /* OpFOrdLessThan */), 6, 70, 60, 10,
    // OpBranchConditional %70 %30 %40
    make_op(4, 250 /* OpBranchConditional */), 70, 30, 40,
    // OpLabel %30
    make_op(2, 248 /* OpLabel */), 30,
    // OpKill (应当被中和为 OpNop)
    make_op(1, hi3_dxvk::SPV_OP_KILL),
    // OpLabel %40
    make_op(2, 248 /* OpLabel */), 40,
    make_op(1, 253 /* OpReturn */),
    make_op(1, 56 /* OpFunctionEnd */)
  };

  hi3_dxvk::process_spirv_anti_dither(spv.data(), spv.size());

  bool found_kill = false;
  bool found_nop = false;
  for (uint32_t w : spv) {
    if ((w & 0xFFFF) == hi3_dxvk::SPV_OP_KILL) found_kill = true;
    if ((w & 0xFFFF) == hi3_dxvk::SPV_OP_NOP) found_nop = true;
  }

  if (found_kill || !found_nop) {
    std::cerr << "FAILED: HI3 character dither OpKill was not converted to OpNop!\n";
    std::abort();
  }

  std::cout << "PASSED\n";
}

// 用例 3: 崩坏3场景植被与材质镂空 Alpha Cutout 100% 保护
void test_hi3_tree_foliage_alpha_cutout_preserved() {
  std::cout << "[Test HI3 3] Scene Alpha Cutout with Texture Sample (Preserved 100%)... ";

  setenv("ANTI_DITHER_ENABLED", "1", 1);
  game_logger::g_initialized = false;
  game_logger::g_exclude_hashes.clear();
  game_logger::g_force_hashes.clear();

  union {
    float f;
    uint32_t u;
  } f17;
  f17.f = 17.0f;

  std::vector<uint32_t> spv = {
    hi3_dxvk::SPV_HEADER_MAGIC,
    0x00010300,
    0,
    100,
    0,
    make_op(4, 71 /* OpDecorate */), 10, hi3_dxvk::SPV_DECORATION_BUILTIN, hi3_dxvk::SPV_BUILTIN_FRAG_COORD,
    make_op(4, 43 /* OpConstant */), 1, 50, f17.u,
    make_op(5, 54 /* OpFunction */), 2, 1, 0, 3,
    make_op(2, 248 /* OpLabel */), 10,
    // OpImageSampleImplicitLod: type 4, res 15, sampled_img 2, coord 3
    make_op(5, 87), 4, 15, 2, 3,
    // OpFOrdLessThan %16 %15 %5 (Alpha Cutout)
    make_op(5, 184), 6, 16, 15, 5,
    make_op(4, 250 /* OpBranchConditional */), 16, 20, 30,
    make_op(2, 248 /* OpLabel */), 20,
    // OpKill (依赖采样，绝对不能被消除)
    make_op(1, hi3_dxvk::SPV_OP_KILL),
    make_op(2, 248 /* OpLabel */), 30,
    make_op(1, 253),
    make_op(1, 56)
  };

  hi3_dxvk::process_spirv_anti_dither(spv.data(), spv.size());

  bool found_kill = false;
  for (uint32_t w : spv) {
    if ((w & 0xFFFF) == hi3_dxvk::SPV_OP_KILL) {
      found_kill = true;
      break;
    }
  }

  if (!found_kill) {
    std::cerr << "FAILED: Scene foliage cutout OpKill was wrongly eliminated!\n";
    std::abort();
  }

  std::cout << "PASSED\n";
}

// 用例 4: 黑名单 Hash 豁免机制验证
void test_hi3_blacklist_exclusion() {
  std::cout << "[Test HI3 4] Blacklist Hash Exclusion... ";

  setenv("ANTI_DITHER_ENABLED", "1", 1);
  game_logger::g_initialized = false;
  game_logger::g_exclude_hashes.clear();
  game_logger::g_force_hashes.clear();

  union { float f; uint32_t u; } f17;
  f17.f = 17.0f;

  std::vector<uint32_t> spv = {
    hi3_dxvk::SPV_HEADER_MAGIC,
    0x00010300,
    0,
    100,
    0,
    make_op(4, 71), 10, hi3_dxvk::SPV_DECORATION_BUILTIN, hi3_dxvk::SPV_BUILTIN_FRAG_COORD,
    make_op(4, 43), 1, 50, f17.u,
    make_op(5, 54), 2, 1, 0, 3,
    make_op(2, 248), 20,
    make_op(5, 133), 1, 60, 50, 10,
    make_op(5, 184), 6, 70, 60, 10,
    make_op(4, 250), 70, 30, 40,
    make_op(2, 248), 30,
    make_op(1, hi3_dxvk::SPV_OP_KILL),
    make_op(2, 248), 40,
    make_op(1, 253),
    make_op(1, 56)
  };

  uint32_t hash = game_logger::compute_spirv_hash(spv.data(), spv.size());
  game_logger::g_exclude_hashes.insert(hash);

  hi3_dxvk::process_spirv_anti_dither(spv.data(), spv.size());

  bool found_kill = false;
  for (uint32_t w : spv) {
    if ((w & 0xFFFF) == hi3_dxvk::SPV_OP_KILL) {
      found_kill = true;
      break;
    }
  }

  if (!found_kill) {
    std::cerr << "FAILED: Blacklisted shader was modified!\n";
    std::abort();
  }

  game_logger::g_exclude_hashes.clear();
  std::cout << "PASSED\n";
}

int main() {
  std::cout << "=== Honkai Impact 3rd (HI3rd / BH3) Anti-Dither Unit Tests ===\n";
  test_hi3_pipeline_stage_dispatch();
  test_hi3_bayer_dither_kill_neutralized();
  test_hi3_tree_foliage_alpha_cutout_preserved();
  test_hi3_blacklist_exclusion();
  std::cout << "=== All HI3rd Anti-Dither Tests Passed Successfully ===\n";
  return 0;
}
