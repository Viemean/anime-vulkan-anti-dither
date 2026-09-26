#pragma once

#include "logger.h"
#include <vulkan/vulkan.h>
#include <unordered_set>
#include <unordered_map>
#include <mutex>
#include <shared_mutex>
#include <atomic>
#include <chrono>
#include <thread>
#include <cstring>

namespace game_probe {

  inline std::shared_mutex g_probe_mutex;
  inline std::unordered_set<VkShaderModule> g_character_shader_modules;
  inline std::unordered_set<VkPipeline> g_character_pipelines;
  inline std::unordered_map<VkCommandBuffer, bool> g_cmd_is_character;

  inline std::atomic<uint32_t> g_char_draw_calls{0};
  inline std::atomic<uint32_t> g_total_draw_calls{0};
  inline std::atomic<uint32_t> g_char_bind_calls{0};

  inline std::atomic<bool> g_probe_enabled{false};
  inline std::atomic<bool> g_has_seen_character{false};
  inline std::atomic<bool> g_was_drawing_character{false};

  inline void init_probe() {
    static std::once_flag s_init_flag;
    std::call_once(s_init_flag, []() {
      // 环境变量 ANTI_DITHER_PROBE=1 或激活日志时默认开启探针监控
      const char* env_probe = std::getenv("ANTI_DITHER_PROBE");
      if (env_probe && (std::strcmp(env_probe, "1") == 0 || std::strcmp(env_probe, "true") == 0)) {
        g_probe_enabled.store(true, std::memory_order_relaxed);
      } else if (game_logger::is_log_enabled()) {
        g_probe_enabled.store(true, std::memory_order_relaxed);
      }
    });
  }

  inline bool is_enabled() {
    init_probe();
    return g_probe_enabled.load(std::memory_order_relaxed);
  }

  inline void register_character_shader_module(VkShaderModule mod) {
    if (!mod) return;
    std::unique_lock<std::shared_mutex> lock(g_probe_mutex);
    g_character_shader_modules.insert(mod);
  }

  inline bool is_character_shader_module(VkShaderModule mod) {
    if (!mod) return false;
    std::shared_lock<std::shared_mutex> lock(g_probe_mutex);
    return g_character_shader_modules.count(mod) > 0;
  }

  inline void register_character_pipeline(VkPipeline pipeline) {
    if (!pipeline) return;
    std::unique_lock<std::shared_mutex> lock(g_probe_mutex);
    g_character_pipelines.insert(pipeline);
    game_logger::log_msg("[通用探针] 注册角色绘制管线: 0x%p (当前共 %zu 个)\n",
                         reinterpret_cast<void*>(pipeline), g_character_pipelines.size());
  }

  inline void unregister_character_pipeline(VkPipeline pipeline) {
    if (!pipeline) return;
    std::unique_lock<std::shared_mutex> lock(g_probe_mutex);
    g_character_pipelines.erase(pipeline);
  }

  inline void on_bind_pipeline(VkCommandBuffer cmd, VkPipelineBindPoint bind_point, VkPipeline pipeline) {
    if (!is_enabled() || bind_point != VK_PIPELINE_BIND_POINT_GRAPHICS) return;

    bool is_char = false;
    {
      std::shared_lock<std::shared_mutex> lock(g_probe_mutex);
      is_char = (g_character_pipelines.count(pipeline) > 0);
    }

    {
      std::unique_lock<std::shared_mutex> lock(g_probe_mutex);
      g_cmd_is_character[cmd] = is_char;
    }

    if (is_char) {
      g_char_bind_calls.fetch_add(1, std::memory_order_relaxed);
    }
  }

  inline void on_draw(VkCommandBuffer cmd) {
    if (!is_enabled()) return;
    g_total_draw_calls.fetch_add(1, std::memory_order_relaxed);

    bool is_char = false;
    {
      std::shared_lock<std::shared_mutex> lock(g_probe_mutex);
      auto it = g_cmd_is_character.find(cmd);
      if (it != g_cmd_is_character.end()) {
        is_char = it->second;
      }
    }

    if (is_char) {
      g_char_draw_calls.fetch_add(1, std::memory_order_relaxed);
    }
  }

  inline void on_cmd_reset_or_free(VkCommandBuffer cmd) {
    std::unique_lock<std::shared_mutex> lock(g_probe_mutex);
    g_cmd_is_character.erase(cmd);
  }

  inline void start_heartbeat_thread() {
    static std::once_flag s_heartbeat_flag;
    std::call_once(s_heartbeat_flag, []() {
      std::thread([]() {
        uint32_t loop_count = 0;
        while (true) {
          std::this_thread::sleep_for(std::chrono::seconds(1));
          if (!is_enabled()) continue;

          uint32_t char_draws = g_char_draw_calls.exchange(0, std::memory_order_relaxed);
          uint32_t total_draws = g_total_draw_calls.exchange(0, std::memory_order_relaxed);
          uint32_t char_binds = g_char_bind_calls.exchange(0, std::memory_order_relaxed);

          bool is_drawing = (char_draws > 0);
          if (is_drawing) {
            g_has_seen_character.store(true, std::memory_order_relaxed);
          }

          if (g_has_seen_character.load(std::memory_order_relaxed)) {
            bool was_drawing = g_was_drawing_character.load(std::memory_order_relaxed);
            if (was_drawing && !is_drawing) {
              game_logger::log_msg("[通用探针] 状态突变: 角色 Draw Call 归零 (总 DC: %u/s, 角色 DC: 0) -> 确认为 CPU 端逻辑隐藏/视锥体剔除\n",
                                   total_draws);
            } else if (!was_drawing && is_drawing) {
              game_logger::log_msg("[通用探针] 状态突变: 角色 Draw Call 恢复 (角色 DC: %u/s, 绑定: %u/s, 总 DC: %u/s)\n",
                                   char_draws, char_binds, total_draws);
            }
            g_was_drawing_character.store(is_drawing, std::memory_order_relaxed);
          }

          if (++loop_count % 2 == 0) {
            size_t pipe_count = 0;
            {
              std::shared_lock<std::shared_mutex> lock(g_probe_mutex);
              pipe_count = g_character_pipelines.size();
            }
            if (pipe_count > 0 || total_draws > 0) {
              game_logger::log_msg("[通用探针] 渲染心跳: 场景总 DC: %u/s | 角色 DC: %u/s | 角色管线数: %zu\n",
                                   total_draws, char_draws, pipe_count);
            }
          }
        }
      }).detach();
    });
  }

} // namespace game_probe
