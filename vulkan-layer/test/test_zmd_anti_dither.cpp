#include <cstdint>
#include <vector>
#include <iostream>
#include <cassert>
#include <cstdlib>

#include "../src/logger.h"
#include "../src/zmd/zmd_anti_dither.h"
#include "../src/zmd/zmd_vulkan.h"

static inline uint32_t make_op(uint16_t length, uint16_t opcode) {
  return (static_cast<uint32_t>(length) << 16) | opcode;
}

// 用例 1: 终末地管线阶段分发判定 (Fragment vs Vertex)
void test_zmd_pipeline_stage_dispatch() {
  std::cout << "[Test ZMD 1] Stage detection (Fragment vs Vertex)... ";

  std::vector<uint32_t> ps_spv = {
    zmd_vulkan::SPV_HEADER_MAGIC,
    0x00010300,
    0, // Generator
    50,
    0,
    make_op(4, 15 /* OpEntryPoint */), 4, 4, 0x6e69616d // ExecutionModel = 4 (Fragment)
  };

  auto ps_stage = zmd_layer::detect_shader_stage(ps_spv.data(), ps_spv.size());
  (void)ps_stage;
  assert(ps_stage == zmd_layer::ShaderStage::Fragment);

  std::vector<uint32_t> vs_spv = {
    zmd_vulkan::SPV_HEADER_MAGIC,
    0x00010300,
    0,
    50,
    0,
    make_op(4, 15 /* OpEntryPoint */), 0, 4, 0x6e69616d // ExecutionModel = 0 (Vertex)
  };

  auto vs_stage = zmd_layer::detect_shader_stage(vs_spv.data(), vs_spv.size());
  (void)vs_stage;
  assert(vs_stage == zmd_layer::ShaderStage::Vertex);

  std::cout << "PASSED\n";
}

// 用例 2: 终末地角色 Bayer 4x4 网格消隐精准中和 (OpKill -> OpNop)
void test_zmd_character_bayer_dither_neutralized() {
  std::cout << "[Test ZMD 2] Character Bayer Dither (OpKill neutralized)... ";

  setenv("ANTI_DITHER_ENABLED", "1", 1);
  game_logger::g_initialized = false;
  game_logger::g_exclude_hashes.clear();
  game_logger::g_force_hashes.clear();

  union { float f; uint32_t u; } f17;
  f17.f = 17.0f;

  std::vector<uint32_t> spv = {
    zmd_vulkan::SPV_HEADER_MAGIC,
    0x00010300,
    0,
    100, // Bound
    0,
    // OpDecorate %10 BuiltIn FragCoord
    make_op(4, 71 /* OpDecorate */), 10, zmd_vulkan::SPV_DECORATION_BUILTIN, zmd_vulkan::SPV_BUILTIN_FRAG_COORD,
    // OpConstant %50 (17.0f Bayer factor)
    make_op(4, 43 /* OpConstant */), 1, 50, f17.u,
    // OpVariable ptr_Input %10
    make_op(4, 59 /* OpVariable */), 1, 10, 1,
    // OpFunction
    make_op(5, 54 /* OpFunction */), 2, 1, 0, 3,
    make_op(2, 248 /* OpLabel */), 20,
    // OpLoad %60 %10 (FragCoord)
    make_op(4, 61 /* OpLoad */), 1, 60, 10,
    // OpDot %70 %60 %50
    make_op(5, 148 /* OpDot */), 1, 70, 60, 50,
    // OpFOrdLessThan %80 %70 %50
    make_op(5, 184 /* OpFOrdLessThan */), 6, 80, 70, 50,
    // OpBranchConditional %80 %30 %40
    make_op(4, 250 /* OpBranchConditional */), 80, 30, 40,
    // OpLabel %30
    make_op(2, 248 /* OpLabel */), 30,
    // OpKill (应当被中和为 OpNop)
    make_op(1, zmd_vulkan::SPV_OP_KILL),
    // OpLabel %40
    make_op(2, 248 /* OpLabel */), 40,
    make_op(1, 253 /* OpReturn */),
    make_op(1, 56 /* OpFunctionEnd */)
  };

  zmd_vulkan::process_spirv_anti_dither(spv.data(), spv.size());

  // 验证 OpBranchConditional 的 true_label 是否从 %30 被重定向到了 %40 (绕过 OpKill 块，CFG 保持合法)
  bool branch_redirected = false;
  for (size_t i = 5; i < spv.size(); ) {
    uint32_t w = spv[i];
    uint16_t op = w & 0xFFFF;
    uint16_t len = (w >> 16) & 0xFFFF;
    if (len == 0 || (i + len) > spv.size()) break;
    if (op == 250 && len >= 4) {
      if (spv[i + 1] == 80 && spv[i + 2] == 40 && spv[i + 3] == 40) {
        branch_redirected = true;
      }
    }
    i += len;
  }

  if (!branch_redirected) {
    std::cerr << "FAILED: ZMD character Bayer branch was not safely redirected to merge label!\n";
    std::abort();
  }

  std::cout << "PASSED\n";
}

// 用例 3: 终末地大世界铁丝网/植被贴图 Alpha Cutout 100% 保护
void test_zmd_fence_foliage_alpha_cutout_preserved() {
  std::cout << "[Test ZMD 3] Fence & Foliage Alpha Cutout with Texture Sample (Preserved 100%)... ";

  setenv("ANTI_DITHER_ENABLED", "1", 1);
  game_logger::g_initialized = false;
  game_logger::g_exclude_hashes.clear();
  game_logger::g_force_hashes.clear();

  union { float f; uint32_t u; } f17;
  f17.f = 17.0f;

  std::vector<uint32_t> spv = {
    zmd_vulkan::SPV_HEADER_MAGIC,
    0x00010300,
    0,
    100,
    0,
    make_op(4, 71 /* OpDecorate */), 10, zmd_vulkan::SPV_DECORATION_BUILTIN, zmd_vulkan::SPV_BUILTIN_FRAG_COORD,
    make_op(4, 43 /* OpConstant */), 1, 50, f17.u,
    make_op(5, 54 /* OpFunction */), 2, 1, 0, 3,
    make_op(2, 248 /* OpLabel */), 10,
    // OpImageSampleImplicitLod (铁丝网纹理采样)
    make_op(5, 87), 4, 15, 2, 3,
    // OpFOrdLessThan %16 %15 %5 (Alpha Cutout)
    make_op(5, 184), 6, 16, 15, 5,
    make_op(4, 250 /* OpBranchConditional */), 16, 20, 30,
    make_op(2, 248 /* OpLabel */), 20,
    // OpKill (依赖纹理采样，必须保留)
    make_op(1, zmd_vulkan::SPV_OP_KILL),
    make_op(2, 248 /* OpLabel */), 30,
    make_op(1, 253),
    make_op(1, 56)
  };

  zmd_vulkan::process_spirv_anti_dither(spv.data(), spv.size());

  bool found_kill = false;
  for (uint32_t w : spv) {
    if ((w & 0xFFFF) == zmd_vulkan::SPV_OP_KILL) {
      found_kill = true;
      break;
    }
  }

  if (!found_kill) {
    std::cerr << "FAILED: Fence Alpha Cutout OpKill was wrongly modified to OpNop!\n";
    std::abort();
  }

  std::cout << "PASSED\n";
}

int main() {
  std::cout << "=== Arknights: Endfield (ZMD / Native Vulkan) Anti-Dither Unit Tests ===\n";
  test_zmd_pipeline_stage_dispatch();
  test_zmd_character_bayer_dither_neutralized();
  test_zmd_fence_foliage_alpha_cutout_preserved();
  std::cout << "=== All ZMD Tests Passed Successfully ===\n";
  return 0;
}
