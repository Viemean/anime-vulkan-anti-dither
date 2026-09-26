#include <cstdint>
#include <vector>
#include <iostream>
#include <cassert>
#include <cstdlib>

#include "../src/logger.h"
#include "../src/zzz/zzz_anti_dither.h"
#include "../src/zzz/zzz_vkd3d.h"

static inline uint32_t make_op(uint16_t length, uint16_t opcode) {
  return (static_cast<uint32_t>(length) << 16) | opcode;
}

// 用例 1: 着色器阶段检测
void test_zzz_stage_detection() {
  std::cout << "[Test ZZZ 1] Shader stage detection... ";

  std::vector<uint32_t> ps_spv = {
    0x07230203,
    0x00010300,
    0,
    50,
    0,
    make_op(4, 15 /* OpEntryPoint */), 4, 4, 0x6e69616d // ExecutionModel = 4 (Fragment)
  };

  auto stage = zzz_layer::detect_shader_stage(ps_spv.data(), ps_spv.size());
  (void)stage;
  assert(stage == zzz_layer::ShaderStage::Fragment);

  std::cout << "PASSED\n";
}

// 用例 2: VKD3D Bayer 4x4 网点虚化消除
void test_zzz_vkd3d_bayer_neutralized() {
  std::cout << "[Test ZZZ 2] VKD3D Bayer 4x4 dither demote neutralization... ";

  setenv("ANTI_DITHER_ENABLED", "1", 1);
  game_logger::g_initialized = false;
  game_logger::g_exclude_hashes.clear();
  game_logger::g_force_hashes.clear();

  // 构造包含 0x3d70f0f1 (1/17) Bayer Composite 矩阵与 demote helper 调用的着色器
  std::vector<uint32_t> spv = {
    0x07230203,
    0x00010300,
    0,
    100,
    0,
    // OpConstant %10 = 0x3d70f0f1
    make_op(4, 43 /* OpConstant */), 1, 10, 0x3d70f0f1,
    // OpConstantComposite %20 = [%10, ...]
    make_op(4, 44 /* OpConstantComposite */), 2, 20, 10,
    // OpVariable %30 (Private) = %20
    make_op(5, 59 /* OpVariable */), 3, 30, 0, 20,
    // OpFunction %demote_helper (50)
    make_op(5, 54 /* OpFunction */), 2, 50, 0, 3,
    make_op(2, 248 /* OpLabel */), 51,
    make_op(1, 5380 /* OpDemoteToHelperInvocationEXT */),
    make_op(1, 56 /* OpFunctionEnd */),
    // OpFunction %main (60)
    make_op(5, 54 /* OpFunction */), 2, 60, 0, 3,
    make_op(2, 248 /* OpLabel */), 61,
    // OpAccessChain %70 from %30 (Bayer variable)
    make_op(4, 65 /* OpAccessChain */), 4, 70, 30,
    // OpLoad %71 from %70
    make_op(4, 61 /* OpLoad */), 1, 71, 70,
    // OpFSub %72 = %arg - %71
    make_op(5, 131 /* OpFSub */), 1, 72, 80, 71,
    // OpFunctionCall %50 (helper) with %72 (derived from Bayer)
    make_op(5, 57 /* OpFunctionCall */), 2, 90, 50, 72,
    make_op(1, 56 /* OpFunctionEnd */)
  };

  // 记录原始调用指令位置
  size_t call_offset = 0;
  for (size_t i = 5; i < spv.size(); ) {
    uint16_t op = spv[i] & 0xFFFF;
    uint16_t len = (spv[i] >> 16) & 0xFFFF;
    if (op == 57) {
      call_offset = i;
      break;
    }
    i += len;
  }
  assert(call_offset != 0);
  (void)call_offset;

  zzz_layer::process_spirv_anti_dither(spv.data(), spv.size());

  // 验证 OpFunctionCall 已被替换为 OpNop
  for (size_t k = 0; k < 5; ++k) {
    assert(spv[call_offset + k] == 0x00010000u /* OpNop */);
  }

  std::cout << "PASSED\n";
}

// 用例 3: 非 Bayer 材质镂空保护
void test_zzz_vkd3d_material_cutout_preserved() {
  std::cout << "[Test ZZZ 3] Material Alpha Cutout demote preserved... ";

  setenv("ANTI_DITHER_ENABLED", "1", 1);
  game_logger::g_initialized = false;
  game_logger::g_exclude_hashes.clear();
  game_logger::g_force_hashes.clear();

  // 构造普通纹理材质镂空着色器（无 Bayer 常数）
  std::vector<uint32_t> spv = {
    0x07230203,
    0x00010300,
    0,
    100,
    0,
    // OpFunction %demote_helper (50)
    make_op(5, 54 /* OpFunction */), 2, 50, 0, 3,
    make_op(2, 248 /* OpLabel */), 51,
    make_op(1, 5380 /* OpDemoteToHelperInvocationEXT */),
    make_op(1, 56 /* OpFunctionEnd */),
    // OpFunction %main (60)
    make_op(5, 54 /* OpFunction */), 2, 60, 0, 3,
    make_op(2, 248 /* OpLabel */), 61,
    // OpFunctionCall %50 (helper) with normal alpha parameter %80
    make_op(5, 57 /* OpFunctionCall */), 2, 90, 50, 80,
    make_op(1, 56 /* OpFunctionEnd */)
  };

  size_t call_offset = 0;
  for (size_t i = 5; i < spv.size(); ) {
    uint16_t op = spv[i] & 0xFFFF;
    uint16_t len = (spv[i] >> 16) & 0xFFFF;
    if (op == 57) {
      call_offset = i;
      break;
    }
    i += len;
  }
  assert(call_offset != 0);
  (void)call_offset;

  zzz_layer::process_spirv_anti_dither(spv.data(), spv.size());

  // 验证未被篡改，依然是 OpFunctionCall
  assert((spv[call_offset] & 0xFFFF) == 57);

  std::cout << "PASSED\n";
}

int main() {
  std::cout << "=== Running ZZZ Anti-Dither Tests ===\n";
  test_zzz_stage_detection();
  test_zzz_vkd3d_bayer_neutralized();
  test_zzz_vkd3d_material_cutout_preserved();
  std::cout << "=== All ZZZ Tests Passed Successfully ===\n";
  return 0;
}
