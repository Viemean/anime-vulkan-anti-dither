#include <cstdint>
#include <vector>
#include <iostream>
#include <cassert>
#include <cstdlib>
#include <fstream>
#include <filesystem>

#include "../src/logger.h"
#include "../src/tof/tof_anti_dither.h"
#include "../src/tof/tof_vkd3d.h"

namespace fs = std::filesystem;

static inline uint32_t make_op(uint16_t length, uint16_t opcode) {
  return (static_cast<uint32_t>(length) << 16) | opcode;
}

// 用例 1: 幻塔角色相机散斑噪声虚化（1000.0f 噪声）精确消除
void test_tof_character_noise_dither_neutralized() {
  std::cout << "[Test TOF 1] Character 1000.0f Noise Dither (Demote neutralized)... ";
  setenv("ANTI_DITHER_ENABLED", "1", 1);
  game_logger::g_initialized = false;
  game_logger::g_exclude_hashes.clear();
  game_logger::g_force_hashes.clear();

  tof_vkd3d::FloatUint f_noise;
  f_noise.f = 1000.0f;

  std::vector<uint32_t> spv = {
    tof_vkd3d::SPV_HEADER_MAGIC,
    0x00010300,
    30017 << 16, // VKD3D
    100, // Bound
    0,
    // OpDecorate %10 BuiltIn FragCoord
    make_op(4, tof_vkd3d::SPV_OP_DECORATE), 10, tof_vkd3d::SPV_DECORATION_BUILTIN, tof_vkd3d::SPV_BUILTIN_FRAG_COORD,
    // OpConstant %50 (1000.0f noise)
    make_op(4, tof_vkd3d::SPV_OP_CONSTANT), 1, 50, f_noise.u,
    // OpVariable ptr_Input %10
    make_op(4, tof_vkd3d::SPV_OP_VARIABLE), 1, 10, 1,
    // OpFunction
    make_op(5, tof_vkd3d::SPV_OP_FUNCTION), 2, 1, 0, 3,
    make_op(2, tof_vkd3d::SPV_OP_LABEL), 20,
    // OpLoad %60 %10 (FragCoord)
    make_op(4, tof_vkd3d::SPV_OP_LOAD), 1, 60, 10,
    // OpFMul %70 %60 %50 (Multiply by 1000.0f)
    make_op(5, 133), 1, 70, 60, 50,
    // OpFOrdLessThan %80 %70 %50
    make_op(5, 184), 6, 80, 70, 50,
    // OpBranchConditional %80 %30 %40
    make_op(4, tof_vkd3d::SPV_OP_BRANCH_CONDITIONAL), 80, 30, 40,
    // OpLabel %30
    make_op(2, tof_vkd3d::SPV_OP_LABEL), 30,
    // OpDemoteToHelperInvocation (应当被 NOP)
    make_op(1, tof_vkd3d::SPV_OP_DEMOTE_TO_HELPER_INVOCATION),
    // OpLabel %40
    make_op(2, tof_vkd3d::SPV_OP_LABEL), 40,
    make_op(1, 253),
    make_op(1, tof_vkd3d::SPV_OP_FUNCTION_END)
  };

  tof_vkd3d::process_spirv_anti_dither(spv.data(), spv.size());
  bool found_nop = false;
  for (uint32_t w : spv) {
    if ((w & 0xFFFF) == tof_vkd3d::SPV_OP_NOP) found_nop = true;
  }
  if (!found_nop) {
    std::cerr << "FAILED: TOF character noise dither was not neutralized!\n";
    std::abort();
  }
  std::cout << "PASSED\n";
}

// 用例 2: 大世界植被/树木 Alpha Cutout 100% 保护
void test_tof_tree_foliage_alpha_cutout_preserved() {
  std::cout << "[Test TOF 2] Tree Foliage Alpha Cutout with Texture Sample (Preserved 100%)... ";
  setenv("ANTI_DITHER_ENABLED", "1", 1);
  game_logger::g_initialized = false;
  game_logger::g_exclude_hashes.clear();
  game_logger::g_force_hashes.clear();

  tof_vkd3d::FloatUint f_cutoff;
  f_cutoff.f = 0.33333334f;

  std::vector<uint32_t> spv = {
    tof_vkd3d::SPV_HEADER_MAGIC,
    0x00010300,
    30017 << 16, // VKD3D
    100,
    0,
    // OpConstant %50
    make_op(4, tof_vkd3d::SPV_OP_CONSTANT), 1, 50, f_cutoff.u,
    // OpFunction
    make_op(5, tof_vkd3d::SPV_OP_FUNCTION), 2, 1, 0, 3,
    make_op(2, tof_vkd3d::SPV_OP_LABEL), 10,
    // OpImageSampleImplicitLod: type 4, res 15, sampled_img 2, coord 3
    make_op(5, 87), 4, 15, 2, 3,
    // OpFOrdLessThan %16 %15 %50 (Alpha Cutout condition)
    make_op(5, 184), 6, 16, 15, 50,
    // OpBranchConditional %16 %20 %30
    make_op(4, tof_vkd3d::SPV_OP_BRANCH_CONDITIONAL), 16, 20, 30,
    // OpLabel %20
    make_op(2, tof_vkd3d::SPV_OP_LABEL), 20,
    // OpDemoteToHelperInvocation (应当保留)
    make_op(1, tof_vkd3d::SPV_OP_DEMOTE_TO_HELPER_INVOCATION),
    // OpLabel %30
    make_op(2, tof_vkd3d::SPV_OP_LABEL), 30,
    make_op(1, 253),
    make_op(1, tof_vkd3d::SPV_OP_FUNCTION_END)
  };

  tof_vkd3d::process_spirv_anti_dither(spv.data(), spv.size());
  bool found_demote = false;
  for (uint32_t w : spv) {
    if ((w & 0xFFFF) == tof_vkd3d::SPV_OP_DEMOTE_TO_HELPER_INVOCATION) found_demote = true;
  }
  if (!found_demote) {
    std::cerr << "FAILED: TOF tree foliage alpha cutout was incorrectly neutralized!\n";
    std::abort();
  }
  std::cout << "PASSED\n";
}

// 用例 3: 真实 Dump 着色器基准对照组验证（若存在 dump 目录则触发）
void test_tof_real_dump_benchmarks() {
  std::string dir = "/tmp/game_anti_dither/dumps/QRSL";
  if (!fs::exists(dir)) {
    dir = "/tmp/game_anti_dither/dumps/QRSL.1";
  }
  if (!fs::exists(dir)) {
    std::cout << "[Test TOF 3] Real dumps directory not present, skipping benchmark.\n";
    return;
  }

  std::cout << "[Test TOF 3] Real dumps benchmark verification (Trees 100% preserved, Chars decoupled & neutralized)... ";

  std::vector<std::string> tree_hashes = {
    "ed19531a", "16085cf1", "25ed2683", "37ba139c", "5e175834",
    "10f1c650", "5ac65728", "7b728109", "c83871c7", "d1c17f49",
    "021240b6", "872b7ef7"
  };

  std::vector<std::string> char_hashes = {
    "1a903725", "32cfe674", "3f2a0c5d", "ebfb0abd", "d3fb8ba9"
  };

  for (const auto& h : tree_hashes) {
    std::string path = dir + "/shader_" + h + "_orig.spv";
    if (!fs::exists(path)) continue;
    std::ifstream f(path, std::ios::binary);
    std::vector<uint32_t> spv((fs::file_size(path)) / 4);
    f.read(reinterpret_cast<char*>(spv.data()), spv.size() * 4);

    uint32_t demotes_before = 0;
    for (size_t i = 5; i < spv.size(); ) {
      uint32_t w = spv[i];
      uint16_t op = w & 0xFFFF;
      uint16_t len = w >> 16;
      if (len == 0 || (i + len) > spv.size()) break;
      if (tof_vkd3d::is_discard_opcode(op)) demotes_before++;
      i += len;
    }

    tof_vkd3d::process_spirv_anti_dither(spv.data(), spv.size());

    uint32_t demotes_after = 0;
    for (size_t i = 5; i < spv.size(); ) {
      uint32_t w = spv[i];
      uint16_t op = w & 0xFFFF;
      uint16_t len = w >> 16;
      if (len == 0 || (i + len) > spv.size()) break;
      if (tof_vkd3d::is_discard_opcode(op)) demotes_after++;
      i += len;
    }

    if (demotes_after != demotes_before) {
      std::cerr << "FAILED: Real tree shader 0x" << h << " had demote incorrectly NOPed! before=" << demotes_before << " after=" << demotes_after << "\n";
      std::abort();
    }
  }

  for (const auto& h : char_hashes) {
    std::string path = dir + "/shader_" + h + "_orig.spv";
    if (!fs::exists(path)) continue;
    std::ifstream f(path, std::ios::binary);
    std::vector<uint32_t> orig_spv((fs::file_size(path)) / 4);
    f.read(reinterpret_cast<char*>(orig_spv.data()), orig_spv.size() * 4);

    std::vector<uint32_t> spv = orig_spv;
    tof_vkd3d::process_spirv_anti_dither(spv.data(), spv.size());

    bool modified = false;
    for (size_t k = 0; k < spv.size(); ++k) {
      if (spv[k] != orig_spv[k]) {
        modified = true;
        break;
      }
    }

    if (!modified) {
      std::cerr << "FAILED: Real char shader 0x" << h << " was neither uncoupled nor neutralized!\n";
      std::abort();
    }
  }

  std::cout << "PASSED\n";
}

// 用例 4: 幻塔 DXVK (DirectX 11) 角色点阵虚化消除 (OpKill + Bayer 向量)
void test_tof_dxvk_character_dither_neutralized() {
  std::cout << "[Test TOF 4] DXVK Character Bayer Dither (OpKill neutralized)... ";
  setenv("ANTI_DITHER_ENABLED", "1", 1);
  game_logger::g_initialized = false;
  game_logger::g_exclude_hashes.clear();
  game_logger::g_force_hashes.clear();

  tof_dxvk::FloatUint f_bayer1, f_bayer2, f_bayer3, f_bayer4;
  f_bayer1.f = 0.0f;
  f_bayer2.f = 0.33333334f;
  f_bayer3.f = 0.6666667f;
  f_bayer4.f = 0.8888889f;

  std::vector<uint32_t> spv = {
    tof_dxvk::SPV_HEADER_MAGIC,
    0x00010300,
    0, // DXVK Generator (not 30017)
    100, // Bound
    0,
    // OpName %10 "cb0" (DXVK signature)
    make_op(3, tof_dxvk::SPV_OP_NAME), 10, 0x00306263,
    // OpDecorate %12 BuiltIn FragCoord
    make_op(4, tof_dxvk::SPV_OP_DECORATE), 12, tof_dxvk::SPV_DECORATION_BUILTIN, tof_dxvk::SPV_BUILTIN_FRAG_COORD,
    // Bayer fractions
    make_op(4, tof_dxvk::SPV_OP_CONSTANT), 1, 51, f_bayer1.u,
    make_op(4, tof_dxvk::SPV_OP_CONSTANT), 1, 52, f_bayer2.u,
    make_op(4, tof_dxvk::SPV_OP_CONSTANT), 1, 53, f_bayer3.u,
    make_op(4, tof_dxvk::SPV_OP_CONSTANT), 1, 54, f_bayer4.u,
    // OpConstantComposite %55 (v4float packed Bayer)
    make_op(6, tof_dxvk::SPV_OP_CONSTANT_COMPOSITE), 2, 55, 51, 52, 53, 54,
    // OpVariable ptr_Input %12
    make_op(4, tof_dxvk::SPV_OP_VARIABLE), 1, 12, 1,
    // OpFunction
    make_op(5, tof_dxvk::SPV_OP_FUNCTION), 2, 1, 0, 3,
    make_op(2, tof_dxvk::SPV_OP_LABEL), 20,
    // OpLoad %60 %12 (FragCoord)
    make_op(4, tof_dxvk::SPV_OP_LOAD), 1, 60, 12,
    // OpCompositeExtract %61 %55 1 (Extract scalar Bayer from composite)
    make_op(5, tof_dxvk::SPV_OP_COMPOSITE_EXTRACT), 1, 61, 55, 1,
    // OpFMul %70 %60 %61
    make_op(5, 133), 1, 70, 60, 61,
    // OpFOrdLessThan %80 %70 %61
    make_op(5, 184), 6, 80, 70, 61,
    // OpBranchConditional %80 %30 %40
    make_op(4, tof_dxvk::SPV_OP_BRANCH_CONDITIONAL), 80, 30, 40,
    // OpLabel %30
    make_op(2, tof_dxvk::SPV_OP_LABEL), 30,
    // OpKill (应当被 NOP)
    make_op(1, tof_dxvk::SPV_OP_KILL),
    // OpLabel %40
    make_op(2, tof_dxvk::SPV_OP_LABEL), 40,
    make_op(1, 253),
    make_op(1, tof_dxvk::SPV_OP_FUNCTION_END)
  };

  tof_dxvk::process_spirv_anti_dither(spv.data(), spv.size());
  bool found_nop = false;
  for (uint32_t w : spv) {
    if ((w & 0xFFFF) == tof_dxvk::SPV_OP_NOP) found_nop = true;
  }
  if (!found_nop) {
    std::cerr << "FAILED: TOF DXVK character Bayer dither was not neutralized!\n";
    std::abort();
  }
  std::cout << "PASSED\n";
}

// 用例 5: 幻塔 DXVK 大世界植被/树叶 Alpha Cutout 100% 保护 (OpKill 绝对保留)
void test_tof_dxvk_tree_cutout_preserved() {
  std::cout << "[Test TOF 5] DXVK Tree Foliage Alpha Cutout with Texture Sample (Preserved 100%)... ";
  setenv("ANTI_DITHER_ENABLED", "1", 1);
  game_logger::g_initialized = false;
  game_logger::g_exclude_hashes.clear();
  game_logger::g_force_hashes.clear();

  tof_dxvk::FloatUint f_cutoff;
  f_cutoff.f = 0.33333334f;

  std::vector<uint32_t> spv = {
    tof_dxvk::SPV_HEADER_MAGIC,
    0x00010300,
    0, // DXVK Generator
    100,
    0,
    // OpName %10 "cb0"
    make_op(3, tof_dxvk::SPV_OP_NAME), 10, 0x00306263,
    // OpConstant %50
    make_op(4, tof_dxvk::SPV_OP_CONSTANT), 1, 50, f_cutoff.u,
    // OpFunction
    make_op(5, tof_dxvk::SPV_OP_FUNCTION), 2, 1, 0, 3,
    make_op(2, tof_dxvk::SPV_OP_LABEL), 10,
    // OpImageSampleImplicitLod: type 4, res 15, sampled_img 2, coord 3
    make_op(5, 87), 4, 15, 2, 3,
    // OpFOrdLessThan %16 %15 %50 (Alpha Cutout condition)
    make_op(5, 184), 6, 16, 15, 50,
    // OpBranchConditional %16 %20 %30
    make_op(4, tof_dxvk::SPV_OP_BRANCH_CONDITIONAL), 16, 20, 30,
    // OpLabel %20
    make_op(2, tof_dxvk::SPV_OP_LABEL), 20,
    // OpKill (应当保留，绝不能被 NOP)
    make_op(1, tof_dxvk::SPV_OP_KILL),
    // OpLabel %30
    make_op(2, tof_dxvk::SPV_OP_LABEL), 30,
    make_op(1, 253),
    make_op(1, tof_dxvk::SPV_OP_FUNCTION_END)
  };

  tof_dxvk::process_spirv_anti_dither(spv.data(), spv.size());
  bool found_kill = false;
  for (uint32_t w : spv) {
    if ((w & 0xFFFF) == tof_dxvk::SPV_OP_KILL) found_kill = true;
  }
  if (!found_kill) {
    std::cerr << "FAILED: TOF DXVK tree foliage alpha cutout was incorrectly neutralized!\n";
    std::abort();
  }
  std::cout << "PASSED\n";
}

// 用例 6: 统一入口双后端分发路由测试
void test_tof_layer_dispatch() {
  std::cout << "[Test TOF 6] Unified tof_layer::process_spirv_anti_dither dispatching... ";
  setenv("ANTI_DITHER_ENABLED", "1", 1);
  game_logger::g_initialized = false;
  game_logger::g_exclude_hashes.clear();
  game_logger::g_force_hashes.clear();

  tof_dxvk::FloatUint f_noise;
  f_noise.f = 1000.0f;

  // 构造带有 Fragment Entry Point 的 DXVK 着色器
  std::vector<uint32_t> dxvk_spv = {
    tof_dxvk::SPV_HEADER_MAGIC,
    0x00010300,
    0, // DXVK
    100,
    0,
    // OpEntryPoint Fragment %1 "main"
    make_op(4, 15), 4, 1, 0x6e69616d,
    // OpName %10 "cb0"
    make_op(3, tof_dxvk::SPV_OP_NAME), 10, 0x00306263,
    // OpDecorate %12 BuiltIn FragCoord
    make_op(4, tof_dxvk::SPV_OP_DECORATE), 12, tof_dxvk::SPV_DECORATION_BUILTIN, tof_dxvk::SPV_BUILTIN_FRAG_COORD,
    // OpConstant %50 (1000.0f noise)
    make_op(4, tof_dxvk::SPV_OP_CONSTANT), 1, 50, f_noise.u,
    make_op(4, tof_dxvk::SPV_OP_VARIABLE), 1, 12, 1,
    make_op(5, tof_dxvk::SPV_OP_FUNCTION), 2, 1, 0, 3,
    make_op(2, tof_dxvk::SPV_OP_LABEL), 20,
    make_op(4, tof_dxvk::SPV_OP_LOAD), 1, 60, 12,
    make_op(5, 133), 1, 70, 60, 50,
    make_op(5, 184), 6, 80, 70, 50,
    make_op(4, tof_dxvk::SPV_OP_BRANCH_CONDITIONAL), 80, 30, 40,
    make_op(2, tof_dxvk::SPV_OP_LABEL), 30,
    make_op(1, tof_dxvk::SPV_OP_KILL),
    make_op(2, tof_dxvk::SPV_OP_LABEL), 40,
    make_op(1, 253),
    make_op(1, tof_dxvk::SPV_OP_FUNCTION_END)
  };

  tof_layer::process_spirv_anti_dither(dxvk_spv.data(), dxvk_spv.size());
  bool found_nop = false;
  for (uint32_t w : dxvk_spv) {
    if ((w & 0xFFFF) == tof_dxvk::SPV_OP_NOP) found_nop = true;
  }
  if (!found_nop) {
    std::cerr << "FAILED: TOF layer unified dispatch failed to process DXVK shader!\n";
    std::abort();
  }
  std::cout << "PASSED\n";
}

int main() {
  test_tof_character_noise_dither_neutralized();
  test_tof_tree_foliage_alpha_cutout_preserved();
  test_tof_real_dump_benchmarks();
  test_tof_dxvk_character_dither_neutralized();
  test_tof_dxvk_tree_cutout_preserved();
  test_tof_layer_dispatch();
  std::cout << "All TOF anti-dither unit tests passed successfully!\n";
  return 0;
}
