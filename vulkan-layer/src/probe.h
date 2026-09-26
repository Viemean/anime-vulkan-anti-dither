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
#include <vector>

namespace game_probe {

  struct DrawCallInfo {
    uint32_t indexCount{0};
    uint32_t instanceCount{0};
    uint32_t firstIndex{0};
    int32_t vertexOffset{0};
    uint32_t firstInstance{0};
    uint32_t vs_hash{0};
    uint32_t fs_hash{0};
    bool is_indexed{true};
  };

  struct PipelineMeta {
    bool is_character{false};
    bool is_shadow{false};
    uint32_t vs_hash{0};
    uint32_t fs_hash{0};
  };

  struct CmdBufferState {
    VkPipeline current_pipeline{VK_NULL_HANDLE};
    PipelineMeta current_meta{};
  };

  inline std::shared_mutex g_probe_mutex;
  inline std::unordered_set<VkShaderModule> g_character_shader_modules;
  inline std::unordered_map<VkPipeline, PipelineMeta> g_pipeline_metadata;
  inline std::unordered_map<VkCommandBuffer, CmdBufferState> g_cmd_state;

  inline std::unordered_map<VkPipeline, DrawCallInfo> g_last_char_draws;
  inline std::unordered_map<VkPipeline, DrawCallInfo> g_last_shadow_draws;

  inline std::atomic<uint32_t> g_char_draw_calls{0};
  inline std::atomic<uint32_t> g_shadow_draw_calls{0};
  inline std::atomic<uint32_t> g_total_draw_calls{0};
  inline std::atomic<uint32_t> g_char_bind_calls{0};
  inline std::atomic<uint32_t> g_shadow_bind_calls{0};

  inline std::atomic<bool> g_probe_enabled{false};
  inline std::atomic<bool> g_has_seen_character{false};
  inline std::atomic<bool> g_was_drawing_character{false};

  inline void init_probe() {
    static std::once_flag s_init_flag;
    std::call_once(s_init_flag, []() {
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

  inline void register_pipeline(VkPipeline pipeline, bool is_character, bool is_shadow, uint32_t vs_hash, uint32_t fs_hash) {
    if (!pipeline) return;
    std::unique_lock<std::shared_mutex> lock(g_probe_mutex);
    g_pipeline_metadata[pipeline] = {is_character, is_shadow, vs_hash, fs_hash};
    if (is_character) {
      game_logger::log_msg("[通用探针] 注册角色绘制管线: 0x%p | VS: 0x%08x | FS: 0x%08x\n",
                           reinterpret_cast<void*>(pipeline), vs_hash, fs_hash);
    } else if (is_shadow) {
      game_logger::log_msg("[通用探针] 注册角色阴影管线: 0x%p | VS: 0x%08x | FS: 0x%08x\n",
                           reinterpret_cast<void*>(pipeline), vs_hash, fs_hash);
    }
  }

  inline void unregister_pipeline(VkPipeline pipeline) {
    if (!pipeline) return;
    std::unique_lock<std::shared_mutex> lock(g_probe_mutex);
    g_pipeline_metadata.erase(pipeline);
    g_last_char_draws.erase(pipeline);
    g_last_shadow_draws.erase(pipeline);
  }

  inline void register_character_pipeline(VkPipeline pipeline) {
    register_pipeline(pipeline, true, false, 0, 0);
  }

  inline void unregister_character_pipeline(VkPipeline pipeline) {
    unregister_pipeline(pipeline);
  }

  inline void on_bind_pipeline(VkCommandBuffer cmd, VkPipelineBindPoint bind_point, VkPipeline pipeline) {
    if (!is_enabled() || bind_point != VK_PIPELINE_BIND_POINT_GRAPHICS) return;

    PipelineMeta meta{};
    {
      std::shared_lock<std::shared_mutex> lock(g_probe_mutex);
      auto it = g_pipeline_metadata.find(pipeline);
      if (it != g_pipeline_metadata.end()) {
        meta = it->second;
      }
    }

    {
      std::unique_lock<std::shared_mutex> lock(g_probe_mutex);
      g_cmd_state[cmd] = {pipeline, meta};
    }

    if (meta.is_character) {
      g_char_bind_calls.fetch_add(1, std::memory_order_relaxed);
    } else if (meta.is_shadow) {
      g_shadow_bind_calls.fetch_add(1, std::memory_order_relaxed);
    }
  }

  inline void on_draw_indexed(VkCommandBuffer cmd, uint32_t indexCount, uint32_t instanceCount,
                              uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance) {
    if (!is_enabled()) return;
    g_total_draw_calls.fetch_add(1, std::memory_order_relaxed);

    CmdBufferState state{};
    {
      std::shared_lock<std::shared_mutex> lock(g_probe_mutex);
      auto it = g_cmd_state.find(cmd);
      if (it != g_cmd_state.end()) {
        state = it->second;
      }
    }

    if (state.current_meta.is_character) {
      g_char_draw_calls.fetch_add(1, std::memory_order_relaxed);
      std::unique_lock<std::shared_mutex> lock(g_probe_mutex);
      g_last_char_draws[state.current_pipeline] = {
        indexCount, instanceCount, firstIndex, vertexOffset, firstInstance,
        state.current_meta.vs_hash, state.current_meta.fs_hash, true
      };
    } else if (state.current_meta.is_shadow) {
      g_shadow_draw_calls.fetch_add(1, std::memory_order_relaxed);
      std::unique_lock<std::shared_mutex> lock(g_probe_mutex);
      g_last_shadow_draws[state.current_pipeline] = {
        indexCount, instanceCount, firstIndex, vertexOffset, firstInstance,
        state.current_meta.vs_hash, state.current_meta.fs_hash, true
      };
    }
  }

  inline void on_draw(VkCommandBuffer cmd, uint32_t vertexCount = 0, uint32_t instanceCount = 0,
                      uint32_t firstVertex = 0, uint32_t firstInstance = 0) {
    if (!is_enabled()) return;
    g_total_draw_calls.fetch_add(1, std::memory_order_relaxed);

    CmdBufferState state{};
    {
      std::shared_lock<std::shared_mutex> lock(g_probe_mutex);
      auto it = g_cmd_state.find(cmd);
      if (it != g_cmd_state.end()) {
        state = it->second;
      }
    }

    if (state.current_meta.is_character) {
      g_char_draw_calls.fetch_add(1, std::memory_order_relaxed);
      std::unique_lock<std::shared_mutex> lock(g_probe_mutex);
      g_last_char_draws[state.current_pipeline] = {
        vertexCount, instanceCount, firstVertex, 0, firstInstance,
        state.current_meta.vs_hash, state.current_meta.fs_hash, false
      };
    } else if (state.current_meta.is_shadow) {
      g_shadow_draw_calls.fetch_add(1, std::memory_order_relaxed);
      std::unique_lock<std::shared_mutex> lock(g_probe_mutex);
      g_last_shadow_draws[state.current_pipeline] = {
        vertexCount, instanceCount, firstVertex, 0, firstInstance,
        state.current_meta.vs_hash, state.current_meta.fs_hash, false
      };
    }
  }

  inline void on_cmd_reset_or_free(VkCommandBuffer cmd) {
    std::unique_lock<std::shared_mutex> lock(g_probe_mutex);
    g_cmd_state.erase(cmd);
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
          uint32_t shadow_draws = g_shadow_draw_calls.exchange(0, std::memory_order_relaxed);
          uint32_t total_draws = g_total_draw_calls.exchange(0, std::memory_order_relaxed);
          uint32_t char_binds = g_char_bind_calls.exchange(0, std::memory_order_relaxed);
          uint32_t shadow_binds = g_shadow_bind_calls.exchange(0, std::memory_order_relaxed);

          bool is_drawing_char = (char_draws > 0);
          if (is_drawing_char) {
            g_has_seen_character.store(true, std::memory_order_relaxed);
          }

          if (g_has_seen_character.load(std::memory_order_relaxed)) {
            bool was_drawing_char = g_was_drawing_character.load(std::memory_order_relaxed);
            if (was_drawing_char && !is_drawing_char) {
              std::unordered_map<VkPipeline, DrawCallInfo> char_snapshot;
              std::unordered_map<VkPipeline, DrawCallInfo> shadow_snapshot;
              {
                std::shared_lock<std::shared_mutex> lock(g_probe_mutex);
                char_snapshot = g_last_char_draws;
                shadow_snapshot = g_last_shadow_draws;
              }
              game_logger::log_msg("\n================ [通用探针-突变诊断报告] ================\n");
              game_logger::log_msg("[通用探针] 突变触发: 角色身体 Draw Call 归零 (总 DC: %u/s | 阴影 DC: %u/s | 身体 DC: 0)\n",
                                   total_draws, shadow_draws);
              game_logger::log_msg("[通用探针] 角色身体消失前最后一刻绘制特征 (共 %zu 个管线):\n", char_snapshot.size());
              for (const auto& [pipe, info] : char_snapshot) {
                game_logger::log_msg("  [身体] Pipe: 0x%p | VS: 0x%08x | FS: 0x%08x | 索引数: %u | 顶点偏移: %d | 实例: %u\n",
                                     reinterpret_cast<void*>(pipe), info.vs_hash, info.fs_hash, info.indexCount, info.vertexOffset, info.instanceCount);
              }
              game_logger::log_msg("[通用探针] 角色身体消失期间活跃绘制的阴影特征 (共 %zu 个管线):\n", shadow_snapshot.size());
              for (const auto& [pipe, info] : shadow_snapshot) {
                game_logger::log_msg("  [阴影] Pipe: 0x%p | VS: 0x%08x | FS: 0x%08x | 索引数: %u | 顶点偏移: %d | 实例: %u\n",
                                     reinterpret_cast<void*>(pipe), info.vs_hash, info.fs_hash, info.indexCount, info.vertexOffset, info.instanceCount);
              }

              uint32_t matched_meshes = 0;
              for (const auto& [c_pipe, c_info] : char_snapshot) {
                for (const auto& [s_pipe, s_info] : shadow_snapshot) {
                  if (c_info.indexCount > 0 && c_info.indexCount == s_info.indexCount && c_info.vertexOffset == s_info.vertexOffset) {
                    game_logger::log_msg("[通用探针] >>> 匹配到同源网格: 阴影正在绘制身体网格 (索引数: %u, 偏移: %d, 角色VS: 0x%08x, 阴影VS: 0x%08x) <<<\n",
                                         c_info.indexCount, c_info.vertexOffset, c_info.vs_hash, s_info.vs_hash);
                    matched_meshes++;
                    break;
                  }
                }
              }
              if (matched_meshes > 0) {
                game_logger::log_msg("[通用探针] >>> 核心定界结论: GPU 网格与骨骼动画完好，仅相机主视图剔除了角色身体 (Culling / Camera Layer Mask) <<<\n");
              } else {
                game_logger::log_msg("[通用探针] >>> 核心定界结论: 阴影亦未检测到同索引网格 (整体 GameObject SetActive(false) 或使用独立网格) <<<\n");
              }
              game_logger::log_msg("=========================================================\n\n");
            } else if (!was_drawing_char && is_drawing_char) {
              game_logger::log_msg("[通用探针] 状态恢复: 角色身体 Draw Call 恢复 (身体 DC: %u/s | 阴影 DC: %u/s | 角色绑定: %u/s | 阴影绑定: %u/s | 总 DC: %u/s)\n",
                                   char_draws, shadow_draws, char_binds, shadow_binds, total_draws);
            }
            g_was_drawing_character.store(is_drawing_char, std::memory_order_relaxed);
          }

          if (++loop_count % 2 == 0) {
            size_t char_pipes = 0;
            size_t shadow_pipes = 0;
            {
              std::shared_lock<std::shared_mutex> lock(g_probe_mutex);
              for (const auto& [p, m] : g_pipeline_metadata) {
                if (m.is_character) char_pipes++;
                if (m.is_shadow) shadow_pipes++;
              }
            }
            if (char_pipes > 0 || total_draws > 0) {
              game_logger::log_msg("[通用探针] 渲染心跳: 场景总 DC: %u/s | 角色身体 DC: %u/s | 阴影 DC: %u/s | 角色管线: %zu | 阴影管线: %zu\n",
                                   total_draws, char_draws, shadow_draws, char_pipes, shadow_pipes);
            }
          }
        }
      }).detach();
    });
  }

} // namespace game_probe
