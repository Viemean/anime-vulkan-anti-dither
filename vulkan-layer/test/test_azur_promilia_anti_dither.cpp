#include <cstdint>
#include <vector>
#include <iostream>
#include <cassert>
#include <cstdlib>

#include "../src/logger.h"
#include "../src/azur_promilia/azur_promilia_anti_dither.h"

static inline uint32_t make_op(uint16_t length, uint16_t opcode) {
  return (static_cast<uint32_t>(length) << 16) | opcode;
}

// 用例 1: 蓝色星原农田作物镂空保护 (word_count = 114)
void test_azur_promilia_crop_cutout_preserved() {
  std::cout << "[Test AzurPromilia 1] Crop leaf cutout preserved (Length: 114 DW)... ";

  setenv("ANTI_DITHER_ENABLED", "1", 1);
  game_logger::g_initialized = false;
  game_logger::g_exclude_hashes.clear();
  game_logger::g_force_hashes.clear();

  std::vector<uint32_t> spv(114, 0);
  spv[0] = 0x07230203;
  spv[1] = 0x00010300;
  spv[2] = 0;
  spv[3] = 100;
  spv[4] = 0;

  // 放入 OpKill (252)
  spv[10] = make_op(1, 252 /* OpKill */);

  azur_promilia_layer::process_spirv_anti_dither(spv.data(), spv.size());

  // 验证未被 NOP，依然是 OpKill
  assert((spv[10] & 0xFFFF) == 252);

  std::cout << "PASSED\n";
}

// 用例 2: 蓝色星原角色散斑噪声 (52.9829178) Fade 解耦与网点中和
void test_azur_promilia_speckle_noise_decoupled() {
  std::cout << "[Test AzurPromilia 2] Speckle noise fade decoupling & dither neutralization... ";

  setenv("ANTI_DITHER_ENABLED", "1", 1);
  game_logger::g_initialized = false;
  game_logger::g_exclude_hashes.clear();
  game_logger::g_force_hashes.clear();

  std::vector<uint32_t> spv = {
    0x07230203,
    0x00010300,
    0,
    100,
    0,
    // OpConstant %58 = 52.9829178f (0x4253ee82)
    make_op(4, 43 /* OpConstant */), 1, 58, 0x4253ee82,
    // OpConstant %66 = 1.0f (0x3f800000)
    make_op(4, 43 /* OpConstant */), 1, 66, 0x3f800000,
    // OpFunction %1
    make_op(5, 54 /* OpFunction */), 2, 1, 0, 3,
    make_op(2, 248 /* OpLabel */), 20,
    // OpFMul %70 = %in * %58
    make_op(5, 133 /* OpFMul */), 1, 70, 80, 58,
    // OpExtInst %71 = GLSL.std.450 Fract (10) %70
    make_op(6, 12 /* OpExtInst */), 1, 71, 99, 10, 70,
    // OpLoad %384 (FadeThreshold from CB)
    make_op(4, 61 /* OpLoad */), 1, 384, 85,
    // OpFSub %72 = %384 - %71
    make_op(5, 131 /* OpFSub */), 1, 72, 384, 71,
    // OpFOrdLessThan %73 = %72 < 0
    make_op(5, 184 /* OpFOrdLessThan */), 6, 73, 72, 0,
    // OpBranchConditional %73 %30 %40
    make_op(4, 250 /* OpBranchConditional */), 73, 30, 40,
    // OpLabel %30
    make_op(2, 248 /* OpLabel */), 30,
    // OpDemoteToHelperInvocation
    make_op(1, 5380 /* OpDemoteToHelperInvocationEXT */),
    // OpLabel %40
    make_op(2, 248 /* OpLabel */), 40,
    make_op(1, 253 /* OpReturn */),
    make_op(1, 56 /* OpFunctionEnd */)
  };

  // 记录 OpLoad %384 的位置
  size_t load_offset = 0;
  size_t demote_offset = 0;
  for (size_t i = 5; i < spv.size(); ) {
    uint16_t op = spv[i] & 0xFFFF;
    uint16_t len = (spv[i] >> 16) & 0xFFFF;
    if (op == 61 && spv[i + 2] == 384) {
      load_offset = i;
    } else if (op == 5380) {
      demote_offset = i;
    }
    i += len;
  }
  assert(load_offset != 0 && demote_offset != 0);
  (void)load_offset;
  (void)demote_offset;

  azur_promilia_layer::process_spirv_anti_dither(spv.data(), spv.size());

  // 1. 验证 FadeThreshold 已被改写为 OpCopyObject %66 (1.0f)
  assert((spv[load_offset] & 0xFFFF) == 83 /* OpCopyObject */);
  assert(spv[load_offset + 3] == 66 /* c_1_id */);

  // 2. 验证 Demote 已被替换为 OpNop
  assert((spv[demote_offset] & 0xFFFF) == 0 /* OpNop */);

  std::cout << "PASSED\n";
}

int main() {
  std::cout << "=== Running Azur Promilia Anti-Dither Tests ===\n";
  test_azur_promilia_crop_cutout_preserved();
  test_azur_promilia_speckle_noise_decoupled();
  std::cout << "=== All Azur Promilia Tests Passed Successfully ===\n";
  return 0;
}
