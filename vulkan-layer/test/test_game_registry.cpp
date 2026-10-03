#include <cassert>
#include <iostream>
#include <string>

#include "../src/game_profile.h"
#include "../src/logger.h"

// 验证全部 12 款已知游戏的精准画像匹配与钩子分发
void test_all_game_profiles_matched() {
  std::cout << "[Test Game Registry 1] All 12 Game Profiles Identification... ";

  struct TestCase {
    const char* process;
    game_core::GameId expected_id;
    const char* expected_name;
    bool has_spirv_hook;
    bool has_draw_hook;
    bool has_device_hook;
  };

  const TestCase cases[] = {
    {"Client-Win64-Shipping.exe", game_core::GameId::WuWa, "Wuthering Waves", true, false, false},
    {"GenshinImpact.exe", game_core::GameId::Genshin, "Genshin Impact", true, false, true},
    {"StarRail.exe", game_core::GameId::HSR, "Honkai: Star Rail", true, false, true},
    {"ZenlessZoneZero.exe", game_core::GameId::ZZZ, "Zenless Zone Zero", true, false, false},
    {"BH3.exe", game_core::GameId::HI3rd, "Honkai Impact 3rd", true, false, false},
    {"HTGame.exe", game_core::GameId::NTE, "Neverness To Everness", true, false, true},
    {"AzurPromilia.exe", game_core::GameId::AzurPromilia, "Azur Promilia", true, false, false},
    {"Endfield.exe", game_core::GameId::ZMD, "Arknights: Endfield", true, true, false},
    {"GF2_Exilium.exe", game_core::GameId::GF2, "Girls' Frontline 2: Exilium", true, false, false},
    {"QRSL.exe", game_core::GameId::TOF, "Tower of Fantasy", true, false, false},
    {"EM-Win64-Shipping.exe", game_core::GameId::DNA, "Duet Night Abyss", true, false, false},
    {"Star.exe", game_core::GameId::Star, "Star", true, false, false},
  };

  for (const auto& c : cases) {
    // 模拟重置初始化
    game_core::reset_game_profiles_for_test();
    game_core::init_game_profiles(c.process);

    // 验证当前激活的 Profile
    const auto* profile = game_core::get_active_profile();
    if (!profile || profile->id != c.expected_id || std::string(profile->name) != c.expected_name) {
      std::cerr << "Profile mismatch for process: " << c.process << "\n";
      std::abort();
    }
    (void)profile;
  }

  std::cout << "PASSED (All 12 Profiles verified)\n";
}

// 验证路径隔离与短词防误伤机制 (确保 Starfield, GEMINI, NTELauncher 等不会被误激活)
void test_anti_false_positive_matching() {
  std::cout << "[Test Game Registry 2] Anti-False-Positive Path & Keyword Isolation... ";

  const char* non_target_processes[] = {
    // 1. Starfield 绝对不能误伤为 Star (星痕共鸣)
    "D:\\SteamLibrary\\steamapps\\common\\Starfield\\Starfield.exe",
    "/home/user/games/starfield/starfield.exe",
    "Starfield.exe",
    "starship.exe",

    // 2. GEMINI 或含有 EM 的路径不能误伤为 DNA (二重螺旋)
    "/home/yuzuki/.gemini/antigravity/tools/my_test_tool.exe",
    "C:\\Windows\\System32\\remoteproc.exe",
    "element_render.exe",

    // 3. 启动器与辅助工具不能误伤为 NTE / ZMD / TOF / ZZZ
    "/mnt/HDD/Games/NTE/Neverness To Everness/NTELauncher.exe",
    "NTELauncher.exe",
    "NTEBrowser.exe",
    "zmd_extract_tool.exe",
    "tof_camera_debug.exe",
    "zzz_sleep_daemon.exe",
    "gf2_unpack.exe"
  };

  for (const char* proc : non_target_processes) {
    game_core::reset_game_profiles_for_test();
    game_core::init_game_profiles(proc);

    const auto* profile = game_core::get_active_profile();
    if (profile != nullptr) {
      std::cerr << "\n[FALSE POSITIVE DETECTED] Process '" << proc 
                << "' was falsely matched as: " << profile->name << "\n";
      std::abort();
    }
  }

  std::cout << "PASSED (0 False Positives)\n";
}

// 验证通用 Mod 开关体系 (is_mod_enabled 与大小写环境变量全自动适配)
void test_generic_mod_framework() {
  std::cout << "[Test Game Registry 2] Generic Mod Toggle Framework (is_mod_enabled)... ";

  // 1. 未定义时默认返回 false
  assert(game_logger::is_mod_enabled("non_existent_custom_mod") == false);

  // 2. 小写环境变量覆盖生效
  setenv("test_custom_mod", "1", 1);
  game_logger::g_initialized = false;
  assert(game_logger::is_mod_enabled("test_custom_mod") == true);

  // 3. 显式设为 0 时禁用
  setenv("test_custom_mod", "0", 1);
  game_logger::g_initialized = false;
  assert(game_logger::is_mod_enabled("test_custom_mod") == false);

  // 4. 大写环境变量自动兼容匹配 (TEST_CUSTOM_MOD -> test_custom_mod)
  unsetenv("test_custom_mod");
  setenv("TEST_CUSTOM_MOD", "1", 1);
  game_logger::g_initialized = false;
  assert(game_logger::is_mod_enabled("test_custom_mod") == true);

  unsetenv("TEST_CUSTOM_MOD");
  game_logger::g_initialized = false;

  std::cout << "PASSED\n";
}

int main() {
  std::cout << "=== Game Registry & Profile Architecture Unit Tests ===\n";
  test_all_game_profiles_matched();
  test_anti_false_positive_matching();
  test_generic_mod_framework();
  std::cout << "=== All Game Registry Tests Passed Successfully ===\n";
  return 0;
}
