#pragma once

#include <string>
#include <cstdlib>
#include <filesystem>

namespace test_utils {

  /**
   * @brief 获取测试用着色器转储根目录路径。
   *
   * 优先检索环境变量 ANTI_DITHER_TEST_DUMP_DIR；
   * 未配置时基于系统临时目录拼接标准转储路径。
   *
   * @return 转储根目录的 filesystem::path 对象。
   */
  inline std::filesystem::path get_test_dump_root() {
    const char* env_dir = std::getenv("ANTI_DITHER_TEST_DUMP_DIR");
    if (env_dir && env_dir[0] != '\0') {
      return std::filesystem::path(env_dir);
    }
    std::error_code ec;
    auto tmp = std::filesystem::temp_directory_path(ec);
    if (!ec) {
      return tmp / "game_anti_dither" / "dumps";
    }
    return std::filesystem::path("/tmp/game_anti_dither/dumps");
  }

  /**
   * @brief 拼接特定游戏与可选着色器文件名的完整测试转储路径。
   *
   * @param game_dir 游戏对应转储子目录名称。
   * @param filename 可选的着色器文件名。
   * @return 解析后的完整路径字符串。
   */
  inline std::string get_game_dump_path(const std::string& game_dir, const std::string& filename = "") {
    auto root = get_test_dump_root();
    if (filename.empty()) {
      return (root / game_dir).string();
    }
    return (root / game_dir / filename).string();
  }

  /**
   * @brief 获取跨平台的系统临时测试文件路径。
   *
   * @param filename 临时文件名。
   * @return 解析后的完整临时路径字符串。
   */
  inline std::string get_temp_test_path(const std::string& filename) {
    std::error_code ec;
    auto tmp = std::filesystem::temp_directory_path(ec);
    if (!ec) {
      return (tmp / filename).string();
    }
    return std::string("/tmp/") + filename;
  }

} // namespace test_utils
