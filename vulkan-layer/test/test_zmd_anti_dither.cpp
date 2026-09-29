#include <cstdint>
#include <vector>
#include <iostream>
#include <cassert>
#include <cstdlib>

#include "../src/logger.h"
#include "../src/zmd/zmd_anti_dither.h"
#include "../src/zmd/zmd_vulkan.h"
#include "../src/zmd/zmd_mod.h"

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

// 用例 4: 终末地 DXVK 翻译层识别与分发测试
void test_zmd_dxvk_backend_detection() {
  std::cout << "[Test ZMD 4] DXVK backend detection (icb / cb0 / OpDemote)... ";

  // DXVK 着色器样本: 带有 OpName "icb" 与 "cb0"
  std::vector<uint32_t> dxvk_spv = {
    zmd_dxvk::SPV_HEADER_MAGIC,
    0x00010300,
    0, // Generator
    50,
    0,
    make_op(3, 5 /* OpName */), 10, 0x00626369, // "icb\0"
    make_op(4, 15 /* OpEntryPoint */), 4, 4, 0x6e69616d,
    make_op(5, 54 /* OpFunction */), 2, 1, 0, 3
  };

  assert(zmd_layer::is_dxvk_translation_layer(dxvk_spv.data(), dxvk_spv.size()) == true);

  // 原生 Vulkan 着色器样本: 无 DXVK 专有名字
  std::vector<uint32_t> native_spv = {
    zmd_vulkan::SPV_HEADER_MAGIC,
    0x00010300,
    0x00140000, // Google spiregg
    50,
    0,
    make_op(4, 15 /* OpEntryPoint */), 4, 4, 0x6e69616d,
    make_op(5, 54 /* OpFunction */), 2, 1, 0, 3
  };

  assert(zmd_layer::is_dxvk_translation_layer(native_spv.data(), native_spv.size()) == false);

  std::cout << "PASSED\n";
}

// 用例 5: 终末地 DXVK Bayer 点阵消隐与 Demote 中和
void test_zmd_dxvk_character_dither_neutralized() {
  std::cout << "[Test ZMD 5] DXVK Character Bayer Dither (OpDemote neutralized)... ";

  setenv("ANTI_DITHER_ENABLED", "1", 1);
  game_logger::g_initialized = false;
  game_logger::g_exclude_hashes.clear();
  game_logger::g_force_hashes.clear();

  union { float f; uint32_t u; } f17;
  f17.f = 17.0f;

  std::vector<uint32_t> spv = {
    zmd_dxvk::SPV_HEADER_MAGIC,
    0x00010300,
    0,
    100,
    0, // Schema
    make_op(4, 15 /* OpEntryPoint */), 4, 1, 0x6e69616d,
    make_op(3, 5 /* OpName */), 10, 0x00626369, // "icb\0"
    make_op(4, 71 /* OpDecorate */), 10, zmd_dxvk::SPV_DECORATION_BUILTIN, zmd_dxvk::SPV_BUILTIN_FRAG_COORD,
    make_op(4, 43 /* OpConstant */), 1, 50, f17.u,
    make_op(4, 59 /* OpVariable */), 1, 10, 1,
    make_op(5, 54 /* OpFunction */), 2, 1, 0, 3,
    make_op(2, 248 /* OpLabel */), 20,
    make_op(4, 61 /* OpLoad */), 1, 60, 10,
    make_op(5, 148 /* OpDot */), 1, 70, 60, 50,
    make_op(5, 184 /* OpFOrdLessThan */), 6, 80, 70, 50,
    make_op(4, 250 /* OpBranchConditional */), 80, 30, 40,
    make_op(2, 248 /* OpLabel */), 30,
    // OpDemoteToHelperInvocationEXT (DXVK 典型的 discard 翻译)
    make_op(1, zmd_dxvk::SPV_OP_DEMOTE_TO_HELPER_INVOCATION),
    make_op(2, 248 /* OpLabel */), 40,
    make_op(1, 253),
    make_op(1, 56)
  };

  zmd_layer::process_spirv_anti_dither(spv.data(), spv.size());

  bool found_demote = false;
  bool found_nop = false;
  for (uint32_t w : spv) {
    if ((w & 0xFFFF) == zmd_dxvk::SPV_OP_DEMOTE_TO_HELPER_INVOCATION) found_demote = true;
    if ((w & 0xFFFF) == 0 && (w >> 16) == 1) found_nop = true;
  }

  if (found_demote || !found_nop) {
    std::cerr << "FAILED: DXVK character Bayer OpDemote was not converted to OpNop!\n";
    std::abort();
  }

  std::cout << "PASSED\n";
}

// 用例 6: 终末地 DXVK 大世界铁丝网/植被贴图 Alpha Cutout 100% 保护
void test_zmd_dxvk_alpha_cutout_preserved() {
  std::cout << "[Test ZMD 6] DXVK Fence & Foliage Alpha Cutout (Preserved 100%)... ";

  setenv("ANTI_DITHER_ENABLED", "1", 1);
  game_logger::g_initialized = false;
  game_logger::g_exclude_hashes.clear();
  game_logger::g_force_hashes.clear();

  union { float f; uint32_t u; } f17;
  f17.f = 17.0f;

  std::vector<uint32_t> spv = {
    zmd_dxvk::SPV_HEADER_MAGIC,
    0x00010300,
    0,
    100,
    0, // Schema
    make_op(4, 15 /* OpEntryPoint */), 4, 1, 0x6e69616d,
    make_op(3, 5 /* OpName */), 10, 0x00626369, // "icb\0"
    make_op(4, 71 /* OpDecorate */), 10, zmd_dxvk::SPV_DECORATION_BUILTIN, zmd_dxvk::SPV_BUILTIN_FRAG_COORD,
    make_op(4, 43 /* OpConstant */), 1, 50, f17.u,
    make_op(5, 54 /* OpFunction */), 2, 1, 0, 3,
    make_op(2, 248 /* OpLabel */), 10,
    // OpImageSampleImplicitLod (铁丝网纹理采样)
    make_op(5, 87), 4, 15, 2, 3,
    // OpFOrdLessThan %16 %15 %5 (Alpha Cutout)
    make_op(5, 184), 6, 16, 15, 5,
    make_op(4, 250 /* OpBranchConditional */), 16, 20, 30,
    make_op(2, 248 /* OpLabel */), 20,
    // OpDemoteToHelperInvocationEXT (依赖纹理采样，必须保留)
    make_op(1, zmd_dxvk::SPV_OP_DEMOTE_TO_HELPER_INVOCATION),
    make_op(2, 248 /* OpLabel */), 30,
    make_op(1, 253),
    make_op(1, 56)
  };

  zmd_layer::process_spirv_anti_dither(spv.data(), spv.size());

  bool found_demote = false;
  for (uint32_t w : spv) {
    if ((w & 0xFFFF) == zmd_dxvk::SPV_OP_DEMOTE_TO_HELPER_INVOCATION) {
      found_demote = true;
      break;
    }
  }

  if (!found_demote) {
    std::cerr << "FAILED: DXVK Fence Alpha Cutout OpDemote was wrongly modified to OpNop!\n";
    std::abort();
  }

  std::cout << "PASSED\n";
}

// 用例 7: 管理员面具 (Mask) 索引特征识别验证
void test_zmd_nomask_index_detection() {
  std::cout << "[Test ZMD 7] Administrator mask index count detection... ";

  // 默认情况下 4524 作为管理员主面具在所有 Pass 下无条件消除
  assert(zmd_mod::should_skip_mask_draw(0) == false);
  assert(zmd_mod::should_skip_mask_draw(4524) == true);
  assert(zmd_mod::should_skip_mask_draw(9000) == false);
  assert(zmd_mod::should_skip_mask_draw(4524) == true);
  assert(zmd_mod::should_skip_mask_draw(27615) == false);
  assert(zmd_mod::should_skip_mask_draw(4524) == true);
  assert(zmd_mod::should_skip_mask_draw(12345) == false);
  assert(zmd_mod::should_skip_mask_draw(4524) == true);

  // 陈千语专属特征保护门禁: 紧随陈千语 Submesh (888, 714, 9477, 15138, 46728, 139392) 的 4524 必须保护放行
  assert(zmd_mod::should_skip_mask_draw(888) == false);
  assert(zmd_mod::should_skip_mask_draw(4524) == false);
  assert(zmd_mod::should_skip_mask_draw(714) == false);
  assert(zmd_mod::should_skip_mask_draw(4524) == false);
  assert(zmd_mod::should_skip_mask_draw(9477) == false);
  assert(zmd_mod::should_skip_mask_draw(4524) == false);
  assert(zmd_mod::should_skip_mask_draw(15138) == false);
  assert(zmd_mod::should_skip_mask_draw(4524) == false);
  assert(zmd_mod::should_skip_mask_draw(46728) == false);
  assert(zmd_mod::should_skip_mask_draw(4524) == false);
  assert(zmd_mod::should_skip_mask_draw(139392) == false);
  assert(zmd_mod::should_skip_mask_draw(4524) == false);

  // 远景 LOD 与配件 (不受时序影响，直接判定)
  assert(zmd_mod::should_skip_mask_draw(2028) == true);
  assert(zmd_mod::should_skip_mask_draw(117) == true);
  assert(zmd_mod::should_skip_mask_draw(69) == true);
  assert(zmd_mod::should_skip_mask_draw(51) == true);

  // 非面具网格必须全部保留 (返回 false)
  assert(zmd_mod::should_skip_mask_draw(0) == false);
  assert(zmd_mod::should_skip_mask_draw(100) == false);
  assert(zmd_mod::should_skip_mask_draw(116) == false);
  assert(zmd_mod::should_skip_mask_draw(118) == false);
  assert(zmd_mod::should_skip_mask_draw(2027) == false);
  assert(zmd_mod::should_skip_mask_draw(2029) == false);
  assert(zmd_mod::should_skip_mask_draw(4523) == false);
  assert(zmd_mod::should_skip_mask_draw(4525) == false);
  assert(zmd_mod::should_skip_mask_draw(12000) == false);

  std::cout << "PASSED\n";
}

// 用例 8: zmd_nomask 环境变量开关与覆盖逻辑验证
void test_zmd_nomask_env_toggle() {
  std::cout << "[Test ZMD 8] zmd_nomask environment toggle... ";

  // 1. 测试显式启用 zmd_nomask=1
  setenv("zmd_nomask", "1", 1);
  game_logger::g_initialized = false;
  assert(game_logger::is_zmd_nomask_enabled() == true);

  // 2. 测试显式禁用 zmd_nomask=0
  setenv("zmd_nomask", "0", 1);
  game_logger::g_initialized = false;
  assert(game_logger::is_zmd_nomask_enabled() == false);

  // 3. 测试大写 ZMD_NOMASK=1 兼容性
  unsetenv("zmd_nomask");
  setenv("ZMD_NOMASK", "1", 1);
  game_logger::g_initialized = false;
  assert(game_logger::is_zmd_nomask_enabled() == true);

  // 清理测试环境
  unsetenv("ZMD_NOMASK");
  game_logger::g_initialized = false;

  std::cout << "PASSED\n";
}

int main() {
  std::cout << "=== Arknights: Endfield (ZMD / Vulkan & DXVK) Anti-Dither Unit Tests ===\n";
  test_zmd_pipeline_stage_dispatch();
  test_zmd_character_bayer_dither_neutralized();
  test_zmd_fence_foliage_alpha_cutout_preserved();
  test_zmd_dxvk_backend_detection();
  test_zmd_dxvk_character_dither_neutralized();
  test_zmd_dxvk_alpha_cutout_preserved();
  test_zmd_nomask_index_detection();
  test_zmd_nomask_env_toggle();
  std::cout << "=== All ZMD Tests Passed Successfully ===\n";
  return 0;
}
