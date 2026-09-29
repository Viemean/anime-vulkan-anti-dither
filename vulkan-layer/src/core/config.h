#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <unordered_set>
#include <mutex>
#include <string_view>
#include "../game_profile.h"

namespace game_core {

  // 全局核心状态变量 (声明为 extern，统一在 config.cpp 中定义)
  extern bool g_initialized;
  extern bool g_enabled;
  extern int g_config_enabled;
  extern bool g_dump_enabled;
  extern bool g_log_enabled;
  extern std::string g_detected_process;
  extern std::string g_clean_process_name;
  extern std::string g_config_targets;
  extern std::unordered_set<uint32_t> g_exclude_hashes;
  extern std::unordered_set<uint32_t> g_force_hashes;
  extern bool g_force_all;
  extern uint32_t g_force_hash_min;
  extern uint32_t g_force_hash_max;
  extern bool g_fps_unlock_enabled;
  extern int g_config_fps_unlock;
  extern int32_t g_target_fps;
  extern bool g_zmd_nomask_enabled;
  extern int g_config_zmd_nomask;

  std::recursive_mutex& get_logger_mutex();

  void init_config();
  void load_config_file();
  void parse_hash_list(const char* env_val, std::unordered_set<uint32_t>& out_set);

  std::string get_process_name();
  std::string get_clean_process_name();

  bool is_active();
  bool is_log_enabled();
  bool is_dump_enabled();
  bool is_fps_unlock_enabled();
  int32_t get_target_fps();
  int32_t get_effective_engine_fps();
  bool is_zmd_nomask_enabled();

  // 各游戏专属状态查询 (基于 GameId 枚举比对)
  inline bool is_nte() {
    init_config();
    return get_active_game_id() == GameId::NTE;
  }
  inline bool is_azur_promilia() {
    init_config();
    return get_active_game_id() == GameId::AzurPromilia;
  }
  inline bool is_hsr() {
    init_config();
    return get_active_game_id() == GameId::HSR;
  }
  inline bool is_genshin() {
    init_config();
    return get_active_game_id() == GameId::Genshin;
  }
  inline bool is_wuwa() {
    init_config();
    return get_active_game_id() == GameId::WuWa;
  }
  inline bool is_zzz() {
    init_config();
    return get_active_game_id() == GameId::ZZZ;
  }
  inline bool is_hi3() {
    init_config();
    return get_active_game_id() == GameId::HI3rd;
  }
  inline bool is_zmd() {
    init_config();
    return get_active_game_id() == GameId::ZMD;
  }
  inline bool is_gf2() {
    init_config();
    return get_active_game_id() == GameId::GF2;
  }
  inline bool is_tof() {
    init_config();
    return get_active_game_id() == GameId::TOF;
  }
  inline bool is_dna() {
    init_config();
    return get_active_game_id() == GameId::DNA;
  }
  inline bool is_star() {
    init_config();
    return get_active_game_id() == GameId::Star;
  }

  // FNV-1a 32-bit 哈希计算
  inline uint32_t compute_spirv_hash(const uint32_t* code, size_t word_count) {
    uint32_t hash = 2166136261u;
    for (size_t i = 0; i < word_count; ++i) {
      hash ^= code[i];
      hash *= 16777619u;
    }
    return hash;
  }

} // namespace game_core
