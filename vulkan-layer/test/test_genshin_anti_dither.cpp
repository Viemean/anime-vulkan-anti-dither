#include <cstdint>
#include <vector>
#include <iostream>
#include <cassert>
#include <cstdlib>

#include "../src/logger.h"
#include "../src/genshin/genshin_anti_dither.h"
#include "../src/genshin/genshin_dxvk.h"

static inline uint32_t make_op(uint16_t length, uint16_t opcode) {
  return (static_cast<uint32_t>(length) << 16) | opcode;
}

// 用例 1: 原神管线着色器阶段分发判定 (Vertex vs Fragment)
void test_genshin_pipeline_stage_dispatch() {
  std::cout << "[Test Genshin 1] Stage detection (Fragment vs Vertex vs Compute)... ";

  // 构造 Fragment Shader (OpEntryPoint Fragment %4 "main")
  std::vector<uint32_t> ps_spv = {
    genshin_dxvk::SPV_HEADER_MAGIC,
    0x00010300,
    0, // Generator (DXVK)
    50,
    0,
    make_op(4, 15 /* OpEntryPoint */), 4, 4, 0x6e69616d // ExecutionModel = 4 (Fragment)
  };

  auto ps_stage = genshin_layer::detect_shader_stage(ps_spv.data(), ps_spv.size());
  (void)ps_stage;
  assert(ps_stage == genshin_layer::ShaderStage::Fragment);

  // 构造 Vertex Shader (OpEntryPoint Vertex %4 "main")
  std::vector<uint32_t> vs_spv = {
    genshin_dxvk::SPV_HEADER_MAGIC,
    0x00010300,
    0,
    50,
    0,
    make_op(4, 15 /* OpEntryPoint */), 0, 4, 0x6e69616d // ExecutionModel = 0 (Vertex)
  };

  auto vs_stage = genshin_layer::detect_shader_stage(vs_spv.data(), vs_spv.size());
  (void)vs_stage;
  assert(vs_stage == genshin_layer::ShaderStage::Vertex);

  std::cout << "PASSED\n";
}

// 用例 2: 原神角色 Bayer 4x4 网点虚化精准中和
void test_genshin_bayer_dither_neutralized() {
  std::cout << "[Test Genshin 2] Character 4x4 Bayer Dither (Demote neutralized)... ";

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
    genshin_dxvk::SPV_HEADER_MAGIC,
    0x00010300,
    0, // Generator
    100, // Bound
    0,
    // OpDecorate %10 BuiltIn FragCoord
    make_op(4, 71 /* OpDecorate */), 10, genshin_dxvk::SPV_DECORATION_BUILTIN, genshin_dxvk::SPV_BUILTIN_FRAG_COORD,
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
    make_op(1, genshin_dxvk::SPV_OP_DEMOTE_TO_HELPER_INVOCATION),
    // OpLabel %40
    make_op(2, 248 /* OpLabel */), 40,
    make_op(1, 253 /* OpReturn */),
    make_op(1, 56 /* OpFunctionEnd */)
  };

  genshin_dxvk::process_spirv_anti_dither(spv.data(), spv.size());

  bool found_nop = false;
  for (uint32_t w : spv) {
    if ((w & 0xFFFF) == genshin_dxvk::SPV_OP_NOP) {
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
void test_genshin_alpha_cutout_preserved() {
  std::cout << "[Test Genshin 3] Alpha Cutout without Bayer constant (Preserved 100%)... ";

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
    genshin_dxvk::SPV_HEADER_MAGIC,
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
    make_op(1, genshin_dxvk::SPV_OP_DEMOTE_TO_HELPER_INVOCATION),
    // OpLabel %40
    make_op(2, 248 /* OpLabel */), 40,
    make_op(1, 253 /* OpReturn */),
    make_op(1, 56 /* OpFunctionEnd */)
  };

  genshin_dxvk::process_spirv_anti_dither(spv.data(), spv.size());

  bool found_demote = false;
  for (uint32_t w : spv) {
    if ((w & 0xFFFF) == genshin_dxvk::SPV_OP_DEMOTE_TO_HELPER_INVOCATION) {
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

// 用例 4: 顶点着色器 -99.0f 几何坍缩中和
void test_genshin_vertex_shader_collapse_neutralized() {
  std::cout << "[Test Genshin 4] Vertex Shader -99.0f Geometry Collapse Neutralized... ";

  setenv("ANTI_DITHER_ENABLED", "1", 1);
  game_logger::g_initialized = false;

  std::vector<uint32_t> spv = {
    genshin_dxvk::SPV_HEADER_MAGIC,
    0x00010300,
    0,
    100, // bound
    0,
    // OpConstant %10 (-99.0f)
    make_op(4, 43 /* OpConstant */), 1, 10, 0xC2C60000,
    // OpConstant %11 (1.0f)
    make_op(4, 43 /* OpConstant */), 1, 11, 0x3F800000,
    // OpFunction
    make_op(5, 54 /* OpFunction */), 2, 1, 0, 3,
    make_op(2, 248 /* OpLabel */), 20,
    // OpSelect %30 (cond=%50, true=%21, false=%10[-99]) -> 坍缩侧是 false
    make_op(6, 169 /* OpSelect */), 1, 30, 50, 21, 10,
    // OpSelect %31 (cond=%50, true=%22, false=%11[1.0]) -> 同条件 W 分量
    make_op(6, 169 /* OpSelect */), 1, 31, 50, 22, 11,
    make_op(1, 253 /* OpReturn */),
    make_op(1, 56 /* OpFunctionEnd */)
  };

  genshin_dxvk::process_vertex_shader(spv.data(), spv.size());

  // 验证 %30 的 false_val 是否被替换为 true_val (%21)
  assert(spv[15] == 21);
  // 验证 %31 的 false_val 是否被替换为 true_val (%22)
  assert(spv[21] == 22);

  std::cout << "PASSED\n";
}

int main() {
  std::cout << "=== Genshin Impact DXVK Anti-Dither Unit Tests ===\n";
  test_genshin_pipeline_stage_dispatch();
  test_genshin_bayer_dither_neutralized();
  test_genshin_alpha_cutout_preserved();
  test_genshin_vertex_shader_collapse_neutralized();
  std::cout << "=== All Genshin Tests Passed Successfully ===\n";
  return 0;
}
