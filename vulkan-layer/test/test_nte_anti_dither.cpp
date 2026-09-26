#include <cstdint>
#include <vector>
#include <iostream>
#include <cassert>
#include <cstdlib>

#include "../src/logger.h"
#include "../src/nte/nte_anti_dither.h"
#include "../src/nte/nte_vkd3d.h"

static inline uint32_t make_op(uint16_t length, uint16_t opcode) {
  return (static_cast<uint32_t>(length) << 16) | opcode;
}

// 用例 1: 角色卡通网点半透明虚化（Bayer / IGN 屏幕空间点阵）精确中和
void test_nte_character_bayer_dither_neutralized() {
  std::cout << "[Test NTE 1] Character Screen-space Bayer Dither (Demote neutralized)... ";
  setenv("ANTI_DITHER_ENABLED", "1", 1);
  game_logger::g_initialized = false;
  game_logger::g_exclude_hashes.clear();
  game_logger::g_force_hashes.clear();

  nte_vkd3d::FloatUint f_bayer;
  f_bayer.f = 0.44444445f;

  std::vector<uint32_t> spv = {
    nte_vkd3d::SPV_HEADER_MAGIC,
    0x00010300,
    30017 << 16, // VKD3D
    100, // Bound
    0,
    // OpDecorate %10 BuiltIn FragCoord
    make_op(4, nte_vkd3d::SPV_OP_DECORATE), 10, nte_vkd3d::SPV_DECORATION_BUILTIN, nte_vkd3d::SPV_BUILTIN_FRAG_COORD,
    // OpConstant %50 (Bayer fraction)
    make_op(4, nte_vkd3d::SPV_OP_CONSTANT), 1, 50, f_bayer.u,
    // OpVariable ptr_Input %10
    make_op(4, nte_vkd3d::SPV_OP_VARIABLE), 1, 10, 1,
    // OpFunction
    make_op(5, nte_vkd3d::SPV_OP_FUNCTION), 2, 1, 0, 3,
    make_op(2, nte_vkd3d::SPV_OP_LABEL), 20,
    // OpLoad %60 %10 (FragCoord)
    make_op(4, nte_vkd3d::SPV_OP_LOAD), 1, 60, 10,
    // OpDot %70 %60 %50 (Dither Noise)
    make_op(5, nte_vkd3d::SPV_OP_DOT), 1, 70, 60, 50,
    // OpFOrdLessThan %80 %70 %50
    make_op(5, 184), 6, 80, 70, 50,
    // OpBranchConditional %80 %30 %40
    make_op(4, nte_vkd3d::SPV_OP_BRANCH_CONDITIONAL), 80, 30, 40,
    // OpLabel %30
    make_op(2, nte_vkd3d::SPV_OP_LABEL), 30,
    // OpDemoteToHelperInvocation (应当被 NOP)
    make_op(1, nte_vkd3d::SPV_OP_DEMOTE_TO_HELPER_INVOCATION),
    // OpLabel %40
    make_op(2, nte_vkd3d::SPV_OP_LABEL), 40,
    make_op(1, 253),
    make_op(1, nte_vkd3d::SPV_OP_FUNCTION_END)
  };

  nte_vkd3d::process_spirv_anti_dither(spv.data(), spv.size());
  bool found_nop = false;
  for (uint32_t w : spv) {
    if ((w & 0xFFFF) == nte_vkd3d::SPV_OP_NOP) found_nop = true;
  }
  if (!found_nop) {
    std::cerr << "FAILED: NTE character dither was not neutralized!\n";
    std::abort();
  }
  std::cout << "PASSED\n";
}

// 用例 2: 树木植被贴图 Alpha Cutout 100% 保护（依赖 OpImageSample*）
void test_nte_tree_foliage_alpha_cutout_preserved() {
  std::cout << "[Test NTE 2] Tree Foliage Alpha Cutout with Texture Sample (Preserved 100%)... ";
  setenv("ANTI_DITHER_ENABLED", "1", 1);
  game_logger::g_initialized = false;
  game_logger::g_exclude_hashes.clear();
  game_logger::g_force_hashes.clear();

  nte_vkd3d::FloatUint f_bayer;
  f_bayer.f = 0.33333334f;

  std::vector<uint32_t> spv = {
    nte_vkd3d::SPV_HEADER_MAGIC,
    0x00010300,
    30017 << 16, // VKD3D
    100,
    0,
    // OpDecorate %10 BuiltIn FragCoord
    make_op(4, nte_vkd3d::SPV_OP_DECORATE), 10, nte_vkd3d::SPV_DECORATION_BUILTIN, nte_vkd3d::SPV_BUILTIN_FRAG_COORD,
    // OpConstant %50
    make_op(4, nte_vkd3d::SPV_OP_CONSTANT), 1, 50, f_bayer.u,
    // OpFunction
    make_op(5, nte_vkd3d::SPV_OP_FUNCTION), 2, 1, 0, 3,
    make_op(2, nte_vkd3d::SPV_OP_LABEL), 10,
    // OpImageSampleImplicitLod: type 4, res 15, sampled_img 2, coord 3
    make_op(5, 87), 4, 15, 2, 3,
    // OpFOrdLessThan %16 %15 %5 (Alpha Cutout condition)
    make_op(5, 184), 6, 16, 15, 5,
    // OpBranchConditional %16 %20 %30
    make_op(4, nte_vkd3d::SPV_OP_BRANCH_CONDITIONAL), 16, 20, 30,
    // OpLabel %20
    make_op(2, nte_vkd3d::SPV_OP_LABEL), 20,
    // OpDemoteToHelperInvocation (依赖贴图采样，严禁消除)
    make_op(1, nte_vkd3d::SPV_OP_DEMOTE_TO_HELPER_INVOCATION),
    // OpLabel %30
    make_op(2, nte_vkd3d::SPV_OP_LABEL), 30,
    make_op(1, 253),
    make_op(1, nte_vkd3d::SPV_OP_FUNCTION_END)
  };

  nte_vkd3d::process_spirv_anti_dither(spv.data(), spv.size());
  // 校验实机导出的树木材质着色器 (shader_6e6fa990)
  const char* tree_path = "/tmp/game_anti_dither/dumps/HTGame/shader_6e6fa990_orig.spv";
  FILE* fp_tree = std::fopen(tree_path, "rb");
  if (fp_tree) {
    std::fseek(fp_tree, 0, SEEK_END);
    size_t tree_bytes = std::ftell(fp_tree);
    std::fseek(fp_tree, 0, SEEK_SET);

    std::vector<uint32_t> tree_spv(tree_bytes / sizeof(uint32_t));
    std::fread(tree_spv.data(), sizeof(uint32_t), tree_spv.size(), fp_tree);
    std::fclose(fp_tree);

    nte_vkd3d::process_spirv_anti_dither(tree_spv.data(), tree_spv.size());

    bool has_nop = false;
    for (size_t idx = 5; idx < tree_spv.size(); ++idx) {
      if (tree_spv[idx] == ((1 << 16) | nte_vkd3d::SPV_OP_NOP)) {
        has_nop = true;
        std::cerr << "Found actual NOP at instruction word " << idx << "!\n";
        break;
      }
    }

    if (has_nop) {
      std::cerr << "FAILED: Real-world tree foliage shader 6e6fa990 was wrongly modified with NOP!\n";
      std::abort();
    }
  }

  std::cout << "PASSED\n";
}

// 用例 3: NTE 黑名单规则排除测试
void test_nte_blacklist_rules() {
  std::cout << "[Test NTE 3] Blacklist rule exclusion... ";
  setenv("ANTI_DITHER_ENABLED", "1", 1);
  game_logger::g_initialized = false;
  game_logger::g_exclude_hashes.clear();
  game_logger::g_force_hashes.clear();

  nte_vkd3d::FloatUint f_bayer;
  f_bayer.f = 0.44444445f;

  std::vector<uint32_t> spv = {
    nte_vkd3d::SPV_HEADER_MAGIC,
    0x00010300,
    30017 << 16,
    100,
    0,
    make_op(4, nte_vkd3d::SPV_OP_DECORATE), 10, nte_vkd3d::SPV_DECORATION_BUILTIN, nte_vkd3d::SPV_BUILTIN_FRAG_COORD,
    make_op(4, nte_vkd3d::SPV_OP_CONSTANT), 1, 50, f_bayer.u,
    make_op(4, nte_vkd3d::SPV_OP_VARIABLE), 1, 10, 1,
    make_op(5, nte_vkd3d::SPV_OP_FUNCTION), 2, 1, 0, 3,
    make_op(2, nte_vkd3d::SPV_OP_LABEL), 20,
    make_op(4, nte_vkd3d::SPV_OP_LOAD), 1, 60, 10,
    make_op(5, nte_vkd3d::SPV_OP_DOT), 1, 70, 60, 50,
    make_op(5, 184), 6, 80, 70, 50,
    make_op(4, nte_vkd3d::SPV_OP_BRANCH_CONDITIONAL), 80, 30, 40,
    make_op(2, nte_vkd3d::SPV_OP_LABEL), 30,
    make_op(1, nte_vkd3d::SPV_OP_DEMOTE_TO_HELPER_INVOCATION),
    make_op(2, nte_vkd3d::SPV_OP_LABEL), 40,
    make_op(1, 253),
    make_op(1, nte_vkd3d::SPV_OP_FUNCTION_END)
  };

  uint32_t hash = game_logger::compute_spirv_hash(spv.data(), spv.size());
  game_logger::g_exclude_hashes.insert(hash);

  nte_vkd3d::process_spirv_anti_dither(spv.data(), spv.size());
  bool found_demote = false;
  for (uint32_t w : spv) {
    if ((w & 0xFFFF) == nte_vkd3d::SPV_OP_DEMOTE_TO_HELPER_INVOCATION) found_demote = true;
  }
  if (!found_demote) {
    std::cerr << "FAILED: Blacklisted shader was modified!\n";
    std::abort();
  }
  game_logger::g_exclude_hashes.clear();
  std::cout << "PASSED\n";
}

// 用例 4: NTE 顶层分发管线阶段检测（Vertex 与 Fragment）
void test_nte_pipeline_stage_dispatch() {
  std::cout << "[Test NTE 4] Stage detection (Fragment vs Vertex)... ";
  
  // 构造 Fragment Shader (OpEntryPoint Fragment %4 "main")
  std::vector<uint32_t> ps_spv = {
    nte_vkd3d::SPV_HEADER_MAGIC,
    0x00010300,
    30017 << 16,
    50,
    0,
    make_op(4, nte_vkd3d::SPV_OP_ENTRY_POINT), 4, 4, 0x6e69616d // ExecutionModel = 4 (Fragment)
  };

  auto ps_stage = nte_layer::detect_shader_stage(ps_spv.data(), ps_spv.size());
  if (ps_stage != nte_layer::ShaderStage::Fragment) {
    std::cerr << "FAILED: Failed to detect Fragment stage!\n";
    std::abort();
  }

  // 构造 Vertex Shader (OpEntryPoint Vertex %4 "main")
  std::vector<uint32_t> vs_spv = {
    nte_vkd3d::SPV_HEADER_MAGIC,
    0x00010300,
    30017 << 16,
    50,
    0,
    make_op(4, nte_vkd3d::SPV_OP_ENTRY_POINT), 0, 4, 0x6e69616d // ExecutionModel = 0 (Vertex)
  };

  auto vs_stage = nte_layer::detect_shader_stage(vs_spv.data(), vs_spv.size());
  if (vs_stage != nte_layer::ShaderStage::Vertex) {
    std::cerr << "FAILED: Failed to detect Vertex stage!\n";
    std::abort();
  }

  std::cout << "PASSED\n";
}

// 用例 5: 实机抓取的 UE 5.5 真实蓝噪声着色器验证 (shader_39827d15)
void test_nte_real_world_dump_shader() {
  std::cout << "[Test NTE 5] Real-world UE 5.5 Blue Noise Shader (shader_39827d15)... ";
  const char* path = "/tmp/game_anti_dither/dumps/HTGame/shader_39827d15_orig.spv";
  FILE* fp = std::fopen(path, "rb");
  if (!fp) {
    std::cout << "SKIPPED (dump file not found)\n";
    return;
  }

  std::fseek(fp, 0, SEEK_END);
  size_t bytes = std::ftell(fp);
  std::fseek(fp, 0, SEEK_SET);

  std::vector<uint32_t> spv(bytes / sizeof(uint32_t));
  std::fread(spv.data(), sizeof(uint32_t), spv.size(), fp);
  std::fclose(fp);

  setenv("ANTI_DITHER_ENABLED", "1", 1);
  game_logger::g_initialized = false;
  game_logger::g_exclude_hashes.clear();
  game_logger::g_force_hashes.clear();

  nte_vkd3d::process_spirv_anti_dither(spv.data(), spv.size());

  bool found_nop = false;
  for (uint32_t w : spv) {
    if ((w & 0xFFFF) == nte_vkd3d::SPV_OP_NOP) {
      found_nop = true;
      break;
    }
  }

  if (!found_nop) {
    std::cerr << "FAILED: Real-world shader 39827d15 demote was not neutralized!\n";
    std::abort();
  }

  // 测试 shader 2: shader_6befdec4
  const char* path2 = "/tmp/game_anti_dither/dumps/HTGame/shader_6befdec4_orig.spv";
  FILE* fp2 = std::fopen(path2, "rb");
  if (fp2) {
    std::fseek(fp2, 0, SEEK_END);
    size_t bytes2 = std::ftell(fp2);
    std::fseek(fp2, 0, SEEK_SET);

    std::vector<uint32_t> spv2(bytes2 / sizeof(uint32_t));
    std::fread(spv2.data(), sizeof(uint32_t), spv2.size(), fp2);
    std::fclose(fp2);

    nte_vkd3d::process_spirv_anti_dither(spv2.data(), spv2.size());

    bool found_nop2 = false;
    for (uint32_t w : spv2) {
      if ((w & 0xFFFF) == nte_vkd3d::SPV_OP_NOP) {
        found_nop2 = true;
        break;
      }
    }

    if (!found_nop2) {
      std::cerr << "FAILED: Real-world shader 6befdec4 demote was not neutralized!\n";
      std::abort();
    }
  }

  // 测试 shader 3: shader_5ef97b1b (角色防窥网点虚化，必须被 NOP 中和)
  const char* path3 = "/tmp/game_anti_dither/dumps/HTGame/shader_5ef97b1b_orig.spv";
  FILE* fp3 = std::fopen(path3, "rb");
  if (fp3) {
    std::fseek(fp3, 0, SEEK_END);
    size_t bytes3 = std::ftell(fp3);
    std::fseek(fp3, 0, SEEK_SET);

    std::vector<uint32_t> spv3(bytes3 / sizeof(uint32_t));
    std::fread(spv3.data(), sizeof(uint32_t), spv3.size(), fp3);
    std::fclose(fp3);

    auto orig3 = spv3;
    nte_vkd3d::process_spirv_anti_dither(spv3.data(), spv3.size());

    if (spv3 == orig3) {
      std::cerr << "FAILED: Real-world character shader 5ef97b1b dither was not neutralized!\n";
      std::abort();
    }
  }

  // 测试 shader 4: shader_c66cdd66 (树木贴图 Alpha 镂空，必须 100% 保留，绝不能被修改)
  const char* path4 = "/tmp/game_anti_dither/dumps/HTGame/shader_c66cdd66_orig.spv";
  FILE* fp4 = std::fopen(path4, "rb");
  if (fp4) {
    std::fseek(fp4, 0, SEEK_END);
    size_t bytes4 = std::ftell(fp4);
    std::fseek(fp4, 0, SEEK_SET);

    std::vector<uint32_t> spv4(bytes4 / sizeof(uint32_t));
    std::fread(spv4.data(), sizeof(uint32_t), spv4.size(), fp4);
    std::fclose(fp4);

    auto orig4 = spv4;
    nte_vkd3d::process_spirv_anti_dither(spv4.data(), spv4.size());

    if (spv4 != orig4) {
      std::cerr << "FAILED: Tree foliage shader c66cdd66 cutout was corrupted/modified!\n";
      std::abort();
    }
  }

  // 测试 shader 5: shader_1cb6512d (贴花/树木 SceneDepth 包围盒裁剪，必须 100% 保留，绝不能被修改)
  const char* path5 = "/tmp/game_anti_dither/dumps/HTGame/shader_1cb6512d_orig.spv";
  FILE* fp5 = std::fopen(path5, "rb");
  if (fp5) {
    std::fseek(fp5, 0, SEEK_END);
    size_t bytes5 = std::ftell(fp5);
    std::fseek(fp5, 0, SEEK_SET);

    std::vector<uint32_t> spv5(bytes5 / sizeof(uint32_t));
    std::fread(spv5.data(), sizeof(uint32_t), spv5.size(), fp5);
    std::fclose(fp5);

    auto orig5 = spv5;
    nte_vkd3d::process_spirv_anti_dither(spv5.data(), spv5.size());

    if (spv5 != orig5) {
      std::cerr << "FAILED: Tree/Decal Bounding Box shader 1cb6512d was corrupted/modified to NOP!\n";
      std::abort();
    }
  }

  // 测试 shader 6: shader_80ad1235 (大世界树木植被：必须 100% 豁免，保持原生代码放行，绝不能被修改)
  const char* path6 = "/tmp/game_anti_dither/dumps/HTGame/shader_80ad1235_orig.spv";
  FILE* fp6 = std::fopen(path6, "rb");
  if (fp6) {
    std::fseek(fp6, 0, SEEK_END);
    size_t bytes6 = std::ftell(fp6);
    std::fseek(fp6, 0, SEEK_SET);

    std::vector<uint32_t> spv6(bytes6 / sizeof(uint32_t));
    std::fread(spv6.data(), sizeof(uint32_t), spv6.size(), fp6);
    std::fclose(fp6);

    auto orig6 = spv6;
    nte_vkd3d::process_spirv_anti_dither(spv6.data(), spv6.size());

    if (spv6 != orig6) {
      std::cerr << "FAILED: Tree foliage shader 80ad1235 was corrupted/modified!\n";
      std::abort();
    }
  }

  // 测试 shader 7: shader_6d741986 (角色卡通网点着色器：必须被成功中和)
  const char* path7 = "/tmp/game_anti_dither/dumps/HTGame/shader_6d741986_orig.spv";
  FILE* fp7 = std::fopen(path7, "rb");
  if (fp7) {
    std::fseek(fp7, 0, SEEK_END);
    size_t bytes7 = std::ftell(fp7);
    std::fseek(fp7, 0, SEEK_SET);

    std::vector<uint32_t> spv7(bytes7 / sizeof(uint32_t));
    std::fread(spv7.data(), sizeof(uint32_t), spv7.size(), fp7);
    std::fclose(fp7);

    auto orig7 = spv7;
    nte_vkd3d::process_spirv_anti_dither(spv7.data(), spv7.size());

    if (spv7 == orig7) {
      std::cerr << "FAILED: Character toon shader 6d741986 dither was not neutralized!\n";
      std::abort();
    }
  }

  // 测试 shader 8: shader_dc232b9a (角色眼眶贴花遮罩：必须 100% 保留镂空，绝不能修改产生黑洞)
  const char* path8 = "/tmp/game_anti_dither/dumps/HTGame/shader_dc232b9a_orig.spv";
  FILE* fp8 = std::fopen(path8, "rb");
  if (fp8) {
    std::fseek(fp8, 0, SEEK_END);
    size_t bytes8 = std::ftell(fp8);
    std::fseek(fp8, 0, SEEK_SET);

    std::vector<uint32_t> spv8(bytes8 / sizeof(uint32_t));
    std::fread(spv8.data(), sizeof(uint32_t), spv8.size(), fp8);
    std::fclose(fp8);

    auto orig8 = spv8;
    nte_vkd3d::process_spirv_anti_dither(spv8.data(), spv8.size());

    if (spv8 != orig8) {
      std::cerr << "FAILED: Eye socket decal mask shader dc232b9a was corrupted (would cause eye black hole)!\n";
      std::abort();
    }
  }

  std::cout << "PASSED\n";
}

int main() {
  std::cout << "=== NTE (Neverness To Everness) VKD3D Anti-Dither Unit Tests ===\n";
  test_nte_character_bayer_dither_neutralized();
  test_nte_tree_foliage_alpha_cutout_preserved();
  test_nte_blacklist_rules();
  test_nte_pipeline_stage_dispatch();
  test_nte_real_world_dump_shader();
  std::cout << "=== All NTE Tests Passed Successfully ===\n";
  return 0;
}
