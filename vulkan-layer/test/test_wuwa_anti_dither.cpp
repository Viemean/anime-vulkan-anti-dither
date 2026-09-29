#include "../src/wuwa/wuwa_anti_dither.h"
#include "../src/azur_promilia/azur_promilia_anti_dither.h"
#include <cassert>
#include <iostream>
#include <vector>
#include <filesystem>
#include <fstream>

static uint32_t make_op(uint16_t length, uint16_t opcode) {
  return (static_cast<uint32_t>(length) << 16) | (static_cast<uint32_t>(opcode) & 0xFFFF);
}

void test_camera_dither_fragcoord_kill() {
  std::cout << "[Test 1] Camera Dither with Bayer 3x3 Table + FragCoord (OpKill 252)... ";
  setenv("ANTI_DITHER_ENABLED", "1", 1);
  game_logger::g_initialized = false;
  game_logger::g_exclude_hashes.clear();
  game_logger::g_force_hashes.clear();

  wuwa_layer::FloatUint f0, f1, f2, f3, f4, f5, f6, f7, f8;
  f0.f = 0.0f;
  f1.f = 0.77777779f;
  f2.f = 0.33333334f;
  f3.f = 0.66666669f;
  f4.f = 0.55555558f;
  f5.f = 0.22222222f;
  f6.f = 0.44444445f;
  f7.f = 0.11111111f;
  f8.f = 0.88888890f;

  std::vector<uint32_t> spv = {
    wuwa_layer::SPV_HEADER_MAGIC,
    0x00010300,
    0,
    100, // bound
    0,
    // OpDecorate %10 BuiltIn FragCoord (15)
    make_op(4, wuwa_layer::SPV_OP_DECORATE), 10, wuwa_layer::SPV_DECORATION_BUILTIN, wuwa_layer::SPV_BUILTIN_FRAG_COORD,
    // Constants for 3x3 Bayer table
    make_op(4, wuwa_layer::SPV_OP_CONSTANT), 1, 50, f0.u,
    make_op(4, wuwa_layer::SPV_OP_CONSTANT), 1, 51, f1.u,
    make_op(4, wuwa_layer::SPV_OP_CONSTANT), 1, 52, f2.u,
    make_op(4, wuwa_layer::SPV_OP_CONSTANT), 1, 53, f3.u,
    make_op(4, wuwa_layer::SPV_OP_CONSTANT), 1, 54, f4.u,
    make_op(4, wuwa_layer::SPV_OP_CONSTANT), 1, 55, f5.u,
    make_op(4, wuwa_layer::SPV_OP_CONSTANT), 1, 56, f6.u,
    make_op(4, wuwa_layer::SPV_OP_CONSTANT), 1, 57, f7.u,
    make_op(4, wuwa_layer::SPV_OP_CONSTANT), 1, 58, f8.u,
    // OpConstantComposite %60 (9 entries)
    make_op(12, wuwa_layer::SPV_OP_CONSTANT_COMPOSITE), 2, 60, 50, 51, 52, 53, 54, 55, 56, 57, 58,
    // OpVariable ptr_Input %10 Input
    make_op(4, wuwa_layer::SPV_OP_VARIABLE), 1, 10, 1,
    // OpVariable ptr_Private %61 Private %60
    make_op(5, wuwa_layer::SPV_OP_VARIABLE), 3, 61, 6, 60,
    // OpFunction void (%1)
    make_op(5, wuwa_layer::SPV_OP_FUNCTION), 2, 1, 0, 3,
    // OpLabel %20
    make_op(2, wuwa_layer::SPV_OP_LABEL), 20,
    // OpInBoundsAccessChain %62 %61 %idx
    make_op(5, 66), 4, 62, 61, 50,
    // OpLoad float %63 %62
    make_op(4, wuwa_layer::SPV_OP_LOAD), 1, 63, 62,
    // OpFOrdLessThan bool %13 %63 %14
    make_op(5, 184), 6, 13, 63, 14,
    // OpBranchConditional %13 %30 %40
    make_op(4, wuwa_layer::SPV_OP_BRANCH_CONDITIONAL), 13, 30, 40,
    // OpLabel %30
    make_op(2, wuwa_layer::SPV_OP_LABEL), 30,
    // OpKill (Should become NOP!)
    make_op(1, wuwa_layer::SPV_OP_KILL),
    // OpLabel %40
    make_op(2, wuwa_layer::SPV_OP_LABEL), 40,
    // OpReturn
    make_op(1, 253),
    // OpFunctionEnd
    make_op(1, wuwa_layer::SPV_OP_FUNCTION_END)
  };

  wuwa_layer::process_spirv_anti_dither(spv.data(), spv.size());

  bool found_nop = false;
  for (uint32_t w : spv) {
    if ((w & 0xFFFF) == wuwa_layer::SPV_OP_NOP)
      found_nop = true;
  }
  if (!found_nop) {
    std::cerr << "FAILED: OpNop not found!\n";
    std::abort();
  }
  std::cout << "PASSED\n";
}

void test_skill_mesh_cutout_preserved() {
  std::cout << "[Test 2] Skill Effect / Mesh Cutout (Vertex Color / Dissolve Math, Preserved)... ";
  setenv("ANTI_DITHER_ENABLED", "1", 1);
  game_logger::g_initialized = false;
  game_logger::g_exclude_hashes.clear();
  game_logger::g_force_hashes.clear();

  std::vector<uint32_t> spv = {
    wuwa_layer::SPV_HEADER_MAGIC,
    0x00010300,
    0,
    100,
    0,
    make_op(4, wuwa_layer::SPV_OP_DECORATE), 10, wuwa_layer::SPV_DECORATION_BUILTIN, wuwa_layer::SPV_BUILTIN_FRAG_COORD,
    make_op(4, wuwa_layer::SPV_OP_VARIABLE), 1, 10, 1,
    make_op(5, wuwa_layer::SPV_OP_FUNCTION), 2, 1, 0, 3,
    make_op(2, wuwa_layer::SPV_OP_LABEL), 20,
    make_op(5, 184), 6, 55, 50, 60,
    make_op(4, wuwa_layer::SPV_OP_BRANCH_CONDITIONAL), 55, 30, 40,
    make_op(2, wuwa_layer::SPV_OP_LABEL), 30,
    make_op(1, wuwa_layer::SPV_OP_KILL),
    make_op(2, wuwa_layer::SPV_OP_LABEL), 40,
    make_op(1, 253),
    make_op(1, wuwa_layer::SPV_OP_FUNCTION_END)
  };

  wuwa_layer::process_spirv_anti_dither(spv.data(), spv.size());
  assert((spv[31] & 0xFFFF) == wuwa_layer::SPV_OP_KILL);
  std::cout << "PASSED\n";
}

void test_camera_dither_demote() {
  std::cout << "[Test 3] Camera Dither with Bayer 3x3 Table + Demote (5380)... ";
  setenv("ANTI_DITHER_ENABLED", "1", 1);
  game_logger::g_initialized = false;
  game_logger::g_exclude_hashes.clear();
  game_logger::g_force_hashes.clear();

  wuwa_layer::FloatUint f0, f1, f2, f3, f4, f5, f6, f7, f8;
  f0.f = 0.0f;
  f1.f = 0.77777779f;
  f2.f = 0.33333334f;
  f3.f = 0.66666669f;
  f4.f = 0.55555558f;
  f5.f = 0.22222222f;
  f6.f = 0.44444445f;
  f7.f = 0.11111111f;
  f8.f = 0.88888890f;

  std::vector<uint32_t> spv = {
    wuwa_layer::SPV_HEADER_MAGIC,
    0x00010300,
    0,
    100,
    0,
    make_op(4, wuwa_layer::SPV_OP_DECORATE), 10, wuwa_layer::SPV_DECORATION_BUILTIN, wuwa_layer::SPV_BUILTIN_FRAG_COORD,
    make_op(4, wuwa_layer::SPV_OP_CONSTANT), 1, 50, f0.u,
    make_op(4, wuwa_layer::SPV_OP_CONSTANT), 1, 51, f1.u,
    make_op(4, wuwa_layer::SPV_OP_CONSTANT), 1, 52, f2.u,
    make_op(4, wuwa_layer::SPV_OP_CONSTANT), 1, 53, f3.u,
    make_op(4, wuwa_layer::SPV_OP_CONSTANT), 1, 54, f4.u,
    make_op(4, wuwa_layer::SPV_OP_CONSTANT), 1, 55, f5.u,
    make_op(4, wuwa_layer::SPV_OP_CONSTANT), 1, 56, f6.u,
    make_op(4, wuwa_layer::SPV_OP_CONSTANT), 1, 57, f7.u,
    make_op(4, wuwa_layer::SPV_OP_CONSTANT), 1, 58, f8.u,
    make_op(12, wuwa_layer::SPV_OP_CONSTANT_COMPOSITE), 2, 60, 50, 51, 52, 53, 54, 55, 56, 57, 58,
    make_op(4, wuwa_layer::SPV_OP_VARIABLE), 1, 10, 1,
    make_op(5, wuwa_layer::SPV_OP_VARIABLE), 3, 61, 6, 60,
    make_op(5, wuwa_layer::SPV_OP_FUNCTION), 2, 1, 0, 3,
    make_op(2, wuwa_layer::SPV_OP_LABEL), 20,
    make_op(5, 66), 4, 62, 61, 50,
    make_op(4, wuwa_layer::SPV_OP_LOAD), 1, 63, 62,
    make_op(5, 184), 6, 13, 63, 14,
    make_op(4, wuwa_layer::SPV_OP_BRANCH_CONDITIONAL), 13, 30, 40,
    make_op(2, wuwa_layer::SPV_OP_LABEL), 30,
    make_op(1, wuwa_layer::SPV_OP_DEMOTE_TO_HELPER_INVOCATION),
    make_op(2, wuwa_layer::SPV_OP_LABEL), 40,
    make_op(1, 253),
    make_op(1, wuwa_layer::SPV_OP_FUNCTION_END)
  };

  wuwa_layer::process_spirv_anti_dither(spv.data(), spv.size());
  bool found_nop = false;
  for (uint32_t w : spv) {
    if ((w & 0xFFFF) == wuwa_layer::SPV_OP_NOP)
      found_nop = true;
  }
  if (!found_nop) {
    std::cerr << "FAILED: OpNop not found!\n";
    std::abort();
  }
  std::cout << "PASSED\n";
}

void test_alpha_cutout_preserved() {
  std::cout << "[Test 4] Alpha Cutout with OpImageSampleImplicitLod (Preserved)... ";
  setenv("ANTI_DITHER_ENABLED", "1", 1);
  game_logger::g_initialized = false;
  game_logger::g_exclude_hashes.clear();
  game_logger::g_force_hashes.clear();

  std::vector<uint32_t> spv = {
    wuwa_layer::SPV_HEADER_MAGIC,
    0x00010300,
    0,
    100,
    0,
    make_op(5, wuwa_layer::SPV_OP_FUNCTION), 2, 1, 0, 3,
    make_op(2, wuwa_layer::SPV_OP_LABEL), 10,
    // OpImageSampleImplicitLod: type 4, res 15, sampled_img 2, coord 3 (length 5, opcode 87)
    make_op(5, 87), 4, 15, 2, 3,
    // OpFOrdLessThan: type 6, res 16, op1 15, op2 5 (length 5, opcode 184)
    make_op(5, 184), 6, 16, 15, 5,
    // OpBranchConditional %16 %20 %30
    make_op(4, wuwa_layer::SPV_OP_BRANCH_CONDITIONAL), 16, 20, 30,
    // OpLabel %20
    make_op(2, wuwa_layer::SPV_OP_LABEL), 20,
    // OpKill (must be PRESERVED!)
    make_op(1, wuwa_layer::SPV_OP_KILL),
    // OpLabel %30
    make_op(2, wuwa_layer::SPV_OP_LABEL), 30,
    // OpReturn
    make_op(1, 253),
    make_op(1, wuwa_layer::SPV_OP_FUNCTION_END)
  };

  wuwa_layer::process_spirv_anti_dither(spv.data(), spv.size());
  assert((spv[28] & 0xFFFF) == wuwa_layer::SPV_OP_KILL);
  std::cout << "PASSED\n";
}

void test_hash_blacklist_whitelist() {
  std::cout << "[Test 5] Hash Blacklist & Whitelist rules... ";
  game_logger::g_initialized = false;
  game_logger::g_exclude_hashes.clear();
  game_logger::g_force_hashes.clear();

  std::vector<uint32_t> spv = {
    wuwa_layer::SPV_HEADER_MAGIC,
    0x00010300,
    0,
    100,
    0,
    make_op(4, wuwa_layer::SPV_OP_DECORATE), 10, wuwa_layer::SPV_DECORATION_BUILTIN, wuwa_layer::SPV_BUILTIN_FRAG_COORD,
    make_op(4, wuwa_layer::SPV_OP_VARIABLE), 1, 10, 1,
    make_op(5, wuwa_layer::SPV_OP_FUNCTION), 2, 1, 0, 3,
    make_op(2, wuwa_layer::SPV_OP_LABEL), 20,
    make_op(4, wuwa_layer::SPV_OP_LOAD), 4, 11, 10,
    make_op(5, 184), 6, 13, 11, 14,
    make_op(4, wuwa_layer::SPV_OP_BRANCH_CONDITIONAL), 13, 30, 40,
    make_op(2, wuwa_layer::SPV_OP_LABEL), 30,
    make_op(1, wuwa_layer::SPV_OP_KILL),
    make_op(2, wuwa_layer::SPV_OP_LABEL), 40,
    make_op(1, 253),
    make_op(1, wuwa_layer::SPV_OP_FUNCTION_END)
  };

  uint32_t hash = game_logger::compute_spirv_hash(spv.data(), spv.size());

  // Test Blacklist: add to exclude hashes -> should NOT be NOPed
  game_logger::g_exclude_hashes.insert(hash);
  wuwa_layer::process_spirv_anti_dither(spv.data(), spv.size());
  assert((spv[35] & 0xFFFF) == wuwa_layer::SPV_OP_KILL);

  // Clear blacklist and run again -> should be NOPed
  game_logger::g_exclude_hashes.clear();
  wuwa_layer::process_spirv_anti_dither(spv.data(), spv.size());
  assert((spv[35] & 0xFFFF) == wuwa_layer::SPV_OP_NOP);

  std::cout << "PASSED\n";
}

void test_dx11_ign_function_call_dither() {
  std::cout << "[Test 6] DX11 (DXVK) IGN Dither with OpFunctionCall (dp2_f32) + Demote... ";
  setenv("ANTI_DITHER_ENABLED", "1", 1);
  game_logger::g_initialized = false;
  game_logger::g_exclude_hashes.clear();
  game_logger::g_force_hashes.clear();

  wuwa_layer::FloatUint f_ign_x, f_ign_y, f_ign_scale;
  f_ign_x.f = 0.0671105608f;
  f_ign_y.f = 0.00583714992f;
  f_ign_scale.f = 52.9829178f;

  std::vector<uint32_t> spv = {
    wuwa_layer::SPV_HEADER_MAGIC,
    0x00010300,
    0,
    100,
    0,
    make_op(4, wuwa_layer::SPV_OP_DECORATE), 10, wuwa_layer::SPV_DECORATION_BUILTIN, wuwa_layer::SPV_BUILTIN_FRAG_COORD,
    make_op(4, wuwa_layer::SPV_OP_CONSTANT), 1, 50, f_ign_x.u,
    make_op(4, wuwa_layer::SPV_OP_CONSTANT), 1, 51, f_ign_y.u,
    make_op(4, wuwa_layer::SPV_OP_CONSTANT), 1, 52, f_ign_scale.u,
    make_op(5, wuwa_layer::SPV_OP_CONSTANT_COMPOSITE), 2, 53, 50, 51,
    make_op(4, wuwa_layer::SPV_OP_VARIABLE), 1, 10, 1,
    make_op(5, wuwa_layer::SPV_OP_FUNCTION), 2, 1, 0, 3,
    make_op(2, wuwa_layer::SPV_OP_LABEL), 20,
    make_op(4, wuwa_layer::SPV_OP_LOAD), 4, 11, 10,
    // OpFunctionCall %float %res(60) %func_dp2(90) %arg_coord(11) %arg_ign(53)
    make_op(5, wuwa_layer::SPV_OP_FUNCTION_CALL), 1, 60, 90, 11, 53,
    make_op(5, 184), 6, 13, 60, 14,
    make_op(4, wuwa_layer::SPV_OP_BRANCH_CONDITIONAL), 13, 30, 40,
    make_op(2, wuwa_layer::SPV_OP_LABEL), 30,
    make_op(1, wuwa_layer::SPV_OP_DEMOTE_TO_HELPER_INVOCATION),
    make_op(2, wuwa_layer::SPV_OP_LABEL), 40,
    make_op(1, 253),
    make_op(1, wuwa_layer::SPV_OP_FUNCTION_END)
  };

  wuwa_layer::process_spirv_anti_dither(spv.data(), spv.size());
  bool found_nop = false;
  for (uint32_t w : spv) {
    if ((w & 0xFFFF) == wuwa_layer::SPV_OP_NOP)
      found_nop = true;
  }
  if (!found_nop) {
    std::cerr << "FAILED: OpNop not found for DX11 IGN dither!\n";
    std::abort();
  }
  std::cout << "PASSED\n";
}

void test_dx11_modulo_function_call_dither() {
  std::cout << "[Test 7] DX11 (DXVK) Modulo Dither with cvt_f32_u32 + OpUMod... ";
  setenv("ANTI_DITHER_ENABLED", "1", 1);
  game_logger::g_initialized = false;
  game_logger::g_exclude_hashes.clear();
  game_logger::g_force_hashes.clear();

  std::vector<uint32_t> spv = {
    wuwa_layer::SPV_HEADER_MAGIC,
    0x00010300,
    0,
    100,
    0,
    make_op(4, wuwa_layer::SPV_OP_DECORATE), 10, wuwa_layer::SPV_DECORATION_BUILTIN, wuwa_layer::SPV_BUILTIN_FRAG_COORD,
    make_op(4, wuwa_layer::SPV_OP_VARIABLE), 1, 10, 1,
    make_op(5, wuwa_layer::SPV_OP_FUNCTION), 2, 1, 0, 3,
    make_op(2, wuwa_layer::SPV_OP_LABEL), 20,
    make_op(4, wuwa_layer::SPV_OP_LOAD), 4, 11, 10,
    // OpFunctionCall %uint %res(60) %func_cvt(90) %arg_coord(11)
    make_op(5, wuwa_layer::SPV_OP_FUNCTION_CALL), 1, 60, 90, 11,
    // OpUMod %uint %res(61) %op1(60) %op2(5) (opcode 137)
    make_op(5, 137), 1, 61, 60, 5,
    make_op(5, 184), 6, 13, 61, 14,
    make_op(4, wuwa_layer::SPV_OP_BRANCH_CONDITIONAL), 13, 30, 40,
    make_op(2, wuwa_layer::SPV_OP_LABEL), 30,
    make_op(1, wuwa_layer::SPV_OP_DEMOTE_TO_HELPER_INVOCATION),
    make_op(2, wuwa_layer::SPV_OP_LABEL), 40,
    make_op(1, 253),
    make_op(1, wuwa_layer::SPV_OP_FUNCTION_END)
  };

  wuwa_layer::process_spirv_anti_dither(spv.data(), spv.size());
  bool found_nop = false;
  for (uint32_t w : spv) {
    if ((w & 0xFFFF) == wuwa_layer::SPV_OP_NOP)
      found_nop = true;
  }
  if (!found_nop) {
    std::cerr << "FAILED: OpNop not found for DX11 Modulo dither!\n";
    std::abort();
  }
  std::cout << "PASSED\n";
}

void test_azur_promilia_pipeline() {
  std::cout << "[Test 8] Azur Promilia Hierarchical Range Anti-Dither Rules... ";
  
  // 1. UberShader (> 3000 DW) -> NOP
  {
    std::vector<uint32_t> spv(3200, make_op(1, 0));
    spv[0] = azur_promilia_layer::SPV_HEADER_MAGIC;
    spv[3] = 100;
    spv[10] = make_op(1, azur_promilia_layer::SPV_OP_DEMOTE_TO_HELPER_INVOCATION);
    azur_promilia_layer::process_spirv_anti_dither(spv.data(), spv.size());
    assert((spv[10] & 0xFFFF) == azur_promilia_layer::SPV_OP_NOP);
  }

  // 2. Secondary Pass (815 DW) -> NOP
  {
    std::vector<uint32_t> spv(815, make_op(1, 0));
    spv[0] = azur_promilia_layer::SPV_HEADER_MAGIC;
    spv[3] = 100;
    spv[10] = make_op(1, azur_promilia_layer::SPV_OP_DEMOTE_TO_HELPER_INVOCATION);
    azur_promilia_layer::process_spirv_anti_dither(spv.data(), spv.size());
    assert((spv[10] & 0xFFFF) == azur_promilia_layer::SPV_OP_NOP);
  }

  // 3. Crop Protection (114, 185, 339 DW) -> Preserved (NOT NOP)
  {
    for (size_t crop_len : {114, 185, 339}) {
      std::vector<uint32_t> spv(crop_len, make_op(1, 0));
      spv[0] = azur_promilia_layer::SPV_HEADER_MAGIC;
      spv[3] = 100;
      spv[10] = make_op(1, azur_promilia_layer::SPV_OP_DEMOTE_TO_HELPER_INVOCATION);
      azur_promilia_layer::process_spirv_anti_dither(spv.data(), spv.size());
      assert((spv[10] & 0xFFFF) == azur_promilia_layer::SPV_OP_DEMOTE_TO_HELPER_INVOCATION);
    }
  }

  // 4. Eyes & Micro Close-up Pass (233 DW, 450 DW) -> NOP
  {
    for (size_t eye_len : {233, 450}) {
      std::vector<uint32_t> spv(eye_len, make_op(1, 0));
      spv[0] = azur_promilia_layer::SPV_HEADER_MAGIC;
      spv[3] = 100;
      spv[10] = make_op(1, azur_promilia_layer::SPV_OP_DEMOTE_TO_HELPER_INVOCATION);
      azur_promilia_layer::process_spirv_anti_dither(spv.data(), spv.size());
      assert((spv[10] & 0xFFFF) == azur_promilia_layer::SPV_OP_NOP);
    }
  }

  // 5. Standard World Tree / Foliage (1200 DW) -> Preserved (NOT NOP)
  {
    std::vector<uint32_t> spv(1200, make_op(1, 0));
    spv[0] = azur_promilia_layer::SPV_HEADER_MAGIC;
    spv[3] = 100;
    spv[10] = make_op(1, azur_promilia_layer::SPV_OP_DEMOTE_TO_HELPER_INVOCATION);
    azur_promilia_layer::process_spirv_anti_dither(spv.data(), spv.size());
    assert((spv[10] & 0xFFFF) == azur_promilia_layer::SPV_OP_DEMOTE_TO_HELPER_INVOCATION);
  }

  std::cout << "PASSED\n";
}

void test_foliage_dither_lod_with_alpha_cutout_preserved() {
  std::cout << "[Test 9] Foliage Dither LOD Transition with Texture Alpha Cutout (Preserved)... ";
  setenv("ANTI_DITHER_ENABLED", "1", 1);
  game_logger::g_initialized = false;
  game_logger::g_exclude_hashes.clear();
  game_logger::g_force_hashes.clear();

  // Simulates UE Landscape/Foliage Grass shader (e.g. 0x3bda0183):
  // Screen FragCoord -> OpUMod % 5 -> Add with Texture Alpha -> Demote
  std::vector<uint32_t> spv = {
    wuwa_layer::SPV_HEADER_MAGIC,
    0x00010300,
    0,
    100,
    0,
    make_op(4, wuwa_layer::SPV_OP_DECORATE), 10, wuwa_layer::SPV_DECORATION_BUILTIN, wuwa_layer::SPV_BUILTIN_FRAG_COORD,
    make_op(4, wuwa_layer::SPV_OP_VARIABLE), 1, 10, 1,
    make_op(5, wuwa_layer::SPV_OP_FUNCTION), 2, 1, 0, 3,
    make_op(2, wuwa_layer::SPV_OP_LABEL), 20,
    // 1. Texture Alpha Sample (opcode 87)
    make_op(5, 87), 4, 15, 2, 3,
    // 2. FragCoord Load & Modulo (opcode 137)
    make_op(4, wuwa_layer::SPV_OP_LOAD), 4, 11, 10,
    make_op(5, 137), 1, 61, 11, 5,
    // 3. Combine Texture Alpha (15) and Dither Noise (61): OpFAdd (opcode 129)
    make_op(5, 129), 1, 70, 15, 61,
    // 4. Comparison
    make_op(5, 184), 6, 13, 70, 14,
    make_op(4, wuwa_layer::SPV_OP_BRANCH_CONDITIONAL), 13, 30, 40,
    make_op(2, wuwa_layer::SPV_OP_LABEL), 30,
    // Demote must be PRESERVED to maintain grass leaf silhouette!
    make_op(1, wuwa_layer::SPV_OP_DEMOTE_TO_HELPER_INVOCATION),
    make_op(2, wuwa_layer::SPV_OP_LABEL), 40,
    make_op(1, 253),
    make_op(1, wuwa_layer::SPV_OP_FUNCTION_END)
  };

  wuwa_layer::process_spirv_anti_dither(spv.data(), spv.size());
  bool found_demote = false;
  for (uint32_t w : spv) {
    if ((w & 0xFFFF) == wuwa_layer::SPV_OP_DEMOTE_TO_HELPER_INVOCATION)
      found_demote = true;
  }
  if (!found_demote) {
    std::cerr << "FAILED: Foliage texture cutout was wrongly NOPed!\n";
    std::abort();
  }
  std::cout << "PASSED\n";
}

#include <fstream>

void test_dx11_real_world_shaders() {
  std::cout << "[Test 10] DX11 (DXVK) Real-World Shaders (Chisa, Foliage, Stripe)... ";
  setenv("ANTI_DITHER_ENABLED", "1", 1);
  game_logger::g_initialized = false;
  game_logger::g_exclude_hashes.clear();
  game_logger::g_force_hashes.clear();

  auto load_spv = [](const std::string& path) -> std::vector<uint32_t> {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) return {};
    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);
    std::vector<uint32_t> spv(size / sizeof(uint32_t));
    file.read(reinterpret_cast<char*>(spv.data()), size);
    return spv;
  };

  std::string dump_dir = "/tmp/game_anti_dither/dumps/Client-Win64-Shipping/";

  // 1. Chisa Close-up (0x5c1ec349): NMin uncoupled to OpCopyObject, Demote neutralized to OpNop
  {
    auto spv = load_spv(dump_dir + "shader_5c1ec349_orig.spv");
    if (!spv.empty()) {
      wuwa_layer::process_spirv_anti_dither(spv.data(), spv.size());
      bool has_copy_object = false;
      bool has_nop = false;
      for (uint32_t w : spv) {
        uint16_t op = w & 0xFFFF;
        if (op == wuwa_layer::SPV_OP_COPY_OBJECT) has_copy_object = true;
        if (op == wuwa_layer::SPV_OP_NOP) has_nop = true;
      }
      if (!has_copy_object || !has_nop) {
        std::cerr << "FAILED: Chisa close-up dither was not properly uncoupled or neutralized!\n";
        std::abort();
      }
    }
  }

  // 2. Character Mesh Cutout (0x8c3a5c3e): NMin uncoupled to OpCopyObject, Demote neutralized to OpNop
  {
    auto spv = load_spv(dump_dir + "shader_8c3a5c3e_orig.spv");
    if (!spv.empty()) {
      wuwa_layer::process_spirv_anti_dither(spv.data(), spv.size());
      bool has_copy_object = false;
      bool has_nop = false;
      for (uint32_t w : spv) {
        uint16_t op = w & 0xFFFF;
        if (op == wuwa_layer::SPV_OP_COPY_OBJECT) has_copy_object = true;
        if (op == wuwa_layer::SPV_OP_NOP) has_nop = true;
      }
      if (!has_copy_object || !has_nop) {
        std::cerr << "FAILED: Character mesh dither was not properly uncoupled or neutralized!\n";
        std::abort();
      }
    }
  }

  // 3. Modulo 5 Look-up Stripe (0x02632c3d): Demote neutralized to OpNop
  {
    auto spv = load_spv(dump_dir + "shader_02632c3d_orig.spv");
    if (!spv.empty()) {
      wuwa_layer::process_spirv_anti_dither(spv.data(), spv.size());
      bool has_nop = false;
      for (uint32_t w : spv) {
        uint16_t op = w & 0xFFFF;
        if (op == wuwa_layer::SPV_OP_NOP) has_nop = true;
      }
      if (!has_nop) {
        std::cerr << "FAILED: Stripe dither demote was not converted to OpNop!\n";
        std::abort();
      }
    }
  }

  // 4. VKD3D Foliage, Flower & Terrain (0x4811f107): Bayer dither eliminated
  {
    auto spv = load_spv(dump_dir + "shader_4811f107_orig.spv");
    if (!spv.empty()) {
      wuwa_layer::process_spirv_anti_dither(spv.data(), spv.size());
      bool has_nop = false;
      for (uint32_t w : spv) {
        if ((w & 0xFFFF) == wuwa_layer::SPV_OP_NOP) has_nop = true;
      }
      if (!has_nop) {
        std::cerr << "FAILED: Foliage/terrain dither was not neutralized in VKD3D!\n";
        std::abort();
      }
    }
  }

  std::cout << "PASSED\n";
}

void test_logger_rotation_and_retention() {
  std::cout << "[Test 11] Logger and Dump 3-Run Rotation Retention... ";
  namespace fs = std::filesystem;
  std::error_code ec;
  std::string test_log_base = "/tmp/test_anti_dither_rotation.log";
  std::string test_dump_base = "/tmp/test_anti_dither_dump_rotation";

  // 清理测试环境
  for (int i = 0; i <= 5; ++i) {
    fs::remove(test_log_base + (i == 0 ? "" : "." + std::to_string(i)), ec);
    fs::remove_all(test_dump_base + (i == 0 ? "" : "." + std::to_string(i)), ec);
  }

  // 模拟5次运行生成与轮转
  for (int run = 1; run <= 5; ++run) {
    game_logger::rotate_file_backups(test_log_base, 3);
    std::ofstream lf(test_log_base);
    lf << "run " << run << "\n";
    lf.close();

    game_logger::rotate_directory_backups(test_dump_base, 3);
    fs::create_directories(test_dump_base, ec);
    std::ofstream df(test_dump_base + "/run.txt");
    df << "dump run " << run << "\n";
    df.close();
  }

  // 验证只保留当前 + 3次历史备份 (.1, .2, .3)
  if (!fs::exists(test_log_base, ec) ||
      !fs::exists(test_log_base + ".1", ec) ||
      !fs::exists(test_log_base + ".2", ec) ||
      !fs::exists(test_log_base + ".3", ec)) {
    std::cerr << "FAILED: Log retention missing required .1, .2 or .3 backups!\n";
    std::abort();
  }
  if (fs::exists(test_log_base + ".4", ec) || fs::exists(test_log_base + ".5", ec)) {
    std::cerr << "FAILED: Log retention did not prune .4 or .5 backups!\n";
    std::abort();
  }

  if (!fs::exists(test_dump_base, ec) ||
      !fs::exists(test_dump_base + ".1", ec) ||
      !fs::exists(test_dump_base + ".2", ec) ||
      !fs::exists(test_dump_base + ".3", ec)) {
    std::cerr << "FAILED: Dump directory retention missing required backups!\n";
    std::abort();
  }
  if (fs::exists(test_dump_base + ".4", ec) || fs::exists(test_dump_base + ".5", ec)) {
    std::cerr << "FAILED: Dump directory retention did not prune older backups!\n";
    std::abort();
  }

  // 清理临时测试文件
  for (int i = 0; i <= 3; ++i) {
    fs::remove(test_log_base + (i == 0 ? "" : "." + std::to_string(i)), ec);
    fs::remove_all(test_dump_base + (i == 0 ? "" : "." + std::to_string(i)), ec);
  }

  std::cout << "PASSED\n";
}

int main() {
  std::cout << "=== Vulkan Anti-Dither SSA Unit Tests ===\n";
  test_camera_dither_fragcoord_kill();
  test_skill_mesh_cutout_preserved();
  test_camera_dither_demote();
  test_alpha_cutout_preserved();
  test_hash_blacklist_whitelist();
  test_dx11_ign_function_call_dither();
  test_dx11_modulo_function_call_dither();
  test_azur_promilia_pipeline();
  test_foliage_dither_lod_with_alpha_cutout_preserved();
  test_dx11_real_world_shaders();
  test_logger_rotation_and_retention();
  std::cout << "=== All Anti-Dither Tests Passed Successfully ===\n";
  return 0;
}



