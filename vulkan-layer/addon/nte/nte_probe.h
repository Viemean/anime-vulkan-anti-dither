#pragma once

#include "../../src/logger.h"

#include <vulkan/vulkan.h>
#include <unordered_set>
#include <unordered_map>
#include <mutex>
#include <atomic>
#include <chrono>

namespace nte_probe {

  inline std::mutex g_probe_mutex;
  inline std::unordered_set<VkPipeline> g_character_pipelines;
  inline std::unordered_set<VkShaderModule> g_character_shader_modules;
  inline std::unordered_map<VkCommandBuffer, bool> g_cmd_is_character;

  inline std::atomic<uint32_t> g_char_draw_calls{0};
  inline std::atomic<uint32_t> g_total_draw_calls{0};
  inline std::atomic<uint32_t> g_char_bind_calls{0};

  inline std::chrono::steady_clock::time_point g_last_report_time = std::chrono::steady_clock::now();
  inline bool g_was_drawing_character = false;
  inline bool g_has_seen_character = false;

  inline void register_character_shader_module(VkShaderModule mod) {
    if (!game_logger::is_nte() || !mod) return;
    std::lock_guard<std::mutex> lock(g_probe_mutex);
    g_character_shader_modules.insert(mod);
    game_logger::log_msg("[NTE-Probe] 记录角色着色器模块: 0x%p (当前共 %zu 个)\n",
                         reinterpret_cast<void*>(mod), g_character_shader_modules.size());
  }

  inline bool is_character_shader_module(VkShaderModule mod) {
    if (!mod) return false;
    std::lock_guard<std::mutex> lock(g_probe_mutex);
    return (g_character_shader_modules.count(mod) > 0);
  }

  inline void register_character_pipeline(VkPipeline pipeline) {
    if (!game_logger::is_nte() || !pipeline) return;
    std::lock_guard<std::mutex> lock(g_probe_mutex);
    g_character_pipelines.insert(pipeline);
    game_logger::log_msg("[NTE-Probe] 注册角色绘制管线: 0x%p (当前共 %zu 个)\n",
                         reinterpret_cast<void*>(pipeline), g_character_pipelines.size());
  }

  inline void unregister_character_pipeline(VkPipeline pipeline) {
    if (!game_logger::is_nte()) return;
    std::lock_guard<std::mutex> lock(g_probe_mutex);
    g_character_pipelines.erase(pipeline);
  }

  inline void on_bind_pipeline(VkCommandBuffer cmd, VkPipelineBindPoint bind_point, VkPipeline pipeline) {
    if (!game_logger::is_nte()) return;
    if (bind_point != VK_PIPELINE_BIND_POINT_GRAPHICS) return;

    bool is_char = false;
    {
      std::lock_guard<std::mutex> lock(g_probe_mutex);
      is_char = (g_character_pipelines.count(pipeline) > 0);
      g_cmd_is_character[cmd] = is_char;
    }

    if (is_char) {
      g_char_bind_calls.fetch_add(1, std::memory_order_relaxed);
    }
  }

  inline void on_draw(VkCommandBuffer cmd) {
    if (!game_logger::is_nte()) return;
    g_total_draw_calls.fetch_add(1, std::memory_order_relaxed);

    bool is_char = false;
    {
      std::lock_guard<std::mutex> lock(g_probe_mutex);
      auto it = g_cmd_is_character.find(cmd);
      if (it != g_cmd_is_character.end()) {
        is_char = it->second;
      }
    }

    if (is_char) {
      g_char_draw_calls.fetch_add(1, std::memory_order_relaxed);
    }
  }

  inline void check_periodic_report() {
    if (!game_logger::is_nte()) return;

    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - g_last_report_time).count();
    if (elapsed < 1000) {
      return;
    }

    std::lock_guard<std::mutex> lock(g_probe_mutex);
    now = std::chrono::steady_clock::now();
    elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - g_last_report_time).count();
    if (elapsed < 1000) return;
    g_last_report_time = now;

    uint32_t char_draws = g_char_draw_calls.exchange(0);
    uint32_t total_draws = g_total_draw_calls.exchange(0);
    uint32_t char_binds = g_char_bind_calls.exchange(0);

    bool is_drawing = (char_draws > 0);
    if (is_drawing) {
      g_has_seen_character = true;
    }

    if (g_has_seen_character) {
      if (g_was_drawing_character && !is_drawing) {
        game_logger::log_msg("[NTE-Probe] 状态突变: 角色 Draw Call 归零 (总 DC: %u/s, 角色 DC: 0) -> 确认为 CPU 端 SetVisibility 隐藏或视锥体剔除\n",
                             total_draws);
      } else if (!g_was_drawing_character && is_drawing) {
        game_logger::log_msg("[NTE-Probe] 状态突变: 角色 Draw Call 恢复 (角色 DC: %u/s, 绑定: %u/s, 总 DC: %u/s)\n",
                             char_draws, char_binds, total_draws);
      }
    }

    static uint32_t counter = 0;
    if (++counter % 2 == 0) {
      game_logger::log_msg("[NTE-Probe] 渲染心跳: 场景总 DC: %u/s | 角色 DC: %u/s | 角色管线数: %zu\n",
                           total_draws, char_draws, g_character_pipelines.size());
    }

    g_was_drawing_character = is_drawing;
  }

} // namespace nte_probe
