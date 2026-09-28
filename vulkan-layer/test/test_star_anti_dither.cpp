#include <cstdint>
#include <vector>
#include <iostream>
#include <cassert>
#include <cstdlib>

#include "../src/logger.h"
#include "../src/star/star_anti_dither.h"

static inline uint32_t make_op(uint16_t length, uint16_t opcode) {
  return (static_cast<uint32_t>(length) << 16) | opcode;
}

// 用例 1: 着色器阶段检测
void test_star_stage_detection() {
  std::cout << "[Test Star 1] Shader stage detection... ";

  std::vector<uint32_t> ps_spv = {
    0x07230203,
    0x00010300,
    0,
    50,
    0,
    make_op(4, 15 /* OpEntryPoint */), 4, 4, 0x6e69616d // ExecutionModel = 4 (Fragment)
  };

  auto stage = star_layer::detect_shader_stage(ps_spv.data(), ps_spv.size());
  (void)stage;
  assert(stage == star_layer::ShaderStage::Fragment);

  std::cout << "PASSED\n";
}

// 用例 2: 4x4 Bayer 17.0f 模数网点虚化消除 (OpKill 分支重定向)
void test_star_bayer_kill_neutralized() {
  std::cout << "[Test Star 2] 4x4 Bayer 17.0f OpKill branch redirection... ";

  setenv("ANTI_DITHER_ENABLED", "1", 1);
  game_logger::g_initialized = false;
  game_logger::g_exclude_hashes.clear();
  game_logger::g_force_hashes.clear();

  union { float f; uint32_t u; } fu;
  fu.f = 17.0f;

  std::vector<uint32_t> spv = {
    0x07230203,
    0x00010300,
    0,
    100,
    0,
    // OpConstant %10 = 17.0f
    make_op(4, 43 /* OpConstant */), 1, 10, fu.u,
    // OpFunction %main (20)
    make_op(5, 54 /* OpFunction */), 2, 20, 0, 3,
    make_op(2, 248 /* OpLabel */), 21,
    // OpFMod %30 = %arg(25) % 17.0f(10)
    make_op(5, 133 /* OpFRem/OpFMod */), 1, 30, 25, 10,
    // OpFOrdLessThan %31 = %30, %threshold(26)
    make_op(5, 184 /* OpFOrdLessThan */), 3, 31, 30, 26,
    // OpBranchConditional %31, %kill_label(40), %cont_label(50)
    make_op(4, 250 /* OpBranchConditional */), 31, 40, 50,
    // %kill_label:
    make_op(2, 248 /* OpLabel */), 40,
    make_op(1, 252 /* OpKill */),
    // %cont_label:
    make_op(2, 248 /* OpLabel */), 50,
    make_op(1, 253 /* OpReturn */),
    make_op(1, 56 /* OpFunctionEnd */)
  };

  // 寻找 OpBranchConditional 指令位置
  size_t branch_idx = 0;
  for (size_t i = 5; i < spv.size(); ) {
    uint16_t op = spv[i] & 0xFFFF;
    uint16_t len = (spv[i] >> 16) & 0xFFFF;
    if (op == 250) {
      branch_idx = i;
      break;
    }
    i += len;
  }
  assert(branch_idx > 0);
  assert(spv[branch_idx + 2] == 40); // 初始 true_label 为 40
  (void)branch_idx;

  star_layer::process_spirv_anti_dither(spv.data(), spv.size());

  // 验证 true_label 已被重定向为 50，跳过 OpKill 基本块
  assert(spv[branch_idx + 2] == 50);

  std::cout << "PASSED\n";
}

// 用例 3: 4x4 Bayer Demote 指令定向置 NOP
void test_star_bayer_demote_neutralized() {
  std::cout << "[Test Star 3] 4x4 Bayer Demote neutralization... ";

  union { float f; uint32_t u; } fu;
  fu.f = 0.0588235f; // 1/17.0f

  std::vector<uint32_t> spv = {
    0x07230203,
    0x00010300,
    0,
    100,
    0,
    // OpConstant %10 = 1/17.0f
    make_op(4, 43 /* OpConstant */), 1, 10, fu.u,
    // OpFunction %main (20)
    make_op(5, 54 /* OpFunction */), 2, 20, 0, 3,
    make_op(2, 248 /* OpLabel */), 21,
    // OpFMul %30 = %arg, %10
    make_op(5, 133 /* OpFMul */), 1, 30, 25, 10,
    // OpFOrdLessThan %31 = %30, %fade
    make_op(5, 184 /* OpFOrdLessThan */), 3, 31, 30, 26,
    make_op(4, 250 /* OpBranchConditional */), 31, 40, 50,
    make_op(2, 248 /* OpLabel */), 40,
    make_op(1, 5380 /* OpDemoteToHelperInvocation */),
    make_op(2, 249 /* OpBranch */), 50,
    make_op(2, 248 /* OpLabel */), 50,
    make_op(1, 253 /* OpReturn */),
    make_op(1, 56 /* OpFunctionEnd */)
  };

  size_t demote_idx = 0;
  for (size_t i = 5; i < spv.size(); ) {
    uint16_t op = spv[i] & 0xFFFF;
    uint16_t len = (spv[i] >> 16) & 0xFFFF;
    if (op == 5380) {
      demote_idx = i;
      break;
    }
    i += len;
  }
  assert(demote_idx > 0);
  (void)demote_idx;

  star_layer::process_spirv_anti_dither(spv.data(), spv.size());

  // 验证 OpDemote 已被替换为 OpNop
  assert(spv[demote_idx] == 0x00010000u /* OpNop */);

  std::cout << "PASSED\n";
}

// 用例 4: 依赖纹理采样的正常 Alpha Cutout 100% 保护
void test_star_texture_sample_cutout_preserved() {
  std::cout << "[Test Star 4] Texture sample cutout preservation... ";

  std::vector<uint32_t> spv = {
    0x07230203,
    0x00010300,
    0,
    100,
    0,
    // OpConstant %10 = 0.5f (Cutout threshold)
    make_op(4, 43 /* OpConstant */), 1, 10, 0x3f000000,
    // OpFunction %main (20)
    make_op(5, 54 /* OpFunction */), 2, 20, 0, 3,
    make_op(2, 248 /* OpLabel */), 21,
    // OpImageSampleImplicitLod %30 (Image Sample)
    make_op(5, 87 /* OpImageSampleImplicitLod */), 1, 30, 22, 23,
    // OpCompositeExtract %31 = %30.a
    make_op(4, 81 /* OpCompositeExtract */), 1, 31, 30,
    // OpFOrdLessThan %32 = %31, 0.5f
    make_op(5, 184 /* OpFOrdLessThan */), 3, 32, 31, 10,
    // OpBranchConditional %32, %kill_label(40), %cont_label(50)
    make_op(4, 250 /* OpBranchConditional */), 32, 40, 50,
    make_op(2, 248 /* OpLabel */), 40,
    make_op(1, 252 /* OpKill */),
    make_op(2, 248 /* OpLabel */), 50,
    make_op(1, 253 /* OpReturn */),
    make_op(1, 56 /* OpFunctionEnd */)
  };

  size_t branch_idx = 0;
  for (size_t i = 5; i < spv.size(); ) {
    uint16_t op = spv[i] & 0xFFFF;
    uint16_t len = (spv[i] >> 16) & 0xFFFF;
    if (op == 250) {
      branch_idx = i;
      break;
    }
    i += len;
  }
  assert(branch_idx > 0);
  (void)branch_idx;

  star_layer::process_spirv_anti_dither(spv.data(), spv.size());

  // 验证因依赖纹理采样，分支保持未变，正常 Alpha Cutout 得到保护
  assert(spv[branch_idx + 2] == 40);

  std::cout << "PASSED\n";
}

// 用例 5: DXVK icb (Immediate Constant Buffer) 矩阵虚化消除 (真实游戏模式)
void test_star_icb_dither_neutralized() {
  std::cout << "[Test Star 5] DXVK icb Bayer dither demote neutralization... ";

  std::vector<uint32_t> spv = {
    0x07230203,
    0x00010300,
    0,
    100,
    0,
    // OpName %icb "icb"
    make_op(3, 5 /* OpName */), 10, 0x00626369, // "icb\0"
    // OpFunction %main (20)
    make_op(5, 54 /* OpFunction */), 2, 20, 0, 3,
    make_op(2, 248 /* OpLabel */), 21,
    // OpAccessChain %30 from %10 (icb)
    make_op(4, 65 /* OpAccessChain */), 1, 30, 10,
    // OpLoad %31 from %30
    make_op(4, 61 /* OpLoad */), 1, 31, 30,
    // OpFOrdLessThan %32 = %31, %fade(40)
    make_op(5, 184 /* OpFOrdLessThan */), 3, 32, 31, 40,
    // OpBranchConditional %32, %demote_label(50), %cont_label(60)
    make_op(4, 250 /* OpBranchConditional */), 32, 50, 60,
    make_op(2, 248 /* OpLabel */), 50,
    make_op(1, 5380 /* OpDemoteToHelperInvocation */),
    make_op(2, 249 /* OpBranch */), 60,
    make_op(2, 248 /* OpLabel */), 60,
    make_op(1, 253 /* OpReturn */),
    make_op(1, 56 /* OpFunctionEnd */)
  };

  size_t demote_idx = 0;
  for (size_t i = 5; i < spv.size(); ) {
    uint16_t op = spv[i] & 0xFFFF;
    uint16_t len = (spv[i] >> 16) & 0xFFFF;
    if (op == 5380) {
      demote_idx = i;
      break;
    }
    i += len;
  }
  assert(demote_idx > 0);
  (void)demote_idx;

  star_layer::process_spirv_anti_dither(spv.data(), spv.size());

  // 验证 OpDemote 已被替换为 OpNop
  assert(spv[demote_idx] == 0x00010000u /* OpNop */);

  std::cout << "PASSED\n";
}

int main() {
  std::cout << "=== Star Anti-Dither Layer Tests ===\n";
  test_star_stage_detection();
  test_star_bayer_kill_neutralized();
  test_star_bayer_demote_neutralized();
  test_star_texture_sample_cutout_preserved();
  test_star_icb_dither_neutralized();
  std::cout << "All Star anti-dither tests passed successfully!\n";
  return 0;
}
