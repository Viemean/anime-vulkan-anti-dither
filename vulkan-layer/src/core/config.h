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

  /**
   * @brief 获取全局配置与日志操作的递归互斥锁。
   *
   * @return 全局递归互斥锁引用。
   */
  std::recursive_mutex& get_logger_mutex();

  /**
   * @brief 初始化全局配置环境，探测进程名并载入配置文件与环境变量。
   * 线程安全，仅在首次调用时执行初始化流程。
   */
  void init_config();

  /**
   * @brief 从用户配置目录读取 rules.conf 并解析键值对配置。
   * 若配置文件不存在或不可读，则维持默认状态。
   */
  void load_config_file();

  /**
   * @brief 解析由逗号或分号分隔的着色器哈希值列表字符串。
   *
   * @param env_val 包含哈希字符串的字符指针，支持 16 进制 (0x 前缀) 与 10 进制格式。
   * @param out_set 存储解析所得 32 位哈希值的集合引用。
   */
  void parse_hash_list(const char* env_val, std::unordered_set<uint32_t>& out_set);

  /**
   * @brief 获取当前进程从操作系统检索到的原始名称。
   *
   * @return 原始进程名称字符串。
   */
  std::string get_process_name();

  /**
   * @brief 获取去除路径和可执行扩展名的清洗后进程名称。
   *
   * @return 清洗后的基准进程名称。
   */
  std::string get_clean_process_name();

  /**
   * @brief 查询当前反虚化驱动层是否针对当前运行进程处于激活状态。
   *
   * @return 激活返回 true，未激活返回 false。
   */
  bool is_active();

  /**
   * @brief 查询当前运行环境是否启用诊断日志输出。
   *
   * @return 启用返回 true，否则返回 false。
   */
  bool is_log_enabled();

  /**
   * @brief 查询当前运行环境是否启用 SPIR-V 着色器转储导出。
   *
   * @return 启用返回 true，否则返回 false。
   */
  bool is_dump_enabled();

  /**
   * @brief 查询是否启用游戏进程帧率解锁。
   *
   * @return 启用返回 true，否则返回 false。
   */
  bool is_fps_unlock_enabled();

  /**
   * @brief 获取配置的目标帧率数值。
   *
   * @return 目标帧率数值，未配置或关闭时返回 0。
   */
  int32_t get_target_fps();

  /**
   * @brief 获取引擎实际期望设置的生效帧率值。
   *
   * @return 若目标帧率有效则返回目标值，否则返回引擎默认基准值。
   */
  int32_t get_effective_engine_fps();

  /**
   * @brief 查询指定轻量 Mod 开关状态，遵循环境变量优先于配置文件的覆盖规则。
   *
   * @param mod_name Mod 配置项标识符（如 "zmd_nomask"）。
   * @return 启用返回 true，否则返回 false。
   */
  bool is_mod_enabled(std::string_view mod_name);


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

  /**
   * @brief 计算 SPIR-V 字节码序列的 32 位 FNV-1a 哈希校验和。
   *
   * @param code 指向 SPIR-V 32 位字数组的指针。
   * @param word_count 字节码包含的 32 位字数量。
   * @return 计算得到的 32 位无符号哈希值。
   */
  inline uint32_t compute_spirv_hash(const uint32_t* code, size_t word_count) {
    uint32_t hash = 2166136261u;
    for (size_t i = 0; i < word_count; ++i) {
      hash ^= code[i];
      hash *= 16777619u;
    }
    return hash;
  }

} // namespace game_core
