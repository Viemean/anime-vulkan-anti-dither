#pragma once

#include <cstdint>
#include <cstddef>
#include <string_view>
#include <vulkan/vulkan.h>

namespace game_core {

  enum class GameId : uint16_t {
    Unknown = 0,
    WuWa,
    Genshin,
    HSR,
    ZZZ,
    HI3rd,
    NTE,
    AzurPromilia,
    ZMD,
    GF2,
    TOF,
    DNA,
    Star,
  };

  /**
   * @brief 游戏画像契约 (Game Profile Contract)
   * 
   * 规范了驱动层与各游戏专有逻辑之间的单向解耦交互接口:
   * 1. 匹配特征 (process_keywords)
   * 2. 着色器反虚化 / 着色器级轻量 Mod (process_spirv)
   * 3. 指令层轻量图元剔除 Mod (should_skip_draw_indexed)
   * 4. 专有 Addon / 跨层服务生命周期回调 (on_device_created / on_device_destroyed)
   */
  struct GameProfile {
    GameId id = GameId::Unknown;
    const char* name = "Unknown";

    // 匹配的进程名关键词列表 (以 nullptr 结尾)
    const char* const* process_keywords = nullptr;

    // 1. 着色器反虚化与着色器级别 Mod 处理函数
    void (*process_spirv)(uint32_t* code, size_t word_count) = nullptr;

    // 2. 命令级别绘制图元过滤 / 跳过 (如面具剔除、特效 Mesh 跳过)
    bool (*should_skip_draw_indexed)(uint32_t index_count, uint32_t first_index, int32_t vertex_offset) = nullptr;

    // 3. 关联的 Addon 生命周期钩子 (如创建 Device 时启动异步线程 / 内存补丁)
    void (*on_device_created)(VkDevice device) = nullptr;
    void (*on_device_destroyed)(VkDevice device) = nullptr;
  };

  // 全局高频访问快速函数指针 (零开销内联，非目标游戏下冷分支开销为 0)
  inline bool (*g_active_skip_draw_indexed)(uint32_t index_count, uint32_t first_index, int32_t vertex_offset) = nullptr;

  void init_game_profiles(std::string_view process_name);
  void reset_game_profiles_for_test();
  const GameProfile* get_active_profile();
  GameId get_active_game_id();
  bool is_game_active();

} // namespace game_core
