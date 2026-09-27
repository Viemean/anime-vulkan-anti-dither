#include <cstdint>
#include <vector>
#include <iostream>
#include <cassert>
#include <cstdlib>

#include "../src/logger.h"
#include "../src/gf2/gf2_anti_dither.h"
#include "../src/gf2/gf2_dxvk.h"

static inline uint32_t make_op(uint16_t length, uint16_t opcode) {
  return (static_cast<uint32_t>(length) << 16) | opcode;
}

// 用例 1: 少前2阶段检测 (Fragment vs Vertex)
void test_gf2_pipeline_stage_dispatch() {
  std::cout << "[Test GF2 1] Stage detection (Fragment vs Vertex)... ";

  std::vector<uint32_t> ps_spv = {
    gf2_dxvk::SPV_HEADER_MAGIC,
    0x00010300,
    0,
    50,
    0,
    make_op(4, 15 /* OpEntryPoint */), 4, 4, 0x6e69616d // ExecutionModel = 4 (Fragment)
  };

  auto ps_stage = gf2_layer::detect_shader_stage(ps_spv.data(), ps_spv.size());
  (void)ps_stage;
  assert(ps_stage == gf2_layer::ShaderStage::Fragment);

  std::vector<uint32_t> vs_spv = {
    gf2_dxvk::SPV_HEADER_MAGIC,
    0x00010300,
    0,
    50,
    0,
    make_op(4, 15 /* OpEntryPoint */), 0, 4, 0x6e69616d // ExecutionModel = 0 (Vertex)
  };

  auto vs_stage = gf2_layer::detect_shader_stage(vs_spv.data(), vs_spv.size());
  (void)vs_stage;
  assert(vs_stage == gf2_layer::ShaderStage::Vertex);

  std::cout << "PASSED\n";
}

// 用例 2: 角色 Bayer 4x4 网格近身虚化消隐中和 (OpKill -> 分支重定向)
void test_gf2_character_bayer_dither_neutralized() {
  std::cout << "[Test GF2 2] Character Bayer Dither (OpKill branch redirected)... ";

  setenv("ANTI_DITHER_ENABLED", "1", 1);
  game_logger::g_initialized = false;
  game_logger::g_exclude_hashes.clear();
  game_logger::g_force_hashes.clear();

  union { float f; uint32_t u; } f17;
  f17.f = 17.0f;

  std::vector<uint32_t> spv = {
    gf2_dxvk::SPV_HEADER_MAGIC,
    0x00010300,
    0,
    100, // Bound
    0,
    // OpDecorate %10 BuiltIn FragCoord
    make_op(4, 71 /* OpDecorate */), 10, gf2_dxvk::SPV_DECORATION_BUILTIN, gf2_dxvk::SPV_BUILTIN_FRAG_COORD,
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
    // OpKill
    make_op(1, gf2_dxvk::SPV_OP_KILL),
    // OpLabel %40
    make_op(2, 248 /* OpLabel */), 40,
    make_op(1, 253 /* OpReturn */),
    make_op(1, 56 /* OpFunctionEnd */)
  };

  gf2_dxvk::process_spirv_anti_dither(spv.data(), spv.size());

  // 验证 OpBranchConditional 的 true_label 是否从 %30 被重定向到了 %40 (绕过 OpKill 块，CFG 保持合法)
  bool branch_redirected = false;
  for (size_t i = 5; i < spv.size(); ) {
    uint32_t w = spv[i];
    uint16_t op = w & 0xFFFF;
    uint16_t len = (w >> 16) & 0xFFFF;
    if (len == 0 || (i + len) > spv.size()) break;

    if (op == 250 /* OpBranchConditional */) {
      if (spv[i + 2] == 40 && spv[i + 3] == 40) {
        branch_redirected = true;
      }
    }
    i += len;
  }

  (void)branch_redirected;
  assert(branch_redirected);
  std::cout << "PASSED\n";
}

// 用例 3: 角色 OpDemoteToHelperInvocation 点阵虚化消隐中和 (置 NOP)
void test_gf2_character_demote_dither_neutralized() {
  std::cout << "[Test GF2 3] Character Demote Dither (OpDemote neutralized to OpNop)... ";

  setenv("ANTI_DITHER_ENABLED", "1", 1);
  game_logger::g_initialized = false;
  game_logger::g_exclude_hashes.clear();
  game_logger::g_force_hashes.clear();

  union { float f; uint32_t u; } f16;
  f16.f = 0.0625f; // 1/16.0f Bayer

  std::vector<uint32_t> spv = {
    gf2_dxvk::SPV_HEADER_MAGIC,
    0x00010300,
    0,
    100, // Bound
    0,
    make_op(4, 71 /* OpDecorate */), 10, gf2_dxvk::SPV_DECORATION_BUILTIN, gf2_dxvk::SPV_BUILTIN_FRAG_COORD,
    make_op(4, 43 /* OpConstant */), 1, 50, f16.u,
    make_op(4, 59 /* OpVariable */), 1, 10, 1,
    make_op(5, 54 /* OpFunction */), 2, 1, 0, 3,
    make_op(2, 248 /* OpLabel */), 20,
    make_op(4, 61 /* OpLoad */), 1, 60, 10,
    make_op(5, 148 /* OpDot */), 1, 70, 60, 50,
    make_op(5, 184 /* OpFOrdLessThan */), 6, 80, 70, 50,
    make_op(4, 250 /* OpBranchConditional */), 80, 30, 40,
    make_op(2, 248 /* OpLabel */), 30,
    // OpDemoteToHelperInvocation (应当被置为 OpNop)
    make_op(1, gf2_dxvk::SPV_OP_DEMOTE_TO_HELPER_INVOCATION),
    make_op(1, 249 /* OpBranch */), 40,
    make_op(2, 248 /* OpLabel */), 40,
    make_op(1, 253 /* OpReturn */),
    make_op(1, 56 /* OpFunctionEnd */)
  };

  gf2_dxvk::process_spirv_anti_dither(spv.data(), spv.size());

  // 验证 Demote 是否被置为 NOP
  bool demote_found = false;
  for (size_t i = 5; i < spv.size(); ) {
    uint32_t w = spv[i];
    uint16_t op = w & 0xFFFF;
    uint16_t len = (w >> 16) & 0xFFFF;
    if (len == 0 || (i + len) > spv.size()) break;

    if (op == gf2_dxvk::SPV_OP_DEMOTE_TO_HELPER_INVOCATION) {
      demote_found = true;
    }
    i += len;
  }

  (void)demote_found;
  assert(!demote_found);
  std::cout << "PASSED\n";
}

// 用例 4: 角色 IGN 噪波虚化消除
void test_gf2_character_ign_noise_neutralized() {
  std::cout << "[Test GF2 4] Character IGN Noise Dither (OpKill branch redirected)... ";

  setenv("ANTI_DITHER_ENABLED", "1", 1);
  game_logger::g_initialized = false;
  game_logger::g_exclude_hashes.clear();
  game_logger::g_force_hashes.clear();

  union { float f; uint32_t u; } f_ign;
  f_ign.f = 52.9829189f; // IGN 常数

  std::vector<uint32_t> spv = {
    gf2_dxvk::SPV_HEADER_MAGIC,
    0x00010300,
    0,
    100, // Bound
    0,
    make_op(4, 71 /* OpDecorate */), 10, gf2_dxvk::SPV_DECORATION_BUILTIN, gf2_dxvk::SPV_BUILTIN_FRAG_COORD,
    make_op(4, 43 /* OpConstant */), 1, 50, f_ign.u,
    make_op(4, 59 /* OpVariable */), 1, 10, 1,
    make_op(5, 54 /* OpFunction */), 2, 1, 0, 3,
    make_op(2, 248 /* OpLabel */), 20,
    make_op(4, 61 /* OpLoad */), 1, 60, 10,
    make_op(5, 148 /* OpDot */), 1, 70, 60, 50,
    make_op(5, 184 /* OpFOrdLessThan */), 6, 80, 70, 50,
    make_op(4, 250 /* OpBranchConditional */), 80, 30, 40,
    make_op(2, 248 /* OpLabel */), 30,
    make_op(1, gf2_dxvk::SPV_OP_KILL),
    make_op(2, 248 /* OpLabel */), 40,
    make_op(1, 253 /* OpReturn */),
    make_op(1, 56 /* OpFunctionEnd */)
  };

  gf2_dxvk::process_spirv_anti_dither(spv.data(), spv.size());

  bool branch_redirected = false;
  for (size_t i = 5; i < spv.size(); ) {
    uint32_t w = spv[i];
    uint16_t op = w & 0xFFFF;
    uint16_t len = (w >> 16) & 0xFFFF;
    if (len == 0 || (i + len) > spv.size()) break;

    if (op == 250 /* OpBranchConditional */) {
      if (spv[i + 2] == 40 && spv[i + 3] == 40) {
        branch_redirected = true;
      }
    }
    i += len;
  }

  (void)branch_redirected;
  assert(branch_redirected);
  std::cout << "PASSED\n";
}

// 用例 5: 树木植被 / 铁丝网 Alpha Cutout 保护 (依赖采样，丢弃条件 100% 完整保留)
void test_gf2_texture_alpha_cutout_preserved() {
  std::cout << "[Test GF2 5] Foliage / Fence Alpha Cutout (strictly preserved)... ";

  setenv("ANTI_DITHER_ENABLED", "1", 1);
  game_logger::g_initialized = false;
  game_logger::g_exclude_hashes.clear();
  game_logger::g_force_hashes.clear();

  union { float f; uint32_t u; } f17;
  f17.f = 17.0f;

  std::vector<uint32_t> spv = {
    gf2_dxvk::SPV_HEADER_MAGIC,
    0x00010300,
    0,
    100, // Bound
    0,
    make_op(4, 71 /* OpDecorate */), 10, gf2_dxvk::SPV_DECORATION_BUILTIN, gf2_dxvk::SPV_BUILTIN_FRAG_COORD,
    make_op(4, 43 /* OpConstant */), 1, 50, f17.u,
    make_op(4, 59 /* OpVariable */), 1, 10, 1,
    make_op(5, 54 /* OpFunction */), 2, 1, 0, 3,
    make_op(2, 248 /* OpLabel */), 20,
    // 模拟纹理采样指令 (OpImageSampleImplicitLod = 87)
    // %65 = OpImageSampleImplicitLod %type %img %uv
    make_op(5, 87 /* OpImageSampleImplicitLod */), 1, 65, 12, 13,
    // %80 = OpFOrdLessThan %bool %65 %50
    make_op(5, 184 /* OpFOrdLessThan */), 6, 80, 65, 50,
    // OpBranchConditional %80 %30 %40
    make_op(4, 250 /* OpBranchConditional */), 80, 30, 40,
    make_op(2, 248 /* OpLabel */), 30,
    make_op(1, gf2_dxvk::SPV_OP_KILL),
    make_op(2, 248 /* OpLabel */), 40,
    make_op(1, 253 /* OpReturn */),
    make_op(1, 56 /* OpFunctionEnd */)
  };

  gf2_dxvk::process_spirv_anti_dither(spv.data(), spv.size());

  // 验证 OpBranchConditional 的 true_label 仍保持为 %30 (未被重定向，正常镂空裁剪完整保留)
  bool preserved = false;
  for (size_t i = 5; i < spv.size(); ) {
    uint32_t w = spv[i];
    uint16_t op = w & 0xFFFF;
    uint16_t len = (w >> 16) & 0xFFFF;
    if (len == 0 || (i + len) > spv.size()) break;

    if (op == 250 /* OpBranchConditional */) {
      if (spv[i + 2] == 30 && spv[i + 3] == 40) {
        preserved = true;
      }
    }
    i += len;
  }

  (void)preserved;
  assert(preserved);
  std::cout << "PASSED\n";
}

// 用例 6: 角色身体 / 衣服 Uniform NMin 动态相机淡出阈值中和
void test_gf2_character_body_nmin_fade_neutralized() {
  std::cout << "[Test GF2 6] Character Body/Clothes NMin Uniform Fade (neutralized)... ";

  setenv("ANTI_DITHER_ENABLED", "1", 1);
  game_logger::g_initialized = false;
  game_logger::g_exclude_hashes.clear();
  game_logger::g_force_hashes.clear();

  std::vector<uint32_t> spv = {
    gf2_dxvk::SPV_HEADER_MAGIC,
    0x00010300,
    0,
    100, // Bound
    0,
    // OpName %10 "cb1"
    make_op(3, 5 /* OpName */), 10, 0x00316263,
    make_op(4, 59 /* OpVariable */), 1, 10, 1,
    make_op(5, 54 /* OpFunction */), 2, 1, 0, 3,
    make_op(2, 248 /* OpLabel */), 20,
    // %11 = OpAccessChain %10
    make_op(4, 65 /* OpAccessChain */), 1, 11, 10,
    // %12 = OpLoad %11
    make_op(4, 61 /* OpLoad */), 1, 12, 11,
    // %13 = OpAccessChain %10
    make_op(4, 65 /* OpAccessChain */), 1, 13, 10,
    // %14 = OpLoad %13
    make_op(4, 61 /* OpLoad */), 1, 14, 13,
    // %15 = OpExtInst NMin (79) %12 %14
    make_op(7, 12 /* OpExtInst */), 1, 15, 1, 79, 12, 14,
    // %16 = OpFSub %alpha %15
    make_op(5, 131 /* OpFSub */), 1, 16, 50, 15,
    // %17 = OpFOrdLessThan %16 0
    make_op(5, 184 /* OpFOrdLessThan */), 6, 17, 16, 60,
    make_op(4, 250 /* OpBranchConditional */), 17, 30, 40,
    make_op(2, 248 /* OpLabel */), 30,
    // OpDemoteToHelperInvocation
    make_op(1, gf2_dxvk::SPV_OP_DEMOTE_TO_HELPER_INVOCATION),
    make_op(2, 248 /* OpLabel */), 40,
    make_op(1, 253 /* OpReturn */),
    make_op(1, 56 /* OpFunctionEnd */)
  };

  gf2_dxvk::process_spirv_anti_dither(spv.data(), spv.size());

  // 验证 Demote 是否被置为 NOP
  bool demote_found = false;
  for (size_t i = 5; i < spv.size(); ) {
    uint32_t w = spv[i];
    uint16_t op = w & 0xFFFF;
    uint16_t len = (w >> 16) & 0xFFFF;
    if (len == 0 || (i + len) > spv.size()) break;

    if (op == gf2_dxvk::SPV_OP_DEMOTE_TO_HELPER_INVOCATION) {
      demote_found = true;
    }
    i += len;
  }

  (void)demote_found;
  assert(!demote_found);
  std::cout << "PASSED\n";
}

// 用例 7: 真实游戏 Dump 着色器验证 (身体、帽子、衣物消除，环境植被 100% 保护)
#include <fstream>

static std::vector<uint32_t> load_spv_file(const std::string& path) {
  std::ifstream f(path, std::ios::binary | std::ios::ate);
  if (!f.is_open()) return {};
  std::streamsize size = f.tellg();
  f.seekg(0, std::ios::beg);
  std::vector<uint32_t> buffer(size / sizeof(uint32_t));
  if (f.read(reinterpret_cast<char*>(buffer.data()), size)) {
    return buffer;
  }
  return {};
}

void test_gf2_real_dumped_shaders() {
  std::cout << "[Test GF2 7] Real Dumped Shaders (Body/Hat Neutralized, Foliage Preserved)... ";

  setenv("ANTI_DITHER_ENABLED", "1", 1);
  game_logger::g_initialized = false;
  game_logger::g_exclude_hashes.clear();
  game_logger::g_force_hashes.clear();

  // 1. 验证角色着色器 (身体、衣物、帽子、长发与面部)
  const std::vector<std::string> character_shaders = {
    "/tmp/game_anti_dither/dumps/GF2_Exilium/shader_df632a08_orig.spv",
    "/tmp/game_anti_dither/dumps/GF2_Exilium/shader_cb71d78c_orig.spv",
    "/tmp/game_anti_dither/dumps/GF2_Exilium/shader_5aaedcf6_orig.spv",
    "/tmp/game_anti_dither/dumps/GF2_Exilium/shader_b81e8680_orig.spv",
    "/tmp/game_anti_dither/dumps/GF2_Exilium/shader_f15414fb_orig.spv"
  };

  for (const auto& spv_path : character_shaders) {
    auto code = load_spv_file(spv_path);
    if (code.empty()) continue;

    size_t orig_demotes = 0;
    for (size_t i = 5; i < code.size(); ) {
      uint32_t w = code[i];
      uint16_t op = w & 0xFFFF;
      uint16_t len = (w >> 16) & 0xFFFF;
      if (len == 0 || (i + len) > code.size()) break;
      if (op == gf2_dxvk::SPV_OP_DEMOTE_TO_HELPER_INVOCATION || op == 5380) orig_demotes++;
      i += len;
    }

    gf2_dxvk::process_spirv_anti_dither(code.data(), code.size());

    // 验证其中的近身消隐 Demote 已被有效中和
    size_t remain_demotes = 0;
    for (size_t i = 5; i < code.size(); ) {
      uint32_t w = code[i];
      uint16_t op = w & 0xFFFF;
      uint16_t len = (w >> 16) & 0xFFFF;
      if (len == 0 || (i + len) > code.size()) break;
      if (op == gf2_dxvk::SPV_OP_DEMOTE_TO_HELPER_INVOCATION || op == 5380) remain_demotes++;
      i += len;
    }
    (void)orig_demotes; (void)remain_demotes;
    assert(remain_demotes < orig_demotes && "Character shader must neutralize near-camera fade demote!");
  }

  // 2. 验证环境与植物树木着色器 (包括地面小草，以及高面数的大树、盆栽棕榈叶片等)
  const std::vector<std::string> foliage_shaders = {
    "/tmp/game_anti_dither/dumps/GF2_Exilium/shader_e60c0263_orig.spv",
    "/tmp/game_anti_dither/dumps/GF2_Exilium/shader_479860b5_orig.spv",
    "/tmp/game_anti_dither/dumps/GF2_Exilium/shader_0d8e27cd_orig.spv",
    "/tmp/game_anti_dither/dumps/GF2_Exilium/shader_705cb07f_orig.spv",
    "/tmp/game_anti_dither/dumps/GF2_Exilium/shader_1cd48a26_orig.spv",
    "/tmp/game_anti_dither/dumps/GF2_Exilium/shader_008c2ad9_orig.spv",
    "/tmp/game_anti_dither/dumps/GF2_Exilium/shader_6bc8dc52_orig.spv",
    "/tmp/game_anti_dither/dumps/GF2_Exilium/shader_d41324a4_orig.spv",
    "/tmp/game_anti_dither/dumps/GF2_Exilium/shader_d4bfc247_orig.spv",
    "/tmp/game_anti_dither/dumps/GF2_Exilium/shader_8e576181_orig.spv"
  };

  for (const auto& spv_path : foliage_shaders) {
    auto code = load_spv_file(spv_path);
    if (code.empty()) continue;

    size_t orig_demotes = 0;
    for (size_t i = 5; i < code.size(); ) {
      uint32_t w = code[i];
      uint16_t op = w & 0xFFFF;
      uint16_t len = (w >> 16) & 0xFFFF;
      if (len == 0 || (i + len) > code.size()) break;
      if (op == gf2_dxvk::SPV_OP_DEMOTE_TO_HELPER_INVOCATION || op == 5380) orig_demotes++;
      i += len;
    }

    gf2_dxvk::process_spirv_anti_dither(code.data(), code.size());

    // 验证环境与植物树木着色器中的 OpDemoteToHelperInvocation 100% 完整保留 (绝不允许被修改为NOP)
    size_t remain_demotes = 0;
    for (size_t i = 5; i < code.size(); ) {
      uint32_t w = code[i];
      uint16_t op = w & 0xFFFF;
      uint16_t len = (w >> 16) & 0xFFFF;
      if (len == 0 || (i + len) > code.size()) break;
      if (op == gf2_dxvk::SPV_OP_DEMOTE_TO_HELPER_INVOCATION || op == 5380) remain_demotes++;
      i += len;
    }
    (void)orig_demotes; (void)remain_demotes;
    assert(remain_demotes == orig_demotes && "Environment foliage cutout must be 100% strictly preserved!");
  }

  std::cout << "PASSED\n";
}

void test_gf2_stripped_debug_info_fallback() {
  std::cout << "[Test GF2 8] Stripped Debug Info Fallback (OpName stripped, cb2 via Binding)... ";

  const std::string spv_path = "/tmp/game_anti_dither/dumps/GF2_Exilium/shader_df632a08_orig.spv";
  auto code = load_spv_file(spv_path);
  if (code.empty()) {
    std::cout << "SKIPPED (dump file not found)\n";
    return;
  }

  // 模拟 spirv-opt --strip-debug: 将所有 OpName (5) 和 OpMemberName (6) 抹除为 OpNop (0)
  size_t orig_demotes = 0;
  for (size_t i = 5; i < code.size(); ) {
    uint32_t w = code[i];
    uint16_t op = w & 0xFFFF;
    uint16_t len = (w >> 16) & 0xFFFF;
    if (len == 0 || (i + len) > code.size()) break;
    if (op == 5 /* OpName */ || op == 6 /* OpMemberName */) {
      for (size_t k = 0; k < len; ++k) {
        code[i + k] = 0; // OpNop
      }
    } else if (op == gf2_dxvk::SPV_OP_DEMOTE_TO_HELPER_INVOCATION || op == 5380) {
      orig_demotes++;
    }
    i += len;
  }

  // 在完全没有 OpName 的情况下处理
  gf2_dxvk::process_spirv_anti_dither(code.data(), code.size());

  size_t remain_demotes = 0;
  for (size_t i = 5; i < code.size(); ) {
    uint32_t w = code[i];
    uint16_t op = w & 0xFFFF;
    uint16_t len = (w >> 16) & 0xFFFF;
    if (len == 0 || (i + len) > code.size()) break;
    if (op == gf2_dxvk::SPV_OP_DEMOTE_TO_HELPER_INVOCATION || op == 5380) remain_demotes++;
    i += len;
  }
  (void)orig_demotes;
  (void)remain_demotes;
  assert(remain_demotes < orig_demotes && "Stripped shader must successfully fallback to Binding order to neutralize cb2 fade!");
  std::cout << "PASSED\n";
}

int main() {
  std::cout << "========================================\n";
  std::cout << "Running GF2 (少女前线2：追放) Anti-Dither Tests\n";
  std::cout << "========================================\n";

  test_gf2_pipeline_stage_dispatch();
  test_gf2_character_bayer_dither_neutralized();
  test_gf2_character_demote_dither_neutralized();
  test_gf2_character_ign_noise_neutralized();
  test_gf2_character_body_nmin_fade_neutralized();
  test_gf2_texture_alpha_cutout_preserved();
  test_gf2_real_dumped_shaders();
  test_gf2_stripped_debug_info_fallback();

  std::cout << "========================================\n";
  std::cout << "All GF2 Anti-Dither Tests PASSED successfully!\n";
  std::cout << "========================================\n";
  return 0;
}
