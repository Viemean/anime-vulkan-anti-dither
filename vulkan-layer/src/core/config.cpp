#include "config.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <strings.h>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <mutex>

namespace game_core {

  // 全局核心状态变量定义
  bool g_initialized = false;
  bool g_enabled = false;
  int g_config_enabled = -1;
  bool g_dump_enabled = false;
  bool g_log_enabled = false;
  std::string g_detected_process;
  std::string g_clean_process_name;
  std::string g_config_targets;
  std::unordered_set<uint32_t> g_exclude_hashes;
  std::unordered_set<uint32_t> g_force_hashes;
  bool g_force_all = false;
  uint32_t g_force_hash_min = 0x00000000u;
  uint32_t g_force_hash_max = 0xFFFFFFFFu;
  bool g_fps_unlock_enabled = false;
  int g_config_fps_unlock = -1;
  int32_t g_target_fps = 120;
  bool g_zmd_nomask_enabled = false;
  int g_config_zmd_nomask = -1;

  std::recursive_mutex& get_logger_mutex() {
    static std::recursive_mutex s_mutex;
    return s_mutex;
  }

  std::string get_process_name() {
    std::ifstream cmdline("/proc/self/cmdline");
    if (!cmdline.is_open())
      return "";
    std::string s;
    std::getline(cmdline, s, '\0');
    return s;
  }

  std::string get_clean_process_name() {
    if (!g_clean_process_name.empty())
      return g_clean_process_name;

    std::string full = get_process_name();
    if (full.empty())
      return "generic_game";

    size_t p = full.find_last_of("/\\");
    std::string base = (p == std::string::npos) ? full : full.substr(p + 1);
    if (base.size() > 4 && base.substr(base.size() - 4) == ".exe")
      base = base.substr(0, base.size() - 4);

    for (char& c : base) {
      if (c == ' ' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|')
        c = '_';
    }

    g_clean_process_name = base;
    return g_clean_process_name;
  }

  void parse_hash_list(const char* env_val, std::unordered_set<uint32_t>& out_set) {
    if (!env_val)
      return;
    std::stringstream ss(env_val);
    std::string item;
    while (std::getline(ss, item, ',')) {
      if (item.empty())
        continue;
      try {
        uint32_t h = static_cast<uint32_t>(std::stoul(item, nullptr, 0));
        out_set.insert(h);
      } catch (...) {}
    }
  }

  void load_config_file() {
    const char* home = std::getenv("HOME");
    if (!home)
      return;

    std::string config_path = std::string(home) + "/.config/anti_dither/rules.conf";
    std::ifstream file(config_path);
    if (!file.is_open())
      return;

    std::string line;
    while (std::getline(file, line)) {
      if (line.empty() || line[0] == '#' || line[0] == ';')
        continue;
      auto eq = line.find('=');
      if (eq == std::string::npos)
        continue;
      std::string key = line.substr(0, eq);
      std::string val = line.substr(eq + 1);

      // Trim whitespace
      while (!key.empty() && (key.back() == ' ' || key.back() == '\t')) key.pop_back();
      while (!val.empty() && (val.back() == ' ' || val.back() == '\t' || val.back() == '\r')) val.pop_back();
      while (!val.empty() && (val.front() == ' ' || val.front() == '\t')) val.erase(0, 1);

      if (key == "exclude_hashes" || key == "blacklist") {
        parse_hash_list(val.c_str(), g_exclude_hashes);
      } else if (key == "force_hashes" || key == "whitelist") {
        parse_hash_list(val.c_str(), g_force_hashes);
      } else if (key == "dump" || key == "dump_shaders") {
        g_dump_enabled = (val == "1");
      } else if (key == "enabled" || key == "anti_dither") {
        g_config_enabled = (val == "1") ? 1 : ((val == "0") ? 0 : -1);
      } else if (key == "log" || key == "logging" || key == "debug" || key == "enable_log") {
        g_log_enabled = (val == "1");
      } else if (key == "targets" || key == "custom_targets") {
        g_config_targets = val;
      } else if (key == "force_all") {
        g_force_all = (val == "1");
      } else if (key == "hash_min") {
        g_force_hash_min = static_cast<uint32_t>(std::strtoul(val.c_str(), nullptr, 0));
      } else if (key == "hash_max") {
        g_force_hash_max = static_cast<uint32_t>(std::strtoul(val.c_str(), nullptr, 0));
      } else if (key == "fps_unlock" || key == "unlock_fps") {
        g_config_fps_unlock = (val == "1") ? 1 : ((val == "0") ? 0 : -1);
      } else if (key == "target_fps" || key == "fps") {
        try {
          int32_t parsed_fps = std::stoi(val);
          g_target_fps = (parsed_fps == 0 || parsed_fps == -1) ? 0 : ((parsed_fps < 30) ? 30 : parsed_fps);
        } catch (...) {}
      } else if (key == "zmd_nomask" || key == "zmd_no_mask" || key == "nomask") {
        g_config_zmd_nomask = (val == "1" || val == "true") ? 1 : ((val == "0" || val == "false") ? 0 : -1);
      }
    }
  }

  void init_config() {
    std::lock_guard<std::recursive_mutex> lock(get_logger_mutex());
    if (g_initialized)
      return;
    g_initialized = true;

    g_detected_process = get_process_name();
    get_clean_process_name();

    // 0. 初始化游戏画像注册表
    init_game_profiles(g_detected_process);

    // 1. 读取静态配置文件 rules.conf
    load_config_file();

    // 2. 依据自定义匹配项与已注册游戏画像进行自动检测
    bool auto_detect_enabled = false;
    const char* custom_target = std::getenv("ANTI_DITHER_TARGETS");
    std::string target_match = custom_target ? custom_target : g_config_targets;

    if (!target_match.empty() && !g_detected_process.empty() && g_detected_process.find(target_match) != std::string::npos) {
      auto_detect_enabled = true;
    } else if (is_game_active()) {
      auto_detect_enabled = true;
    }

    if (g_config_enabled != -1) {
      g_enabled = (g_config_enabled == 1);
    } else {
      g_enabled = auto_detect_enabled;
    }

    // 3. 环境变量具备最高覆盖优先级
    const char* env_dither = std::getenv("ANTI_DITHER_ENABLED");
    if (env_dither) {
      if (std::strcmp(env_dither, "1") == 0) g_enabled = true;
      else if (std::strcmp(env_dither, "0") == 0) g_enabled = false;
    }

    const char* env_dump = std::getenv("ANTI_DITHER_DUMP");
    if (env_dump) {
      if (std::strcmp(env_dump, "1") == 0) g_dump_enabled = true;
      else if (std::strcmp(env_dump, "0") == 0) g_dump_enabled = false;
    }

    const char* env_log = std::getenv("ANTI_DITHER_LOG");
    if (!env_log)
      env_log = std::getenv("ANTI_DITHER_DEBUG");
    if (env_log) {
      if (std::strcmp(env_log, "1") == 0) g_log_enabled = true;
      else if (std::strcmp(env_log, "0") == 0) g_log_enabled = false;
    }

    const char* env_exclude = std::getenv("ANTI_DITHER_EXCLUDE_HASHES");
    if (env_exclude)
      parse_hash_list(env_exclude, g_exclude_hashes);

    const char* env_force = std::getenv("ANTI_DITHER_FORCE_HASHES");
    if (env_force)
      parse_hash_list(env_force, g_force_hashes);

    const char* env_force_all = std::getenv("ANTI_DITHER_FORCE_ALL");
    if (env_force_all) {
      if (std::strcmp(env_force_all, "1") == 0) g_force_all = true;
      else if (std::strcmp(env_force_all, "0") == 0) g_force_all = false;
    }

    const char* env_min = std::getenv("ANTI_DITHER_HASH_MIN");
    if (env_min)
      g_force_hash_min = static_cast<uint32_t>(std::strtoul(env_min, nullptr, 0));

    const char* env_max = std::getenv("ANTI_DITHER_HASH_MAX");
    if (env_max)
      g_force_hash_max = static_cast<uint32_t>(std::strtoul(env_max, nullptr, 0));

    if (g_config_fps_unlock != -1) {
      g_fps_unlock_enabled = (g_config_fps_unlock == 1);
    }

    const char* env_unlock = std::getenv("ANTI_DITHER_UNLOCK_FPS");
    if (!env_unlock)
      env_unlock = std::getenv("ANTI_DITHER_FPS_UNLOCK");
    if (env_unlock) {
      if (std::strcmp(env_unlock, "1") == 0) g_fps_unlock_enabled = true;
      else if (std::strcmp(env_unlock, "0") == 0) g_fps_unlock_enabled = false;
    }

    const char* env_target_fps = std::getenv("ANTI_DITHER_TARGET_FPS");
    if (!env_target_fps)
      env_target_fps = std::getenv("ANTI_DITHER_FPS");
    if (env_target_fps) {
      try {
        int32_t parsed_fps = std::stoi(env_target_fps);
        g_target_fps = (parsed_fps == 0 || parsed_fps == -1) ? 0 : ((parsed_fps < 30) ? 30 : parsed_fps);
      } catch (...) {}
    }

    if (g_config_zmd_nomask != -1) {
      g_zmd_nomask_enabled = (g_config_zmd_nomask == 1);
    }

    const char* env_zmd_nomask = std::getenv("zmd_nomask");
    if (!env_zmd_nomask)
      env_zmd_nomask = std::getenv("ZMD_NOMASK");
    if (env_zmd_nomask) {
      if (std::strcmp(env_zmd_nomask, "1") == 0 || ::strcasecmp(env_zmd_nomask, "true") == 0)
        g_zmd_nomask_enabled = true;
      else if (std::strcmp(env_zmd_nomask, "0") == 0 || ::strcasecmp(env_zmd_nomask, "false") == 0)
        g_zmd_nomask_enabled = false;
    }
  }

  bool is_active() {
    init_config();
    return g_enabled;
  }

  bool is_log_enabled() {
    init_config();
    return g_log_enabled;
  }

  bool is_dump_enabled() {
    init_config();
    return g_dump_enabled;
  }

  bool is_fps_unlock_enabled() {
    init_config();
    return g_fps_unlock_enabled;
  }

  int32_t get_target_fps() {
    init_config();
    return g_target_fps;
  }

  int32_t get_effective_engine_fps() {
    init_config();
    return (g_target_fps == 0) ? -1 : g_target_fps;
  }

  bool is_zmd_nomask_enabled() {
    init_config();
    return g_zmd_nomask_enabled;
  }

} // namespace game_core
