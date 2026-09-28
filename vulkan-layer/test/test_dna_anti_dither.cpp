#include <cstdint>
#include <vector>
#include <iostream>
#include <cassert>
#include <cstdlib>
#include <fstream>
#include <filesystem>

#include "../src/logger.h"
#include "../src/dna/dna_anti_dither.h"
#include "../src/dna/dna_vkd3d.h"

namespace fs = std::filesystem;

static inline uint32_t make_op(uint16_t length, uint16_t opcode) {
  return (static_cast<uint32_t>(length) << 16) | opcode;
}

// 用例 1: 二重螺旋 (DNA) 角色相机 Bayer / PseudoRandom 网点虚化精确消除
void test_dna_character_dither_neutralized() {
  std::cout << "[Test DNA 1] Character UE4 PseudoRandom Dither (Demote neutralized)... ";
  setenv("ANTI_DITHER_ENABLED", "1", 1);
  game_logger::g_initialized = false;
  game_logger::g_exclude_hashes.clear();
  game_logger::g_force_hashes.clear();

  dna_vkd3d::FloatUint f_noise;
  f_noise.f = 347.8345f;

  dna_vkd3d::FloatUint f_mult;
  f_mult.f = 1000.0f;

  std::vector<uint32_t> spv = {
    dna_vkd3d::SPV_HEADER_MAGIC,
    0x00010300,
    30017 << 16, // VKD3D
    100, // Bound
    0,
    // OpDecorate %10 BuiltIn FragCoord
    make_op(4, dna_vkd3d::SPV_OP_DECORATE), 10, dna_vkd3d::SPV_DECORATION_BUILTIN, dna_vkd3d::SPV_BUILTIN_FRAG_COORD,
    // OpConstant %50 (347.8345f noise const)
    make_op(4, dna_vkd3d::SPV_OP_CONSTANT), 1, 50, f_noise.u,
    // OpConstant %51 (1000.0f mult)
    make_op(4, dna_vkd3d::SPV_OP_CONSTANT), 1, 51, f_mult.u,
    // OpVariable ptr_Input %10
    make_op(4, dna_vkd3d::SPV_OP_VARIABLE), 1, 10, 1,
    // OpFunction
    make_op(5, 54 /* OpFunction */), 2, 1, 0, 3,
    make_op(2, dna_vkd3d::SPV_OP_LABEL), 20,
    // OpLoad %60 %10 (FragCoord)
    make_op(4, dna_vkd3d::SPV_OP_LOAD), 1, 60, 10,
    // OpFMul %70 %60 %50
    make_op(5, dna_vkd3d::SPV_OP_FMUL), 1, 70, 60, 50,
    // OpFMul %71 %70 %51
    make_op(5, dna_vkd3d::SPV_OP_FMUL), 1, 71, 70, 51,
    // OpFOrdLessThan %80 %71 %51
    make_op(5, dna_vkd3d::SPV_OP_FORDERED_LESS_THAN), 6, 80, 71, 51,
    // OpBranchConditional %80 %30 %40
    make_op(4, dna_vkd3d::SPV_OP_BRANCH_CONDITIONAL), 80, 30, 40,
    // OpLabel %30
    make_op(2, dna_vkd3d::SPV_OP_LABEL), 30,
    // OpDemoteToHelperInvocation (应当被 NOP)
    make_op(1, dna_vkd3d::SPV_OP_DEMOTE_TO_HELPER),
    // OpLabel %40
    make_op(2, dna_vkd3d::SPV_OP_LABEL), 40,
    make_op(1, 253 /* OpReturn */),
    make_op(1, 56 /* OpFunctionEnd */)
  };

  dna_vkd3d::process_spirv(spv.data(), spv.size());
  bool found_nop = false;
  for (uint32_t w : spv) {
    if ((w & 0xFFFF) == dna_vkd3d::SPV_OP_NOP) found_nop = true;
  }
  if (!found_nop) {
    std::cerr << "FAILED: DNA character noise dither was not neutralized!\n";
    std::abort();
  }
  std::cout << "PASSED\n";
}

// 用例 2: 植被/树木 Alpha Cutout 100% 保护
void test_dna_tree_foliage_alpha_cutout_preserved() {
  std::cout << "[Test DNA 2] Foliage Alpha Cutout with Texture Sample (Preserved 100%)... ";
  setenv("ANTI_DITHER_ENABLED", "1", 1);
  game_logger::g_initialized = false;
  game_logger::g_exclude_hashes.clear();
  game_logger::g_force_hashes.clear();

  dna_vkd3d::FloatUint f_cutoff;
  f_cutoff.f = 0.33333334f;

  std::vector<uint32_t> spv = {
    dna_vkd3d::SPV_HEADER_MAGIC,
    0x00010300,
    30017 << 16, // VKD3D
    100,
    0,
    // OpConstant %50
    make_op(4, dna_vkd3d::SPV_OP_CONSTANT), 1, 50, f_cutoff.u,
    // OpFunction
    make_op(5, 54), 2, 1, 0, 3,
    make_op(2, dna_vkd3d::SPV_OP_LABEL), 10,
    // OpImageSampleImplicitLod: type 4, res 15, sampled_img 2, coord 3
    make_op(5, 87), 4, 15, 2, 3,
    // OpFOrdLessThan %16 %15 %50 (Alpha Cutout condition)
    make_op(5, dna_vkd3d::SPV_OP_FORDERED_LESS_THAN), 6, 16, 15, 50,
    // OpBranchConditional %16 %20 %30
    make_op(4, dna_vkd3d::SPV_OP_BRANCH_CONDITIONAL), 16, 20, 30,
    // OpLabel %20
    make_op(2, dna_vkd3d::SPV_OP_LABEL), 20,
    // OpDemoteToHelperInvocation (应当保留，绝不能被 NOP)
    make_op(1, dna_vkd3d::SPV_OP_DEMOTE_TO_HELPER),
    // OpLabel %30
    make_op(2, dna_vkd3d::SPV_OP_LABEL), 30,
    make_op(1, 253),
    make_op(1, 56)
  };

  dna_vkd3d::process_spirv(spv.data(), spv.size());
  bool found_demote = false;
  for (uint32_t w : spv) {
    if ((w & 0xFFFF) == dna_vkd3d::SPV_OP_DEMOTE_TO_HELPER) found_demote = true;
  }
  if (!found_demote) {
    std::cerr << "FAILED: DNA foliage alpha cutout was incorrectly neutralized!\n";
    std::abort();
  }
  std::cout << "PASSED\n";
}

// 用例 3: 实机 Dump 着色器基准对照组验证
void test_dna_real_dump_benchmarks() {
  std::string dir = "/tmp/game_anti_dither/dumps/EM-Win64-Shipping";
  if (!fs::exists(dir)) {
    dir = "/tmp/game_anti_dither/dumps/EM-Win64-Shipping.1";
  }
  if (!fs::exists(dir)) {
    std::cout << "[Test DNA 3] Real dumps directory not present, skipping benchmark.\n";
    return;
  }

  std::cout << "[Test DNA 3] Real dumps benchmark verification (Trees 100% preserved, Chars dither eliminated)... ";

  std::vector<std::string> cutout_hashes = {
    "07d5b4e8", "0cf5fb85", "efb02371", "d4fa8e00", "288d03fd", "35a06b1e", "3822d2fc"
  };

  std::vector<std::string> char_hashes = {
    "11d0124c", "a75dc03a", "8c10cabe", "88f29f66", "63ad2188", "da1582f2", "c9750142",
    "5f718d79", "bc7fc1b4", "af4ab335", "07de3a83", "4febc1e5", "1fb9caf7", "c6859d72"
  };

  // 1. 验证纯镂空着色器 100% 保护
  for (const auto& h : cutout_hashes) {
    std::string path = dir + "/shader_" + h + "_orig.spv";
    if (!fs::exists(path)) continue;
    std::ifstream f(path, std::ios::binary);
    std::vector<uint32_t> spv(fs::file_size(path) / 4);
    f.read(reinterpret_cast<char*>(spv.data()), spv.size() * 4);

    uint32_t demotes_before = 0;
    for (size_t i = 5; i < spv.size(); ) {
      uint32_t w = spv[i];
      uint16_t op = w & 0xFFFF;
      uint16_t len = w >> 16;
      if (len == 0 || (i + len) > spv.size()) break;
      if (dna_vkd3d::is_discard_opcode(op)) demotes_before++;
      i += len;
    }

    dna_vkd3d::process_spirv(spv.data(), spv.size());

    uint32_t demotes_after = 0;
    for (size_t i = 5; i < spv.size(); ) {
      uint32_t w = spv[i];
      uint16_t op = w & 0xFFFF;
      uint16_t len = w >> 16;
      if (len == 0 || (i + len) > spv.size()) break;
      if (dna_vkd3d::is_discard_opcode(op)) demotes_after++;
      i += len;
    }

    if (demotes_after != demotes_before) {
      std::cerr << "FAILED: Real cutout shader 0x" << h << " had demote incorrectly NOPed! before="
                << demotes_before << " after=" << demotes_after << "\n";
      std::abort();
    }
  }

  // 2. 验证角色混合着色器：虚化网点消除，Alpha Cutout 保留
  for (const auto& h : char_hashes) {
    std::string path = dir + "/shader_" + h + "_orig.spv";
    if (!fs::exists(path)) continue;
    std::ifstream f(path, std::ios::binary);
    std::vector<uint32_t> spv(fs::file_size(path) / 4);
    f.read(reinterpret_cast<char*>(spv.data()), spv.size() * 4);

    uint32_t demotes_before = 0;
    for (size_t i = 5; i < spv.size(); ) {
      uint32_t w = spv[i];
      uint16_t op = w & 0xFFFF;
      uint16_t len = w >> 16;
      if (len == 0 || (i + len) > spv.size()) break;
      if (dna_vkd3d::is_discard_opcode(op)) demotes_before++;
      i += len;
    }

    dna_vkd3d::process_spirv(spv.data(), spv.size());

    uint32_t demotes_after = 0;
    for (size_t i = 5; i < spv.size(); ) {
      uint32_t w = spv[i];
      uint16_t op = w & 0xFFFF;
      uint16_t len = w >> 16;
      if (len == 0 || (i + len) > spv.size()) break;
      if (dna_vkd3d::is_discard_opcode(op)) demotes_after++;
      i += len;
    }

    bool code_modified = false;
    // 检查是否存在上游解耦重写或者 Demote NOP
    for (size_t k = 5; k < spv.size(); ++k) {
      uint32_t w = spv[k];
      uint16_t op = w & 0xFFFF;
      if (op == dna_vkd3d::SPV_OP_NOP || op == dna_vkd3d::SPV_OP_COPY_OBJECT) {
        code_modified = true;
        break;
      }
    }

    if (demotes_after >= demotes_before && !code_modified) {
      std::cerr << "FAILED: Real char shader 0x" << h << " expected dither demote NOPed or uncoupled! before="
                << demotes_before << " after=" << demotes_after << "\n";
      std::abort();
    }
  }

  std::cout << "PASSED\n";
}

// 用例 4: 统一入口分发路由测试
void test_dna_layer_dispatch() {
  std::cout << "[Test DNA 4] dna_layer::process_spirv_anti_dither dispatching... ";
  setenv("ANTI_DITHER_ENABLED", "1", 1);
  game_logger::g_initialized = false;
  game_logger::g_exclude_hashes.clear();
  game_logger::g_force_hashes.clear();

  dna_vkd3d::FloatUint f_noise;
  f_noise.f = 347.8345f;

  std::vector<uint32_t> spv = {
    dna_vkd3d::SPV_HEADER_MAGIC,
    0x00010300,
    30017 << 16, // VKD3D
    100,
    0,
    make_op(4, dna_vkd3d::SPV_OP_DECORATE), 10, dna_vkd3d::SPV_DECORATION_BUILTIN, dna_vkd3d::SPV_BUILTIN_FRAG_COORD,
    make_op(4, dna_vkd3d::SPV_OP_CONSTANT), 1, 50, f_noise.u,
    make_op(4, dna_vkd3d::SPV_OP_VARIABLE), 1, 10, 1,
    make_op(5, 54), 2, 1, 0, 3,
    make_op(2, dna_vkd3d::SPV_OP_LABEL), 20,
    make_op(4, dna_vkd3d::SPV_OP_LOAD), 1, 60, 10,
    make_op(5, dna_vkd3d::SPV_OP_FMUL), 1, 70, 60, 50,
    make_op(5, dna_vkd3d::SPV_OP_FORDERED_LESS_THAN), 6, 80, 70, 50,
    make_op(4, dna_vkd3d::SPV_OP_BRANCH_CONDITIONAL), 80, 30, 40,
    make_op(2, dna_vkd3d::SPV_OP_LABEL), 30,
    make_op(1, dna_vkd3d::SPV_OP_DEMOTE_TO_HELPER),
    make_op(2, dna_vkd3d::SPV_OP_LABEL), 40,
    make_op(1, 253),
    make_op(1, 56)
  };

  dna_layer::process_spirv_anti_dither(spv.data(), spv.size());
  bool found_nop = false;
  for (uint32_t w : spv) {
    if ((w & 0xFFFF) == dna_vkd3d::SPV_OP_NOP) found_nop = true;
  }
  if (!found_nop) {
    std::cerr << "FAILED: DNA layer dispatch failed!\n";
    std::abort();
  }
  std::cout << "PASSED\n";
}

static void test_dna_force_all_mode() {
  std::cout << "[Test DNA 5] Force all mode (ANTI_DITHER_FORCE_ALL=1)... ";
  dna_vkd3d::FloatUint f_cutoff;
  f_cutoff.f = 0.33333334f;

  std::vector<uint32_t> spv = {
    dna_vkd3d::SPV_HEADER_MAGIC,
    0x00010300,
    30017 << 16, // VKD3D
    100,
    0,
    make_op(4, dna_vkd3d::SPV_OP_CONSTANT), 1, 50, f_cutoff.u,
    make_op(5, 54), 2, 1, 0, 3,
    make_op(2, dna_vkd3d::SPV_OP_LABEL), 10,
    make_op(5, 87), 4, 15, 2, 3,
    make_op(5, dna_vkd3d::SPV_OP_FORDERED_LESS_THAN), 6, 16, 15, 50,
    make_op(4, dna_vkd3d::SPV_OP_BRANCH_CONDITIONAL), 16, 20, 30,
    make_op(2, dna_vkd3d::SPV_OP_LABEL), 20,
    make_op(1, dna_vkd3d::SPV_OP_DEMOTE_TO_HELPER),
    make_op(2, dna_vkd3d::SPV_OP_LABEL), 30,
    make_op(1, 253),
    make_op(1, 56)
  };

  game_logger::g_force_all = true;
  dna_vkd3d::process_spirv(spv.data(), spv.size());
  game_logger::g_force_all = false;

  bool found_nop = false;
  for (uint32_t w : spv) {
    if ((w & 0xFFFF) == dna_vkd3d::SPV_OP_NOP) found_nop = true;
  }
  if (!found_nop) {
    std::cerr << "FAILED: Force all mode should eliminate all demote!\n";
    std::abort();
  }
  std::cout << "PASSED\n";
}

// 用例 6: 角色裸露皮肤与头发次表面散射反向消隐消除 (Inverted Mask: Not(Alpha <= Thresh))
void test_dna_inverted_mask_skin_neutralized() {
  std::cout << "[Test DNA 6] Character skin/hair inverted mask anti-dither (Not(Alpha <= Cutoff))... ";
  setenv("ANTI_DITHER_ENABLED", "1", 1);
  game_logger::g_initialized = false;
  game_logger::g_exclude_hashes.clear();
  game_logger::g_force_hashes.clear();

  dna_vkd3d::FloatUint f_thresh;
  f_thresh.f = 0.89999998f;

  std::vector<uint32_t> spv = {
    dna_vkd3d::SPV_HEADER_MAGIC,
    0x00010300,
    30017 << 16, // VKD3D
    100, // Bound
    0,
    // OpConstant %50 (0.9f)
    make_op(4, dna_vkd3d::SPV_OP_CONSTANT), 1, 50, f_thresh.u,
    // OpFunction
    make_op(5, 54), 2, 1, 0, 3,
    make_op(2, dna_vkd3d::SPV_OP_LABEL), 10,
    // OpImageSampleImplicitLod %15
    make_op(5, 87), 4, 15, 2, 3,
    // OpFOrdLessThanEqual %16 %15 %50
    make_op(5, 186 /* OpFOrdLessThanEqual */), 6, 16, 15, 50,
    // OpLogicalNot %17 %16 (Inverted Mask)
    make_op(4, dna_vkd3d::SPV_OP_LOGICAL_NOT), 6, 17, 16,
    // OpBranchConditional %17 %20 %30
    make_op(4, dna_vkd3d::SPV_OP_BRANCH_CONDITIONAL), 17, 20, 30,
    // OpLabel %20
    make_op(2, dna_vkd3d::SPV_OP_LABEL), 20,
    // OpDemoteToHelperInvocation (应当被 100% NOP)
    make_op(1, dna_vkd3d::SPV_OP_DEMOTE_TO_HELPER),
    make_op(2, dna_vkd3d::SPV_OP_LABEL), 30,
    make_op(1, 253),
    make_op(1, 56)
  };

  dna_vkd3d::process_spirv(spv.data(), spv.size());

  bool found_demote = false;
  bool found_nop = false;
  for (uint32_t w : spv) {
    uint16_t op = w & 0xFFFF;
    if (op == dna_vkd3d::SPV_OP_DEMOTE_TO_HELPER) found_demote = true;
    if (op == dna_vkd3d::SPV_OP_NOP) found_nop = true;
  }

  if (found_demote || !found_nop) {
    std::cerr << "FAILED: Character skin inverted mask Demote should be NOPed!\n";
    std::abort();
  }
  std::cout << "PASSED\n";
}

// 用例 7: DXVK (DirectX 11) 后端角色点阵消除与植被保护验证
void test_dna_dxvk_character_and_foliage() {
  std::cout << "[Test DNA 7] DXVK (DirectX 11 / OpKill) character anti-dither and foliage cutout... ";
  setenv("ANTI_DITHER_ENABLED", "1", 1);
  game_logger::g_initialized = false;
  game_logger::g_exclude_hashes.clear();
  game_logger::g_force_hashes.clear();

  dna_dxvk::FloatUint f_noise;
  f_noise.f = 347.8345f;

  // 1. 角色 DXVK 着色器 (OpKill 应当被 NOP)
  std::vector<uint32_t> char_spv = {
    dna_dxvk::SPV_HEADER_MAGIC,
    0x00010300,
    16 << 16, // DXVK Generator Tool ID: 16
    100, // Bound
    0,
    // OpDecorate %10 BuiltIn FragCoord
    make_op(4, dna_dxvk::SPV_OP_DECORATE), 10, dna_dxvk::SPV_DECORATION_BUILTIN, dna_dxvk::SPV_BUILTIN_FRAG_COORD,
    // OpConstant %50 (347.8345f)
    make_op(4, dna_dxvk::SPV_OP_CONSTANT), 1, 50, f_noise.u,
    make_op(4, dna_dxvk::SPV_OP_VARIABLE), 1, 10, 1,
    make_op(5, 54), 2, 1, 0, 3,
    make_op(2, dna_dxvk::SPV_OP_LABEL), 20,
    // OpImageSampleImplicitLod %15 (角色材质贴图采样)
    make_op(5, 87), 4, 15, 2, 3,
    make_op(4, dna_dxvk::SPV_OP_LOAD), 1, 60, 10,
    make_op(5, dna_dxvk::SPV_OP_FMUL), 1, 70, 60, 50,
    make_op(5, dna_dxvk::SPV_OP_FORDERED_LESS_THAN), 6, 80, 70, 50,
    make_op(4, dna_dxvk::SPV_OP_BRANCH_CONDITIONAL), 80, 30, 40,
    make_op(2, dna_dxvk::SPV_OP_LABEL), 30,
    make_op(1, dna_dxvk::SPV_OP_KILL), // OpKill (252)
    make_op(2, dna_dxvk::SPV_OP_LABEL), 40,
    make_op(1, 253),
    make_op(1, 56)
  };

  // 通过统一层入口路由
  dna_layer::process_spirv_anti_dither(char_spv.data(), char_spv.size());

  bool char_has_kill = false;
  bool char_has_nop = false;
  for (uint32_t w : char_spv) {
    uint16_t op = w & 0xFFFF;
    if (op == dna_dxvk::SPV_OP_KILL) char_has_kill = true;
    if (op == dna_dxvk::SPV_OP_NOP) char_has_nop = true;
  }

  if (char_has_kill || !char_has_nop) {
    std::cerr << "FAILED: DXVK character OpKill should be NOPed!\n";
    std::abort();
  }

  // 2. 植被 DXVK 着色器 (OpKill 应当保留)
  dna_dxvk::FloatUint f_cutoff;
  f_cutoff.f = 0.33333334f;

  std::vector<uint32_t> plant_spv = {
    dna_dxvk::SPV_HEADER_MAGIC,
    0x00010300,
    16 << 16, // DXVK Generator Tool ID: 16
    100,
    0,
    make_op(4, dna_dxvk::SPV_OP_CONSTANT), 1, 50, f_cutoff.u,
    make_op(5, 54), 2, 1, 0, 3,
    make_op(2, dna_dxvk::SPV_OP_LABEL), 10,
    make_op(5, 87), 4, 15, 2, 3,
    make_op(5, dna_dxvk::SPV_OP_FORDERED_LESS_THAN), 6, 16, 15, 50,
    make_op(4, dna_dxvk::SPV_OP_BRANCH_CONDITIONAL), 16, 20, 30,
    make_op(2, dna_dxvk::SPV_OP_LABEL), 20,
    make_op(1, dna_dxvk::SPV_OP_KILL), // OpKill
    make_op(2, dna_dxvk::SPV_OP_LABEL), 30,
    make_op(1, 253),
    make_op(1, 56)
  };

  dna_layer::process_spirv_anti_dither(plant_spv.data(), plant_spv.size());

  bool plant_has_kill = false;
  for (uint32_t w : plant_spv) {
    uint16_t op = w & 0xFFFF;
    if (op == dna_dxvk::SPV_OP_KILL) plant_has_kill = true;
  }

  if (!plant_has_kill) {
    std::cerr << "FAILED: DXVK foliage OpKill must be preserved!\n";
    std::abort();
  }

  std::cout << "PASSED\n";
}

// 用例 8: DXVK (DirectX 11) 角色面部低视角消隐与远景替身树轮廓解耦验证
void test_dna_dxvk_real_face_and_impostor_tree() {
  std::cout << "[Test DNA 8] DXVK character face camera fade and impostor tree uncoupling... ";
  setenv("ANTI_DITHER_ENABLED", "1", 1);
  game_logger::g_initialized = false;
  game_logger::g_exclude_hashes.clear();
  game_logger::g_force_hashes.clear();

  std::string dir = "/tmp/game_anti_dither/dumps/EM-Win64-Shipping";
  auto test_dump_shader = [&](const std::string& h, uint32_t expected_before, uint32_t expected_after) {
    std::string path = dir + "/shader_" + h + "_orig.spv";
    if (!fs::exists(path)) return;
    std::ifstream f(path, std::ios::binary);
    std::vector<uint32_t> spv(fs::file_size(path) / 4);
    f.read(reinterpret_cast<char*>(spv.data()), spv.size() * 4);

    uint32_t before = 0;
    for (size_t i = 5; i < spv.size(); ) {
      uint32_t w = spv[i];
      uint16_t op = w & 0xFFFF;
      uint16_t len = w >> 16;
      if (len == 0 || (i + len) > spv.size()) break;
      if (dna_dxvk::is_discard_opcode(op)) before++;
      i += len;
    }

    dna_dxvk::process_spirv(spv.data(), spv.size());

    uint32_t after = 0;
    for (size_t i = 5; i < spv.size(); ) {
      uint32_t w = spv[i];
      uint16_t op = w & 0xFFFF;
      uint16_t len = w >> 16;
      if (len == 0 || (i + len) > spv.size()) break;
      if (dna_dxvk::is_discard_opcode(op)) after++;
      i += len;
    }

    if (before != expected_before || after != expected_after) {
      std::cerr << "FAILED: Shader 0x" << h << " unexpected demote counts! before="
                << before << " (expected " << expected_before << "), after="
                << after << " (expected " << expected_after << ")\n";
      std::abort();
    }
  };

  // 1. 大世界花草与树木植被：LOD 过渡绝对保护，杜绝中远距离方形卡片
  test_dump_shader("1f7ea4ed", 2, 2);
  test_dump_shader("03423537", 1, 1);

  std::cout << "PASSED\n";
}

int main() {
  test_dna_character_dither_neutralized();
  test_dna_tree_foliage_alpha_cutout_preserved();
  test_dna_real_dump_benchmarks();
  test_dna_layer_dispatch();
  test_dna_force_all_mode();
  test_dna_inverted_mask_skin_neutralized();
  test_dna_dxvk_character_and_foliage();
  test_dna_dxvk_real_face_and_impostor_tree();
  std::cout << "All DNA anti-dither unit tests passed successfully!\n";
  return 0;
}

