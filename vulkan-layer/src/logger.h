#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdarg>
#include <ctime>
#include <string>
#include <vector>
#include <unordered_set>
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>
#include <mutex>
#include <string_view>

namespace game_logger {

  inline std::mutex& get_logger_mutex() {
    static std::mutex s_mutex;
    return s_mutex;
  }

  static bool g_initialized = false;
  static bool g_enabled = false;
  static int g_config_enabled = -1; // -1: 未在配置中指定, 0: 显式禁用, 1: 显式启用
  static bool g_dump_enabled = false;
  static bool g_log_enabled = false; // 发行版默认静默，零磁盘 I/O 开销
  static std::string g_detected_process;
  static std::string g_clean_process_name;
  static std::unordered_set<uint32_t> g_exclude_hashes;
  static std::unordered_set<uint32_t> g_force_hashes;

  inline void init_config();
  inline bool is_log_enabled();

  inline void make_dirs(const std::string& path) {
    std::string current;
    for (size_t i = 0; i < path.length(); ++i) {
      current += path[i];
      if (path[i] == '/' || i == path.length() - 1) {
        if (!current.empty() && current != "/") {
          mkdir(current.c_str(), 0777);
        }
      }
    }
  }

  inline std::string get_process_name() {
    std::ifstream cmdline("/proc/self/cmdline");
    if (!cmdline.is_open())
      return "";
    std::string s;
    std::getline(cmdline, s, '\0');
    return s;
  }

  inline std::string get_clean_process_name() {
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

  inline void log_msg(const char* fmt, ...) {
#ifdef DISABLE_LOGGING
    (void)fmt;
    return;
#else
    if (!is_log_enabled()) {
      return;
    }

    std::lock_guard<std::mutex> lock(get_logger_mutex());
    va_list args;
    std::string proc = get_clean_process_name();
    const char* home = std::getenv("HOME");

    std::string tmp_dir = "/tmp/game_anti_dither/logs";
    make_dirs(tmp_dir);
    std::string tmp_log = tmp_dir + "/" + proc + ".log";
    std::string tmp_latest = tmp_dir + "/latest.log";

    std::string home_log, home_latest;
    if (home) {
      std::string home_dir = std::string(home) + "/.local/share/game_anti_dither/logs";
      make_dirs(home_dir);
      home_log = home_dir + "/" + proc + ".log";
      home_latest = home_dir + "/latest.log";
    }

    char time_buf[64];
    std::time_t t = std::time(nullptr);
    std::strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", std::localtime(&t));

    FILE* files[4] = {
      std::fopen(tmp_log.c_str(), "a"),
      std::fopen(tmp_latest.c_str(), "a"),
      home_log.empty() ? nullptr : std::fopen(home_log.c_str(), "a"),
      home_latest.empty() ? nullptr : std::fopen(home_latest.c_str(), "a")
    };

    va_start(args, fmt);
    for (int idx = 0; idx < 4; ++idx) {
      if (files[idx]) {
        std::fprintf(files[idx], "[%s] ", time_buf);
        va_list a;
        va_copy(a, args);
        std::vfprintf(files[idx], fmt, a);
        va_end(a);
        std::fclose(files[idx]);
      }
    }
    va_end(args);
#endif
  }

  inline void parse_hash_list(const char* env_val, std::unordered_set<uint32_t>& out_set) {
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

  inline void load_config_file() {
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
        g_dump_enabled = (val == "1" || val == "true");
      } else if (key == "enabled" || key == "anti_dither") {
        g_config_enabled = (val == "1" || val == "true") ? 1 : 0;
        g_enabled = (g_config_enabled == 1);
      } else if (key == "log" || key == "logging" || key == "debug" || key == "enable_log") {
        g_log_enabled = (val == "1" || val == "true");
      }
    }
  }

  inline void init_config() {
    std::lock_guard<std::mutex> lock(get_logger_mutex());
    if (g_initialized)
      return;
    g_initialized = true;

    g_detected_process = get_process_name();
    get_clean_process_name();

    // 1. 默认状态：依据进程名白名单进行自动检测
    bool auto_detect_enabled = false;
    const char* custom_target = std::getenv("ANTI_DITHER_TARGETS");
    if (!custom_target)
      custom_target = std::getenv("WUWA_TARGETS");

    if (custom_target && !g_detected_process.empty() && g_detected_process.find(custom_target) != std::string::npos) {
      auto_detect_enabled = true;
    } else if (
        // Wuthering Waves (Unreal Engine 4/5)
        g_detected_process.find("Client-Win64-Shipping") != std::string::npos ||
        g_detected_process.find("WutheringWaves") != std::string::npos ||
        g_detected_process.find("Wuthering Waves") != std::string::npos ||
        g_detected_process.find("Client-Win64") != std::string::npos ||
        g_detected_process.find("wuwa") != std::string::npos ||
        // MiHoYo Games (Unity Engine)
        g_detected_process.find("GenshinImpact") != std::string::npos ||
        g_detected_process.find("YuanShen") != std::string::npos ||
        g_detected_process.find("StarRail") != std::string::npos ||
        g_detected_process.find("ZenlessZoneZero") != std::string::npos ||
        g_detected_process.find("BH3") != std::string::npos ||
        g_detected_process.find("Honkai Impact 3") != std::string::npos ||
        // Azur Promilia (UE)
        g_detected_process.find("AzurPromilia") != std::string::npos ||
        g_detected_process.find("Azur Promilia") != std::string::npos ||
        g_detected_process.find("azur_promilia") != std::string::npos ||
        g_detected_process.find("AP-Win64") != std::string::npos ||
        g_detected_process.find("Promilia") != std::string::npos ||
        // Neverness To Everness (HTGame / UE5)
        g_detected_process.find("HTGame") != std::string::npos ||
        g_detected_process.find("HT-Win64") != std::string::npos ||
        g_detected_process.find("NevernessToEverness") != std::string::npos ||
        g_detected_process.find("HT") != std::string::npos ||
        // Other Anime / UE / Unity Games
        g_detected_process.find("Snowbreak") != std::string::npos ||
        g_detected_process.find("DuetNightAbyss") != std::string::npos ||
        g_detected_process.find("NarakaBladepoint") != std::string::npos ||
        g_detected_process.find("Naraka") != std::string::npos ||
        g_detected_process.find("InfinityNikki") != std::string::npos ||
        g_detected_process.find("ProjectMugen") != std::string::npos ||
        g_detected_process.find("QRSL") != std::string::npos ||
        g_detected_process.find("TOF") != std::string::npos) {
      auto_detect_enabled = true;
    }

    // 2. 静态配置文件 rules.conf 覆盖默认的进程名检测
    load_config_file();
    if (g_config_enabled != -1) {
      g_enabled = (g_config_enabled == 1);
    } else {
      g_enabled = auto_detect_enabled;
    }

    // 3. 环境变量具备最高覆盖优先级
    const char* env_dither = std::getenv("ANTI_DITHER_ENABLED");
    if (!env_dither)
      env_dither = std::getenv("WUWA_ANTI_DITHER");

    if (env_dither) {
      g_enabled = (std::strcmp(env_dither, "0") != 0 && std::strcmp(env_dither, "false") != 0);
    }

    // 环境变量具备最高覆盖优先级
    const char* env_dump = std::getenv("ANTI_DITHER_DUMP");
    if (!env_dump)
      env_dump = std::getenv("WUWA_DUMP_SHADERS");
    if (env_dump)
      g_dump_enabled = (std::strcmp(env_dump, "1") == 0 || std::strcmp(env_dump, "true") == 0);

    const char* env_log = std::getenv("ANTI_DITHER_LOG");
    if (!env_log)
      env_log = std::getenv("ANTI_DITHER_DEBUG");
    if (!env_log)
      env_log = std::getenv("WUWA_LOG");
    if (env_log)
      g_log_enabled = (std::strcmp(env_log, "1") == 0 || std::strcmp(env_log, "true") == 0);

    const char* env_exclude = std::getenv("ANTI_DITHER_EXCLUDE_HASHES");
    if (env_exclude)
      parse_hash_list(env_exclude, g_exclude_hashes);

    const char* env_force = std::getenv("ANTI_DITHER_FORCE_HASHES");
    if (env_force)
      parse_hash_list(env_force, g_force_hashes);
  }

  inline bool is_log_enabled() {
    init_config();
    return g_log_enabled;
  }

  inline bool is_active() {
    init_config();
    return g_enabled;
  }

  inline bool is_nte() {
    init_config();
    return g_detected_process.find("HTGame") != std::string::npos ||
           g_detected_process.find("HT-Win64") != std::string::npos ||
           g_detected_process.find("NevernessToEverness") != std::string::npos;
  }

  inline bool is_azur_promilia() {
    init_config();
    return g_detected_process.find("AzurPromilia") != std::string::npos ||
           g_detected_process.find("Azur Promilia") != std::string::npos ||
           g_detected_process.find("azur_promilia") != std::string::npos ||
           g_detected_process.find("AP-Win64") != std::string::npos ||
           g_detected_process.find("Promilia") != std::string::npos;
  }

  inline bool is_hsr() {
    init_config();
    return g_detected_process.find("StarRail") != std::string::npos ||
           g_detected_process.find("Star Rail") != std::string::npos;
  }

  inline bool is_genshin() {
    init_config();
    return g_detected_process.find("YuanShen") != std::string::npos ||
           g_detected_process.find("GenshinImpact") != std::string::npos ||
           g_detected_process.find("Genshin Impact") != std::string::npos ||
           g_detected_process.find("Genshin") != std::string::npos;
  }

  inline uint32_t compute_spirv_hash(const uint32_t* code, size_t word_count) {
    uint32_t hash = 2166136261u;
    for (size_t i = 0; i < word_count; ++i) {
      hash ^= code[i];
      hash *= 16777619u;
    }
    return hash;
  }

  inline void dump_shader_bundle(const uint32_t* orig_code, size_t orig_words,
                                 const uint32_t* patched_code, size_t patched_words,
                                 uint32_t hash, uint32_t dither_nops, uint32_t preserved,
                                 bool has_frag_coord, bool has_sample) {
#ifdef DISABLE_LOGGING
    (void)orig_code; (void)orig_words; (void)patched_code; (void)patched_words;
    (void)hash; (void)dither_nops; (void)preserved; (void)has_frag_coord; (void)has_sample;
    return;
#else
    init_config();
    if (!g_dump_enabled)
      return;

    std::lock_guard<std::mutex> lock(get_logger_mutex());
    std::string proc = get_clean_process_name();
    const char* home = std::getenv("HOME");

    std::vector<std::string> base_dirs;
    base_dirs.push_back("/tmp/game_anti_dither/dumps/" + proc);
    if (home) {
      base_dirs.push_back(std::string(home) + "/.local/share/game_anti_dither/dumps/" + proc);
    }

    for (const auto& dir : base_dirs) {
      make_dirs(dir);
      char path_orig[512];
      char path_patched[512];
      char path_report[512];

      std::snprintf(path_orig, sizeof(path_orig), "%s/shader_%08x_orig.spv", dir.c_str(), hash);
      std::snprintf(path_patched, sizeof(path_patched), "%s/shader_%08x_patched.spv", dir.c_str(), hash);
      std::snprintf(path_report, sizeof(path_report), "%s/shader_%08x_report.txt", dir.c_str(), hash);

      FILE* f_orig = std::fopen(path_orig, "wb");
      if (f_orig) {
        std::fwrite(orig_code, sizeof(uint32_t), orig_words, f_orig);
        std::fclose(f_orig);
      }

      FILE* f_patched = std::fopen(path_patched, "wb");
      if (f_patched) {
        std::fwrite(patched_code, sizeof(uint32_t), patched_words, f_patched);
        std::fclose(f_patched);
      }

      FILE* f_rep = std::fopen(path_report, "w");
      if (f_rep) {
        std::fprintf(f_rep, "Shader Hash: 0x%08x\n", hash);
        std::fprintf(f_rep, "Process: %s\n", g_detected_process.c_str());
        std::fprintf(f_rep, "Words: %zu\n", orig_words);
        std::fprintf(f_rep, "Dither NOPed: %u\n", dither_nops);
        std::fprintf(f_rep, "Cutout Preserved: %u\n", preserved);
        std::fprintf(f_rep, "Has FragCoord: %s\n", has_frag_coord ? "true" : "false");
        std::fprintf(f_rep, "Has Image Sample: %s\n", has_sample ? "true" : "false");
        std::fclose(f_rep);
      }
    }
#endif
  }

  inline void dump_character_vertex_shader(const uint32_t* vs_code, size_t vs_words,
                                           uint32_t vs_hash, uint32_t fs_hash) {
#ifdef DISABLE_LOGGING
    (void)vs_code; (void)vs_words; (void)vs_hash; (void)fs_hash;
    return;
#else
    init_config();
    if (!g_dump_enabled)
      return;

    std::lock_guard<std::mutex> lock(get_logger_mutex());
    std::string proc = get_clean_process_name();
    const char* home = std::getenv("HOME");

    std::vector<std::string> base_dirs;
    base_dirs.push_back("/tmp/game_anti_dither/dumps/" + proc);
    if (home) {
      base_dirs.push_back(std::string(home) + "/.local/share/game_anti_dither/dumps/" + proc);
    }

    for (const auto& dir : base_dirs) {
      make_dirs(dir);
      char path_vs[512];
      char path_report[512];

      std::snprintf(path_vs, sizeof(path_vs), "%s/shader_vs_%08x_fs_%08x.spv", dir.c_str(), vs_hash, fs_hash);
      std::snprintf(path_report, sizeof(path_report), "%s/shader_vs_%08x_fs_%08x_report.txt", dir.c_str(), vs_hash, fs_hash);

      FILE* f_vs = std::fopen(path_vs, "wb");
      if (f_vs) {
        std::fwrite(vs_code, sizeof(uint32_t), vs_words, f_vs);
        std::fclose(f_vs);
      }

      FILE* f_rep = std::fopen(path_report, "w");
      if (f_rep) {
        std::fprintf(f_rep, "Vertex Shader Hash: 0x%08x\n", vs_hash);
        std::fprintf(f_rep, "Paired Fragment Shader Hash: 0x%08x\n", fs_hash);
        std::fprintf(f_rep, "Process: %s\n", g_detected_process.c_str());
        std::fprintf(f_rep, "Words: %zu\n", vs_words);
        std::fclose(f_rep);
      }
    }
#endif
  }

} // namespace game_logger
