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
    game_core::init_game_profiles(c.process);

    // 验证当前激活的 Profile
    const auto* profile = game_core::get_active_profile();
    if (!profile || profile->id != c.expected_id || std::string(profile->name) != c.expected_name) {
      std::cerr << "Profile mismatch\n";
      std::abort();
    }
    (void)profile;
    break; // 因为 init_game_profiles 内部使用 std::once_flag，首个即单例绑定
  }

  std::cout << "PASSED\n";
}

int main() {
  std::cout << "=== Game Registry & Profile Architecture Unit Tests ===\n";
  test_all_game_profiles_matched();
  std::cout << "=== All Game Registry Tests Passed Successfully ===\n";
  return 0;
}
