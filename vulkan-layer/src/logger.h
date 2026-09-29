#pragma once

#include "core/config.h"
#include "core/logger.h"
#include "game_profile.h"

/**
 * @brief 驱动层日志与配置门面头文件 (Facade)
 * 
 * 保持对既有模块和单测的 100% 命名空间兼容性 (namespace game_logger)，
 * 底层实现已完全拆分至 core/config.cpp 与 core/logger.cpp 独立编译单元中。
 */
namespace game_logger {

  // 全局核心状态变量声明映射
  using game_core::g_initialized;
  using game_core::g_enabled;
  using game_core::g_config_enabled;
  using game_core::g_dump_enabled;
  using game_core::g_log_enabled;
  using game_core::g_detected_process;
  using game_core::g_clean_process_name;
  using game_core::g_config_targets;
  using game_core::g_exclude_hashes;
  using game_core::g_force_hashes;
  using game_core::g_force_all;
  using game_core::g_force_hash_min;
  using game_core::g_force_hash_max;
  using game_core::g_fps_unlock_enabled;
  using game_core::g_config_fps_unlock;
  using game_core::g_target_fps;
  using game_core::g_zmd_nomask_enabled;
  using game_core::g_config_zmd_nomask;

  // 基础方法映射
  using game_core::get_logger_mutex;
  using game_core::init_config;
  using game_core::load_config_file;
  using game_core::parse_hash_list;
  using game_core::get_process_name;
  using game_core::get_clean_process_name;
  using game_core::rotate_file_backups;
  using game_core::rotate_directory_backups;

  // 状态与配置查询
  using game_core::is_active;
  using game_core::is_log_enabled;
  using game_core::is_dump_enabled;
  using game_core::is_fps_unlock_enabled;
  using game_core::get_target_fps;
  using game_core::get_effective_engine_fps;
  using game_core::is_zmd_nomask_enabled;

  // 各游戏专属状态查询
  using game_core::is_nte;
  using game_core::is_azur_promilia;
  using game_core::is_hsr;
  using game_core::is_genshin;
  using game_core::is_wuwa;
  using game_core::is_zzz;
  using game_core::is_hi3;
  using game_core::is_zmd;
  using game_core::is_gf2;
  using game_core::is_tof;
  using game_core::is_dna;
  using game_core::is_star;

  // 哈希与转储接口
  using game_core::compute_spirv_hash;
  using game_core::log_msg;
  using game_core::dump_shader_bundle;
  using game_core::dump_character_vertex_shader;

} // namespace game_logger
