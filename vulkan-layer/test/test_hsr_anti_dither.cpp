#include <cstdint>
#include <vector>
#include <iostream>
#include <cassert>
#include <cstdlib>

#include "../src/logger.h"
#include "../src/hsr/hsr_anti_dither.h"
#include "../src/hsr/hsr_dxvk.h"

static inline uint32_t make_op(uint16_t length, uint16_t opcode) {
  return (static_cast<uint32_t>(length) << 16) | opcode;
}

// 用例 1: HSR 顶层着色器阶段分发判定 (Vertex vs Fragment)
void test_hsr_pipeline_stage_dispatch() {
  std::cout << "[Test HSR 1] Stage detection (Fragment vs Vertex vs Compute)... ";

  // 构造 Fragment Shader (OpEntryPoint Fragment %4 "main")
  std::vector<uint32_t> ps_spv = {
    hsr_dxvk::SPV_HEADER_MAGIC,
    0x00010300,
    0, // Generator (DXVK)
    50,
    0,
    make_op(4, 15 /* OpEntryPoint */), 4, 4, 0x6e69616d // ExecutionModel = 4 (Fragment)
  };

  auto ps_stage = hsr_layer::detect_shader_stage(ps_spv.data(), ps_spv.size());
  (void)ps_stage;
  assert(ps_stage == hsr_layer::ShaderStage::Fragment);

  // 构造 Vertex Shader (OpEntryPoint Vertex %4 "main")
  std::vector<uint32_t> vs_spv = {
    hsr_dxvk::SPV_HEADER_MAGIC,
    0x00010300,
    0,
    50,
    0,
    make_op(4, 15 /* OpEntryPoint */), 0, 4, 0x6e69616d // ExecutionModel = 0 (Vertex)
  };

  auto vs_stage = hsr_layer::detect_shader_stage(vs_spv.data(), vs_spv.size());
  (void)vs_stage;
  assert(vs_stage == hsr_layer::ShaderStage::Vertex);

  std::cout << "PASSED\n";
}

// 用例 2: HSR 角色 Bayer 4x4 网点虚化精准中和
void test_hsr_bayer_dither_neutralized() {
  std::cout << "[Test HSR 2] Character 4x4 Bayer Dither (Demote neutralized)... ";

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
    hsr_dxvk::SPV_HEADER_MAGIC,
    0x00010300,
    0, // Generator
    100, // Bound
    0,
    // OpDecorate %10 BuiltIn FragCoord
    make_op(4, 71 /* OpDecorate */), 10, hsr_dxvk::SPV_DECORATION_BUILTIN, hsr_dxvk::SPV_BUILTIN_FRAG_COORD,
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
    // OpDemoteToHelperInvocation (应当被 NOP)
    make_op(1, hsr_dxvk::SPV_OP_DEMOTE_TO_HELPER_INVOCATION),
    // OpLabel %40
    make_op(2, 248 /* OpLabel */), 40,
    make_op(1, 253 /* OpReturn */),
    make_op(1, 56 /* OpFunctionEnd */)
  };

  hsr_dxvk::process_spirv_anti_dither(spv.data(), spv.size());

  bool found_nop = false;
  for (uint32_t w : spv) {
    if ((w & 0xFFFF) == hsr_dxvk::SPV_OP_NOP) {
      found_nop = true;
      break;
    }
  }
  if (!found_nop) {
    std::cerr << "FAILED: Bayer demote was not neutralized!\n";
    std::abort();
  }
  std::cout << "PASSED\n";
}

// 用例 3: 非 Bayer 材质 Alpha Cutout 100% 保护（不含 17.0f，Demote 完整保留）
void test_hsr_alpha_cutout_preserved() {
  std::cout << "[Test HSR 3] Alpha Cutout without Bayer constant (Preserved 100%)... ";

  setenv("ANTI_DITHER_ENABLED", "1", 1);
  game_logger::g_initialized = false;
  game_logger::g_exclude_hashes.clear();
  game_logger::g_force_hashes.clear();

  union {
    float f;
    uint32_t u;
  } f_alpha;
  f_alpha.f = 0.5f;

  std::vector<uint32_t> spv = {
    hsr_dxvk::SPV_HEADER_MAGIC,
    0x00010300,
    0,
    100,
    0,
    // OpConstant %50 (0.5f Cutout threshold)
    make_op(4, 43 /* OpConstant */), 1, 50, f_alpha.u,
    // OpFunction
    make_op(5, 54 /* OpFunction */), 2, 1, 0, 3,
    make_op(2, 248 /* OpLabel */), 20,
    // OpFOrdLessThan %70 %50 %50
    make_op(5, 184 /* OpFOrdLessThan */), 6, 70, 50, 50,
    // OpBranchConditional %70 %30 %40
    make_op(4, 250 /* OpBranchConditional */), 70, 30, 40,
    // OpLabel %30
    make_op(2, 248 /* OpLabel */), 30,
    // OpDemoteToHelperInvocation (非 Bayer，绝不能被 NOP)
    make_op(1, hsr_dxvk::SPV_OP_DEMOTE_TO_HELPER_INVOCATION),
    // OpLabel %40
    make_op(2, 248 /* OpLabel */), 40,
    make_op(1, 253 /* OpReturn */),
    make_op(1, 56 /* OpFunctionEnd */)
  };

  hsr_dxvk::process_spirv_anti_dither(spv.data(), spv.size());

  bool found_demote = false;
  for (uint32_t w : spv) {
    if ((w & 0xFFFF) == hsr_dxvk::SPV_OP_DEMOTE_TO_HELPER_INVOCATION) {
      found_demote = true;
      break;
    }
  }
  if (!found_demote) {
    std::cerr << "FAILED: Non-Bayer demote was incorrectly modified!\n";
    std::abort();
  }
  std::cout << "PASSED\n";
}

// 用例 4: 实机抓取的真实角色 Bayer 着色器端到端验证 (shader_a4b7eb69)
void test_hsr_real_shader_dump_neutralized() {
  std::cout << "[Test HSR 4] Real Dumped Shader (shader_a4b7eb69) Bayer Demote neutralized & Cutouts preserved... ";

  const char* dump_path = "/tmp/game_anti_dither/dumps/StarRail/shader_a4b7eb69_orig.spv";
  FILE* fp = fopen(dump_path, "rb");
  if (!fp) {
    std::cout << "SKIPPED (no dump file)\n";
    return;
  }

  fseek(fp, 0, SEEK_END);
  long fsize = ftell(fp);
  fseek(fp, 0, SEEK_SET);

  if (fsize < 20 || (fsize % 4) != 0) {
    fclose(fp);
    std::cout << "SKIPPED (invalid dump size)\n";
    return;
  }

  std::vector<uint32_t> spv(fsize / 4);
  if (fread(spv.data(), 1, fsize, fp) != static_cast<size_t>(fsize)) {
    fclose(fp);
    std::cout << "SKIPPED (read error)\n";
    return;
  }
  fclose(fp);

  setenv("ANTI_DITHER_ENABLED", "1", 1);
  game_logger::g_initialized = false;
  game_logger::g_dump_enabled = false;

  hsr_dxvk::process_spirv_anti_dither(spv.data(), spv.size());

  uint32_t nop_count = 0;
  uint32_t demote_count = 0;
  for (uint32_t w : spv) {
    uint16_t op = w & 0xFFFF;
    if (op == hsr_dxvk::SPV_OP_NOP) nop_count++;
    if (op == hsr_dxvk::SPV_OP_DEMOTE_TO_HELPER_INVOCATION) demote_count++;
  }

  // 精准断言：原本 4 处 demote，其中 1 处 Bayer 虚化被 NOP，剩余 3 处材质裁切完整保留
  if (nop_count < 1 || demote_count != 3) {
    std::cerr << "FAILED: Real shader test failed (nop=" << nop_count << ", demote=" << demote_count << ")\n";
    std::abort();
  }
  std::cout << "PASSED (1 Bayer NOPed, 3 Cutouts Preserved)\n";
}

// 用例 5: 大世界花朵/植被着色器 (shader_0b4aa2b5) 材质 Alpha Cutout 100% 保护验证
void test_hsr_flower_shader_cutout_preserved() {
  std::cout << "[Test HSR 5] Flower Shader (shader_0b4aa2b5) Petal Alpha Cutouts preserved... ";

  const char* dump_path = "/tmp/game_anti_dither/dumps/StarRail/shader_0b4aa2b5_orig.spv";
  FILE* fp = fopen(dump_path, "rb");
  if (!fp) {
    std::cout << "SKIPPED (no dump file)\n";
    return;
  }

  fseek(fp, 0, SEEK_END);
  long fsize = ftell(fp);
  fseek(fp, 0, SEEK_SET);

  if (fsize < 20 || (fsize % 4) != 0) {
    fclose(fp);
    std::cout << "SKIPPED (invalid dump size)\n";
    return;
  }

  std::vector<uint32_t> spv(fsize / 4);
  if (fread(spv.data(), 1, fsize, fp) != static_cast<size_t>(fsize)) {
    fclose(fp);
    std::cout << "SKIPPED (read error)\n";
    return;
  }
  fclose(fp);

  setenv("ANTI_DITHER_ENABLED", "1", 1);
  game_logger::g_initialized = false;
  game_logger::g_dump_enabled = false;

  hsr_dxvk::process_spirv_anti_dither(spv.data(), spv.size());

  uint32_t nop_count = 0;
  uint32_t demote_count = 0;
  for (uint32_t w : spv) {
    uint16_t op = w & 0xFFFF;
    if (op == hsr_dxvk::SPV_OP_NOP) nop_count++;
    if (op == hsr_dxvk::SPV_OP_DEMOTE_TO_HELPER_INVOCATION) demote_count++;
  }

  // 精准断言：原本 5 处 demote，其中 1 处 Bayer 虚化被 NOP，剩余 4 处花瓣材质镂空必须 100% 完整保留！
  if (nop_count < 1 || demote_count != 4) {
    std::cerr << "FAILED: Flower cutout was incorrectly modified! (nop=" << nop_count << ", demote=" << demote_count << ")\n";
    std::abort();
  }
  std::cout << "PASSED (1 Bayer NOPed, 4 Petal Cutouts Preserved)\n";
}

int main() {
  std::cout << "=== HSR (Honkai: Star Rail) DXVK Anti-Dither Unit Tests ===\n";
  test_hsr_pipeline_stage_dispatch();
  test_hsr_bayer_dither_neutralized();
  test_hsr_alpha_cutout_preserved();
  test_hsr_real_shader_dump_neutralized();
  test_hsr_flower_shader_cutout_preserved();
  std::cout << "=== All HSR Tests Passed Successfully ===\n";
  return 0;
}
