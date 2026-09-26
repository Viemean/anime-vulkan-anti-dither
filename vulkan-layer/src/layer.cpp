#include <vulkan/vulkan.h>
#include <vulkan/vk_layer.h>

#include "wuwa/wuwa_anti_dither.h"
#include "azur_promilia/azur_promilia_anti_dither.h"
#include "nte/nte_anti_dither.h"
#include "hsr/hsr_anti_dither.h"
#include "probe.h"
#include "../addon/nte/memory_patcher.h"

#include <mutex>
#include <shared_mutex>
#include <unordered_map>
#include <vector>
#include <cstring>
#include <thread>
#include <chrono>

#define LAYER_NAME "VK_LAYER_WUWA_antidither"
#define LAYER_DESC "Wuthering Waves Camera Dither Discard Neutralizer Layer"

namespace {

  // Helper to extract dispatch key from dispatchable handles
  inline void* get_dispatch_key(const void* object) {
    return *(void**)object;
  }

  inline void dispatch_anti_dither_process(uint32_t* code, size_t word_count) {
    if (game_logger::is_hsr()) {
      hsr_layer::process_spirv_anti_dither(code, word_count);
    } else if (game_logger::is_nte()) {
      nte_layer::process_spirv_anti_dither(code, word_count);
    } else if (game_logger::is_azur_promilia()) {
      azur_promilia_layer::process_spirv_anti_dither(code, word_count);
    } else {
      wuwa_layer::process_spirv_anti_dither(code, word_count);
    }
  }

  struct InstanceData {
    PFN_vkGetInstanceProcAddr get_instance_proc_addr = nullptr;
    PFN_vkDestroyInstance destroy_instance = nullptr;
    PFN_vkCreateDevice create_device = nullptr;
    PFN_vkEnumeratePhysicalDevices enumerate_physical_devices = nullptr;
    PFN_vkEnumeratePhysicalDeviceGroups enumerate_physical_device_groups = nullptr;
  };

  struct DeviceData {
    PFN_vkGetDeviceProcAddr get_device_proc_addr = nullptr;
    PFN_vkDestroyDevice destroy_device = nullptr;
    PFN_vkCreateShaderModule create_shader_module = nullptr;
    PFN_vkCreateGraphicsPipelines create_graphics_pipelines = nullptr;
    PFN_vkCreateComputePipelines create_compute_pipelines = nullptr;
    PFN_vkAllocateCommandBuffers allocate_command_buffers = nullptr;
    PFN_vkFreeCommandBuffers free_command_buffers = nullptr;
    PFN_vkDestroyPipeline destroy_pipeline = nullptr;
    PFN_vkCmdBindPipeline cmd_bind_pipeline = nullptr;
    PFN_vkCmdDraw cmd_draw = nullptr;
    PFN_vkCmdDrawIndexed cmd_draw_indexed = nullptr;
    PFN_vkCmdDrawIndirect cmd_draw_indirect = nullptr;
    PFN_vkCmdDrawIndexedIndirect cmd_draw_indexed_indirect = nullptr;
  };

  std::mutex g_lock;
  std::unordered_map<void*, InstanceData> g_instance_dispatch;
  std::unordered_map<void*, DeviceData> g_device_dispatch;
  std::unordered_map<VkPhysicalDevice, void*> g_phys_device_to_instance;

  std::shared_mutex g_cmd_device_lock;
  std::unordered_map<VkCommandBuffer, VkDevice> g_cmd_to_device;
  std::atomic<VkDevice> g_primary_device{VK_NULL_HANDLE};
  DeviceData g_primary_dev_data;

  inline DeviceData get_dev_data_for_cmd(VkCommandBuffer cmd) {
    VkDevice primary = g_primary_device.load(std::memory_order_relaxed);
    if (primary != VK_NULL_HANDLE) {
      return g_primary_dev_data;
    }
    std::shared_lock<std::shared_mutex> lock(g_cmd_device_lock);
    auto it = g_cmd_to_device.find(cmd);
    if (it != g_cmd_to_device.end()) {
      std::lock_guard<std::mutex> dev_lock(g_lock);
      auto dev_it = g_device_dispatch.find(get_dispatch_key(it->second));
      if (dev_it != g_device_dispatch.end()) return dev_it->second;
    }
    return {};
  }

  VkLayerInstanceCreateInfo* get_instance_chain_info(const VkInstanceCreateInfo* pCreateInfo, VkLayerFunction func) {
    auto* chain_info = static_cast<const VkLayerInstanceCreateInfo*>(pCreateInfo->pNext);
    while (chain_info) {
      if (chain_info->sType == VK_STRUCTURE_TYPE_LOADER_INSTANCE_CREATE_INFO && chain_info->function == func)
        return const_cast<VkLayerInstanceCreateInfo*>(chain_info);
      chain_info = static_cast<const VkLayerInstanceCreateInfo*>(chain_info->pNext);
    }
    return nullptr;
  }

  VkLayerDeviceCreateInfo* get_device_chain_info(const VkDeviceCreateInfo* pCreateInfo, VkLayerFunction func) {
    auto* chain_info = static_cast<const VkLayerDeviceCreateInfo*>(pCreateInfo->pNext);
    while (chain_info) {
      if (chain_info->sType == VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO && chain_info->function == func)
        return const_cast<VkLayerDeviceCreateInfo*>(chain_info);
      chain_info = static_cast<const VkLayerDeviceCreateInfo*>(chain_info->pNext);
    }
    return nullptr;
  }

  static std::mutex g_module_spirv_lock;
  static std::unordered_map<VkShaderModule, std::vector<uint32_t>> g_module_spirv_cache;
  static std::unordered_map<VkShaderModule, uint32_t> g_module_hash_cache;

} // namespace

// Intercepted Device Functions
static VKAPI_ATTR VkResult VKAPI_CALL wuwa_vkCreateShaderModule(
    VkDevice                                    device,
    const VkShaderModuleCreateInfo*             pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkShaderModule*                             pShaderModule) {

  DeviceData dev_data;
  {
    std::lock_guard<std::mutex> lock(g_lock);
    auto it = g_device_dispatch.find(get_dispatch_key(device));
    if (it != g_device_dispatch.end())
      dev_data = it->second;
  }

  if (!dev_data.create_shader_module)
    return VK_ERROR_INITIALIZATION_FAILED;

  if (pCreateInfo && pCreateInfo->pCode && pCreateInfo->codeSize >= sizeof(uint32_t) * 5 && wuwa_layer::is_layer_active()) {
    size_t word_count = pCreateInfo->codeSize / sizeof(uint32_t);
    std::vector<uint32_t> patched_code(pCreateInfo->pCode, pCreateInfo->pCode + word_count);

    uint32_t orig_hash = game_logger::compute_spirv_hash(patched_code.data(), patched_code.size());
    dispatch_anti_dither_process(patched_code.data(), patched_code.size());
    uint32_t new_hash = game_logger::compute_spirv_hash(patched_code.data(), patched_code.size());

    VkShaderModuleCreateInfo modified_info = *pCreateInfo;
    modified_info.pCode = patched_code.data();

    VkResult res = dev_data.create_shader_module(device, &modified_info, pAllocator, pShaderModule);
    if (res == VK_SUCCESS && pShaderModule && *pShaderModule) {
      {
        std::lock_guard<std::mutex> lk(g_module_spirv_lock);
        g_module_spirv_cache[*pShaderModule] = patched_code;
        g_module_hash_cache[*pShaderModule] = orig_hash;
      }
      if (orig_hash != new_hash) {
        game_probe::register_character_shader_module(*pShaderModule);
      }
    }
    return res;
  }

  return dev_data.create_shader_module(device, pCreateInfo, pAllocator, pShaderModule);
}

static VKAPI_ATTR VkResult VKAPI_CALL wuwa_vkCreateGraphicsPipelines(
    VkDevice                                    device,
    VkPipelineCache                             pipelineCache,
    uint32_t                                    createInfoCount,
    const VkGraphicsPipelineCreateInfo*         pCreateInfos,
    const VkAllocationCallbacks*                pAllocator,
    VkPipeline*                                 pPipelines) {

  DeviceData dev_data;
  {
    std::lock_guard<std::mutex> lock(g_lock);
    auto it = g_device_dispatch.find(get_dispatch_key(device));
    if (it != g_device_dispatch.end())
      dev_data = it->second;
  }

  if (!dev_data.create_graphics_pipelines)
    return VK_ERROR_INITIALIZATION_FAILED;

  if (!wuwa_layer::is_layer_active() || !pCreateInfos || createInfoCount == 0)
    return dev_data.create_graphics_pipelines(device, pipelineCache, createInfoCount, pCreateInfos, pAllocator, pPipelines);

  std::vector<VkGraphicsPipelineCreateInfo> modified_infos(pCreateInfos, pCreateInfos + createInfoCount);
  std::vector<std::vector<VkPipelineShaderStageCreateInfo>> modified_stages(createInfoCount);
  std::vector<std::vector<VkShaderModuleCreateInfo>> modified_module_infos(createInfoCount);
  std::vector<std::vector<std::vector<uint32_t>>> patched_codes(createInfoCount);
  std::vector<bool> pipeline_is_character(createInfoCount, false);
  std::vector<bool> pipeline_is_shadow(createInfoCount, false);
  std::vector<uint32_t> pipeline_vs_hash(createInfoCount, 0);
  std::vector<uint32_t> pipeline_fs_hash(createInfoCount, 0);

  for (uint32_t i = 0; i < createInfoCount; ++i) {
    const auto& orig_info = pCreateInfos[i];
    if (!orig_info.pStages || orig_info.stageCount == 0)
      continue;

    modified_stages[i].assign(orig_info.pStages, orig_info.pStages + orig_info.stageCount);
    modified_module_infos[i].resize(orig_info.stageCount);
    patched_codes[i].resize(orig_info.stageCount);

    uint32_t char_fs_hash = 0;
    bool is_shadow_pipe = false;

    for (uint32_t s = 0; s < orig_info.stageCount; ++s) {
      auto& stage = modified_stages[i][s];

      if (stage.module && game_probe::is_character_shader_module(stage.module)) {
        pipeline_is_character[i] = true;
        std::lock_guard<std::mutex> lk(g_module_spirv_lock);
        auto it = g_module_hash_cache.find(stage.module);
        if (it != g_module_hash_cache.end()) char_fs_hash = it->second;
      }
      if (stage.module) {
        std::lock_guard<std::mutex> lk(g_module_spirv_lock);
        auto it = g_module_hash_cache.find(stage.module);
        if (it != g_module_hash_cache.end() && it->second == 0x492b730c) {
          is_shadow_pipe = true;
          pipeline_is_shadow[i] = true;
        }
      }

      const auto* header = static_cast<const VkBaseInStructure*>(stage.pNext);
      const VkShaderModuleCreateInfo* mod_info = nullptr;
      while (header) {
        if (header->sType == VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO) {
          mod_info = reinterpret_cast<const VkShaderModuleCreateInfo*>(header);
          break;
        }
        header = header->pNext;
      }

      if (mod_info && mod_info->pCode && mod_info->codeSize >= 20) {
        size_t word_count = mod_info->codeSize / sizeof(uint32_t);
        patched_codes[i][s].assign(mod_info->pCode, mod_info->pCode + word_count);

        uint32_t orig_hash = game_logger::compute_spirv_hash(patched_codes[i][s].data(), patched_codes[i][s].size());
        if (orig_hash == 0x492b730c) {
          is_shadow_pipe = true;
          pipeline_is_shadow[i] = true;
        }
        if (stage.stage == VK_SHADER_STAGE_FRAGMENT_BIT ||
            ((game_logger::is_nte() || game_logger::is_hsr()) && stage.stage == VK_SHADER_STAGE_VERTEX_BIT)) {
          dispatch_anti_dither_process(patched_codes[i][s].data(), patched_codes[i][s].size());
        }
        uint32_t new_hash = game_logger::compute_spirv_hash(patched_codes[i][s].data(), patched_codes[i][s].size());
        if (orig_hash != new_hash) {
          pipeline_is_character[i] = true;
          char_fs_hash = orig_hash;
        }

        modified_module_infos[i][s] = *mod_info;
        modified_module_infos[i][s].pCode = patched_codes[i][s].data();
        stage.pNext = &modified_module_infos[i][s];
      }
    }

    if (char_fs_hash != 0) {
      pipeline_fs_hash[i] = char_fs_hash;
    }

    if (pipeline_is_character[i] || is_shadow_pipe) {
      for (uint32_t s = 0; s < orig_info.stageCount; ++s) {
        auto& stage = modified_stages[i][s];
        if (stage.stage == VK_SHADER_STAGE_VERTEX_BIT) {
          const uint32_t* vs_ptr = nullptr;
          size_t vs_words = 0;
          uint32_t vs_hash = 0;

          if (!patched_codes[i][s].empty()) {
            vs_ptr = patched_codes[i][s].data();
            vs_words = patched_codes[i][s].size();
            vs_hash = game_logger::compute_spirv_hash(vs_ptr, vs_words);
          } else if (stage.module) {
            std::lock_guard<std::mutex> lk(g_module_spirv_lock);
            auto it = g_module_spirv_cache.find(stage.module);
            if (it != g_module_spirv_cache.end()) {
              vs_ptr = it->second.data();
              vs_words = it->second.size();
              vs_hash = g_module_hash_cache[stage.module];
            }
          }

          if (vs_ptr && vs_words > 0) {
            pipeline_vs_hash[i] = vs_hash;
            uint32_t paired_fs = is_shadow_pipe ? 0x492b730c : char_fs_hash;
            game_logger::dump_character_vertex_shader(vs_ptr, vs_words, vs_hash, paired_fs);
            game_logger::log_msg("[反虚化驱动层-StarRail] %s管线创建: 绑定 VS: 0x%08x | 对应 FS: 0x%08x | 顶点字长: %zu\n",
                                 is_shadow_pipe ? "阴影" : "角色", vs_hash, paired_fs, vs_words);
          }
        }
      }
    }

    modified_infos[i].pStages = modified_stages[i].data();
  }

  VkResult res = dev_data.create_graphics_pipelines(device, pipelineCache, createInfoCount, modified_infos.data(), pAllocator, pPipelines);
  if (res == VK_SUCCESS && pPipelines) {
    for (uint32_t i = 0; i < createInfoCount; ++i) {
      if (pPipelines[i]) {
        if (pipeline_is_character[i]) {
          game_probe::register_pipeline(pPipelines[i], true, false, pipeline_vs_hash[i], pipeline_fs_hash[i]);
        } else if (pipeline_is_shadow[i]) {
          game_probe::register_pipeline(pPipelines[i], false, true, pipeline_vs_hash[i], 0x492b730c);
        }
      }
    }
  }
  return res;
}

static VKAPI_ATTR VkResult VKAPI_CALL wuwa_vkCreateComputePipelines(
    VkDevice                                    device,
    VkPipelineCache                             pipelineCache,
    uint32_t                                    createInfoCount,
    const VkComputePipelineCreateInfo*          pCreateInfos,
    const VkAllocationCallbacks*                pAllocator,
    VkPipeline*                                 pPipelines) {

  DeviceData dev_data;
  {
    std::lock_guard<std::mutex> lock(g_lock);
    auto it = g_device_dispatch.find(get_dispatch_key(device));
    if (it != g_device_dispatch.end())
      dev_data = it->second;
  }

  if (!dev_data.create_compute_pipelines)
    return VK_ERROR_INITIALIZATION_FAILED;

  if (!wuwa_layer::is_layer_active() || !pCreateInfos || createInfoCount == 0)
    return dev_data.create_compute_pipelines(device, pipelineCache, createInfoCount, pCreateInfos, pAllocator, pPipelines);

  std::vector<VkComputePipelineCreateInfo> modified_infos(pCreateInfos, pCreateInfos + createInfoCount);
  std::vector<VkShaderModuleCreateInfo> modified_module_infos(createInfoCount);
  std::vector<std::vector<uint32_t>> patched_codes(createInfoCount);

  for (uint32_t i = 0; i < createInfoCount; ++i) {
    auto& stage = modified_infos[i].stage;
    const auto* header = static_cast<const VkBaseInStructure*>(stage.pNext);
    const VkShaderModuleCreateInfo* mod_info = nullptr;
    while (header) {
      if (header->sType == VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO) {
        mod_info = reinterpret_cast<const VkShaderModuleCreateInfo*>(header);
        break;
      }
      header = header->pNext;
    }

    if (mod_info && mod_info->pCode && mod_info->codeSize >= 20) {
      size_t word_count = mod_info->codeSize / sizeof(uint32_t);
      patched_codes[i].assign(mod_info->pCode, mod_info->pCode + word_count);

      modified_module_infos[i] = *mod_info;
      modified_module_infos[i].pCode = patched_codes[i].data();
      stage.pNext = &modified_module_infos[i];
    }
  }

  return dev_data.create_compute_pipelines(device, pipelineCache, createInfoCount, modified_infos.data(), pAllocator, pPipelines);
}

static VKAPI_ATTR VkResult VKAPI_CALL wuwa_vkAllocateCommandBuffers(
    VkDevice                                    device,
    const VkCommandBufferAllocateInfo*          pAllocateInfo,
    VkCommandBuffer*                            pCommandBuffers) {
  DeviceData dev_data;
  {
    std::lock_guard<std::mutex> lock(g_lock);
    auto it = g_device_dispatch.find(get_dispatch_key(device));
    if (it != g_device_dispatch.end()) dev_data = it->second;
  }
  if (!dev_data.allocate_command_buffers) return VK_ERROR_INITIALIZATION_FAILED;

  VkResult res = dev_data.allocate_command_buffers(device, pAllocateInfo, pCommandBuffers);
  if (res == VK_SUCCESS && pAllocateInfo && pCommandBuffers) {
    std::unique_lock<std::shared_mutex> lock(g_cmd_device_lock);
    for (uint32_t i = 0; i < pAllocateInfo->commandBufferCount; ++i) {
      g_cmd_to_device[pCommandBuffers[i]] = device;
    }
  }
  return res;
}

static VKAPI_ATTR void VKAPI_CALL wuwa_vkFreeCommandBuffers(
    VkDevice                                    device,
    VkCommandPool                               commandPool,
    uint32_t                                    commandBufferCount,
    const VkCommandBuffer*                      pCommandBuffers) {
  DeviceData dev_data;
  {
    std::lock_guard<std::mutex> lock(g_lock);
    auto it = g_device_dispatch.find(get_dispatch_key(device));
    if (it != g_device_dispatch.end()) dev_data = it->second;
  }
  if (pCommandBuffers) {
    std::unique_lock<std::shared_mutex> lock(g_cmd_device_lock);
    for (uint32_t i = 0; i < commandBufferCount; ++i) {
      g_cmd_to_device.erase(pCommandBuffers[i]);
      game_probe::on_cmd_reset_or_free(pCommandBuffers[i]);
    }
  }
  if (dev_data.free_command_buffers) {
    dev_data.free_command_buffers(device, commandPool, commandBufferCount, pCommandBuffers);
  }
}

static VKAPI_ATTR void VKAPI_CALL wuwa_vkDestroyPipeline(
    VkDevice                                    device,
    VkPipeline                                  pipeline,
    const VkAllocationCallbacks*                pAllocator) {
  DeviceData dev_data;
  {
    std::lock_guard<std::mutex> lock(g_lock);
    auto it = g_device_dispatch.find(get_dispatch_key(device));
    if (it != g_device_dispatch.end()) dev_data = it->second;
  }
  game_probe::unregister_pipeline(pipeline);
  if (dev_data.destroy_pipeline) {
    dev_data.destroy_pipeline(device, pipeline, pAllocator);
  }
}

static VKAPI_ATTR void VKAPI_CALL wuwa_vkCmdBindPipeline(
    VkCommandBuffer                             commandBuffer,
    VkPipelineBindPoint                         pipelineBindPoint,
    VkPipeline                                  pipeline) {
  DeviceData dev_data = get_dev_data_for_cmd(commandBuffer);
  game_probe::on_bind_pipeline(commandBuffer, pipelineBindPoint, pipeline);
  if (dev_data.cmd_bind_pipeline) {
    dev_data.cmd_bind_pipeline(commandBuffer, pipelineBindPoint, pipeline);
  }
}

static VKAPI_ATTR void VKAPI_CALL wuwa_vkCmdDraw(
    VkCommandBuffer                             commandBuffer,
    uint32_t                                    vertexCount,
    uint32_t                                    instanceCount,
    uint32_t                                    firstVertex,
    uint32_t                                    firstInstance) {
  DeviceData dev_data = get_dev_data_for_cmd(commandBuffer);
  game_probe::on_draw(commandBuffer, vertexCount, instanceCount, firstVertex, firstInstance);
  if (dev_data.cmd_draw) {
    dev_data.cmd_draw(commandBuffer, vertexCount, instanceCount, firstVertex, firstInstance);
  }
}

static VKAPI_ATTR void VKAPI_CALL wuwa_vkCmdDrawIndexed(
    VkCommandBuffer                             commandBuffer,
    uint32_t                                    indexCount,
    uint32_t                                    instanceCount,
    uint32_t                                    firstIndex,
    int32_t                                     vertexOffset,
    uint32_t                                    firstInstance) {
  DeviceData dev_data = get_dev_data_for_cmd(commandBuffer);
  game_probe::on_draw_indexed(commandBuffer, indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
  if (dev_data.cmd_draw_indexed) {
    dev_data.cmd_draw_indexed(commandBuffer, indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
  }
}

static VKAPI_ATTR void VKAPI_CALL wuwa_vkCmdDrawIndirect(
    VkCommandBuffer                             commandBuffer,
    VkBuffer                                    buffer,
    VkDeviceSize                                offset,
    uint32_t                                    drawCount,
    uint32_t                                    stride) {
  DeviceData dev_data = get_dev_data_for_cmd(commandBuffer);
  game_probe::on_draw(commandBuffer);
  if (dev_data.cmd_draw_indirect) {
    dev_data.cmd_draw_indirect(commandBuffer, buffer, offset, drawCount, stride);
  }
}

static VKAPI_ATTR void VKAPI_CALL wuwa_vkCmdDrawIndexedIndirect(
    VkCommandBuffer                             commandBuffer,
    VkBuffer                                    buffer,
    VkDeviceSize                                offset,
    uint32_t                                    drawCount,
    uint32_t                                    stride) {
  DeviceData dev_data = get_dev_data_for_cmd(commandBuffer);
  game_probe::on_draw(commandBuffer);
  if (dev_data.cmd_draw_indexed_indirect) {
    dev_data.cmd_draw_indexed_indirect(commandBuffer, buffer, offset, drawCount, stride);
  }
}

static VKAPI_ATTR void VKAPI_CALL wuwa_vkDestroyDevice(
    VkDevice                                    device,
    const VkAllocationCallbacks*                pAllocator) {

  DeviceData dev_data;
  void* key = get_dispatch_key(device);
  {
    std::lock_guard<std::mutex> lock(g_lock);
    auto it = g_device_dispatch.find(key);
    if (it != g_device_dispatch.end()) {
      dev_data = it->second;
      g_device_dispatch.erase(it);
    }
  }

  if (dev_data.destroy_device)
    dev_data.destroy_device(device, pAllocator);
}

// Intercepted Instance Functions
static VKAPI_ATTR VkResult VKAPI_CALL wuwa_vkCreateDevice(
    VkPhysicalDevice                            physicalDevice,
    const VkDeviceCreateInfo*                   pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkDevice*                                   pDevice) {

  VkLayerDeviceCreateInfo* chain_info = get_device_chain_info(pCreateInfo, VK_LAYER_LINK_INFO);
  if (!chain_info || !chain_info->u.pLayerInfo)
    return VK_ERROR_INITIALIZATION_FAILED;

  PFN_vkGetInstanceProcAddr fpGetInstanceProcAddr = chain_info->u.pLayerInfo->pfnNextGetInstanceProcAddr;
  PFN_vkGetDeviceProcAddr fpGetDeviceProcAddr = chain_info->u.pLayerInfo->pfnNextGetDeviceProcAddr;

  // Advance the link info for the next layer in the chain
  chain_info->u.pLayerInfo = chain_info->u.pLayerInfo->pNext;

  PFN_vkCreateDevice fpCreateDevice = (PFN_vkCreateDevice)fpGetInstanceProcAddr(VK_NULL_HANDLE, "vkCreateDevice");
  if (!fpCreateDevice) {
    std::lock_guard<std::mutex> lock(g_lock);
    auto it = g_phys_device_to_instance.find(physicalDevice);
    if (it != g_phys_device_to_instance.end()) {
      auto inst_it = g_instance_dispatch.find(it->second);
      if (inst_it != g_instance_dispatch.end() && inst_it->second.create_device)
        fpCreateDevice = inst_it->second.create_device;
    }
  }

  if (!fpCreateDevice)
    return VK_ERROR_INITIALIZATION_FAILED;

  VkResult result = fpCreateDevice(physicalDevice, pCreateInfo, pAllocator, pDevice);
  if (result != VK_SUCCESS || !pDevice || !*pDevice)
    return result;

  DeviceData dev_data;
  dev_data.get_device_proc_addr = fpGetDeviceProcAddr;
  dev_data.destroy_device = (PFN_vkDestroyDevice)fpGetDeviceProcAddr(*pDevice, "vkDestroyDevice");
  dev_data.create_shader_module = (PFN_vkCreateShaderModule)fpGetDeviceProcAddr(*pDevice, "vkCreateShaderModule");
  dev_data.create_graphics_pipelines = (PFN_vkCreateGraphicsPipelines)fpGetDeviceProcAddr(*pDevice, "vkCreateGraphicsPipelines");
  dev_data.create_compute_pipelines = (PFN_vkCreateComputePipelines)fpGetDeviceProcAddr(*pDevice, "vkCreateComputePipelines");
  dev_data.allocate_command_buffers = (PFN_vkAllocateCommandBuffers)fpGetDeviceProcAddr(*pDevice, "vkAllocateCommandBuffers");
  dev_data.free_command_buffers = (PFN_vkFreeCommandBuffers)fpGetDeviceProcAddr(*pDevice, "vkFreeCommandBuffers");
  dev_data.destroy_pipeline = (PFN_vkDestroyPipeline)fpGetDeviceProcAddr(*pDevice, "vkDestroyPipeline");
  dev_data.cmd_bind_pipeline = (PFN_vkCmdBindPipeline)fpGetDeviceProcAddr(*pDevice, "vkCmdBindPipeline");
  dev_data.cmd_draw = (PFN_vkCmdDraw)fpGetDeviceProcAddr(*pDevice, "vkCmdDraw");
  dev_data.cmd_draw_indexed = (PFN_vkCmdDrawIndexed)fpGetDeviceProcAddr(*pDevice, "vkCmdDrawIndexed");
  dev_data.cmd_draw_indirect = (PFN_vkCmdDrawIndirect)fpGetDeviceProcAddr(*pDevice, "vkCmdDrawIndirect");
  dev_data.cmd_draw_indexed_indirect = (PFN_vkCmdDrawIndexedIndirect)fpGetDeviceProcAddr(*pDevice, "vkCmdDrawIndexedIndirect");

  g_primary_device.store(*pDevice, std::memory_order_relaxed);
  g_primary_dev_data = dev_data;

  {
    std::lock_guard<std::mutex> lock(g_lock);
    g_device_dispatch[get_dispatch_key(*pDevice)] = dev_data;
  }

  game_probe::start_heartbeat_thread();

  return VK_SUCCESS;
}

static VKAPI_ATTR void VKAPI_CALL wuwa_vkDestroyInstance(
    VkInstance                                  instance,
    const VkAllocationCallbacks*                pAllocator) {

  InstanceData inst_data;
  void* key = get_dispatch_key(instance);
  {
    std::lock_guard<std::mutex> lock(g_lock);
    auto it = g_instance_dispatch.find(key);
    if (it != g_instance_dispatch.end()) {
      inst_data = it->second;
      g_instance_dispatch.erase(it);
    }
    for (auto p_it = g_phys_device_to_instance.begin(); p_it != g_phys_device_to_instance.end();) {
      if (p_it->second == key) {
        p_it = g_phys_device_to_instance.erase(p_it);
      } else {
        ++p_it;
      }
    }
  }

  if (inst_data.destroy_instance)
    inst_data.destroy_instance(instance, pAllocator);
}

static VKAPI_ATTR VkResult VKAPI_CALL wuwa_vkEnumeratePhysicalDevices(
    VkInstance                                  instance,
    uint32_t*                                   pPhysicalDeviceCount,
    VkPhysicalDevice*                           pPhysicalDevices) {

  InstanceData inst_data;
  {
    std::lock_guard<std::mutex> lock(g_lock);
    auto it = g_instance_dispatch.find(get_dispatch_key(instance));
    if (it != g_instance_dispatch.end())
      inst_data = it->second;
  }

  if (!inst_data.enumerate_physical_devices)
    return VK_ERROR_INITIALIZATION_FAILED;

  VkResult res = inst_data.enumerate_physical_devices(instance, pPhysicalDeviceCount, pPhysicalDevices);
  if (res == VK_SUCCESS && pPhysicalDevices && pPhysicalDeviceCount && *pPhysicalDeviceCount > 0) {
    std::lock_guard<std::mutex> lock(g_lock);
    void* inst_key = get_dispatch_key(instance);
    for (uint32_t i = 0; i < *pPhysicalDeviceCount; ++i) {
      if (pPhysicalDevices[i])
        g_phys_device_to_instance[pPhysicalDevices[i]] = inst_key;
    }
  }

  return res;
}

static VKAPI_ATTR VkResult VKAPI_CALL wuwa_vkEnumeratePhysicalDeviceGroups(
    VkInstance                                  instance,
    uint32_t*                                   pPhysicalDeviceGroupCount,
    VkPhysicalDeviceGroupProperties*            pPhysicalDeviceGroupProperties) {

  InstanceData inst_data;
  {
    std::lock_guard<std::mutex> lock(g_lock);
    auto it = g_instance_dispatch.find(get_dispatch_key(instance));
    if (it != g_instance_dispatch.end())
      inst_data = it->second;
  }

  if (!inst_data.enumerate_physical_device_groups)
    return VK_ERROR_INITIALIZATION_FAILED;

  VkResult res = inst_data.enumerate_physical_device_groups(instance, pPhysicalDeviceGroupCount, pPhysicalDeviceGroupProperties);
  if (res == VK_SUCCESS && pPhysicalDeviceGroupProperties && pPhysicalDeviceGroupCount && *pPhysicalDeviceGroupCount > 0) {
    std::lock_guard<std::mutex> lock(g_lock);
    void* inst_key = get_dispatch_key(instance);
    for (uint32_t i = 0; i < *pPhysicalDeviceGroupCount; ++i) {
      for (uint32_t j = 0; j < pPhysicalDeviceGroupProperties[i].physicalDeviceCount; ++j) {
        if (pPhysicalDeviceGroupProperties[i].physicalDevices[j])
          g_phys_device_to_instance[pPhysicalDeviceGroupProperties[i].physicalDevices[j]] = inst_key;
      }
    }
  }

  return res;
}

static VKAPI_ATTR VkResult VKAPI_CALL wuwa_vkEnumerateDeviceExtensionProperties(
    VkPhysicalDevice                            physicalDevice,
    const char*                                 pLayerName,
    uint32_t*                                   pPropertyCount,
    VkExtensionProperties*                      pProperties) {
  (void)physicalDevice;
  (void)pProperties;
  if (pLayerName && std::strcmp(pLayerName, LAYER_NAME) == 0) {
    if (pPropertyCount)
      *pPropertyCount = 0;
    return VK_SUCCESS;
  }
  return VK_ERROR_LAYER_NOT_PRESENT;
}

static VKAPI_ATTR VkResult VKAPI_CALL wuwa_vkEnumerateInstanceExtensionProperties(
    const char*                                 pLayerName,
    uint32_t*                                   pPropertyCount,
    VkExtensionProperties*                      pProperties) {
  (void)pProperties;
  if (pLayerName && std::strcmp(pLayerName, LAYER_NAME) == 0) {
    if (pPropertyCount)
      *pPropertyCount = 0;
    return VK_SUCCESS;
  }
  return VK_ERROR_LAYER_NOT_PRESENT;
}

static VKAPI_ATTR VkResult VKAPI_CALL wuwa_vkEnumerateInstanceLayerProperties(
    uint32_t*                                   pPropertyCount,
    VkLayerProperties*                          pProperties) {
  if (pProperties == nullptr) {
    if (pPropertyCount)
      *pPropertyCount = 1;
    return VK_SUCCESS;
  }

  if (pPropertyCount && *pPropertyCount >= 1) {
    std::strncpy(pProperties[0].layerName, LAYER_NAME, VK_MAX_EXTENSION_NAME_SIZE - 1);
    pProperties[0].layerName[VK_MAX_EXTENSION_NAME_SIZE - 1] = '\0';
#if defined(VK_API_VERSION_1_4)
    pProperties[0].specVersion = VK_API_VERSION_1_4;
#else
    pProperties[0].specVersion = VK_API_VERSION_1_3;
#endif
    pProperties[0].implementationVersion = 1;
    std::strncpy(pProperties[0].description, LAYER_DESC, VK_MAX_DESCRIPTION_SIZE - 1);
    pProperties[0].description[VK_MAX_DESCRIPTION_SIZE - 1] = '\0';
    *pPropertyCount = 1;
    return VK_SUCCESS;
  }
  return VK_INCOMPLETE;
}

// Forward declarations
extern "C" VKAPI_ATTR VkResult VKAPI_CALL wuwa_vkCreateInstance(
    const VkInstanceCreateInfo*                 pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkInstance*                                 pInstance);

// Function resolution
extern "C" VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL wuwa_vkGetDeviceProcAddr(
    VkDevice                                    device,
    const char*                                 pName);

extern "C" VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL wuwa_vkGetInstanceProcAddr(
    VkInstance                                  instance,
    const char*                                 pName);

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL wuwa_vkGetDeviceProcAddr(
    VkDevice                                    device,
    const char*                                 pName) {

  if (!pName)
    return nullptr;

  if (std::strcmp(pName, "vkGetDeviceProcAddr") == 0)
    return reinterpret_cast<PFN_vkVoidFunction>(wuwa_vkGetDeviceProcAddr);
  if (std::strcmp(pName, "vkDestroyDevice") == 0)
    return reinterpret_cast<PFN_vkVoidFunction>(wuwa_vkDestroyDevice);
  if (std::strcmp(pName, "vkCreateShaderModule") == 0)
    return reinterpret_cast<PFN_vkVoidFunction>(wuwa_vkCreateShaderModule);
  if (std::strcmp(pName, "vkCreateGraphicsPipelines") == 0)
    return reinterpret_cast<PFN_vkVoidFunction>(wuwa_vkCreateGraphicsPipelines);
  if (std::strcmp(pName, "vkCreateComputePipelines") == 0)
    return reinterpret_cast<PFN_vkVoidFunction>(wuwa_vkCreateComputePipelines);
  if (std::strcmp(pName, "vkAllocateCommandBuffers") == 0)
    return reinterpret_cast<PFN_vkVoidFunction>(wuwa_vkAllocateCommandBuffers);
  if (std::strcmp(pName, "vkFreeCommandBuffers") == 0)
    return reinterpret_cast<PFN_vkVoidFunction>(wuwa_vkFreeCommandBuffers);
  if (std::strcmp(pName, "vkDestroyPipeline") == 0)
    return reinterpret_cast<PFN_vkVoidFunction>(wuwa_vkDestroyPipeline);
  if (std::strcmp(pName, "vkCmdBindPipeline") == 0)
    return reinterpret_cast<PFN_vkVoidFunction>(wuwa_vkCmdBindPipeline);
  if (std::strcmp(pName, "vkCmdDraw") == 0)
    return reinterpret_cast<PFN_vkVoidFunction>(wuwa_vkCmdDraw);
  if (std::strcmp(pName, "vkCmdDrawIndexed") == 0)
    return reinterpret_cast<PFN_vkVoidFunction>(wuwa_vkCmdDrawIndexed);
  if (std::strcmp(pName, "vkCmdDrawIndirect") == 0)
    return reinterpret_cast<PFN_vkVoidFunction>(wuwa_vkCmdDrawIndirect);
  if (std::strcmp(pName, "vkCmdDrawIndexedIndirect") == 0)
    return reinterpret_cast<PFN_vkVoidFunction>(wuwa_vkCmdDrawIndexedIndirect);

  DeviceData dev_data;
  {
    std::lock_guard<std::mutex> lock(g_lock);
    auto it = g_device_dispatch.find(get_dispatch_key(device));
    if (it != g_device_dispatch.end())
      dev_data = it->second;
  }

  if (dev_data.get_device_proc_addr)
    return dev_data.get_device_proc_addr(device, pName);

  return nullptr;
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL wuwa_vkGetInstanceProcAddr(
    VkInstance                                  instance,
    const char*                                 pName) {

  if (!pName)
    return nullptr;

  if (std::strcmp(pName, "vkGetInstanceProcAddr") == 0)
    return reinterpret_cast<PFN_vkVoidFunction>(wuwa_vkGetInstanceProcAddr);
  if (std::strcmp(pName, "vkGetDeviceProcAddr") == 0)
    return reinterpret_cast<PFN_vkVoidFunction>(wuwa_vkGetDeviceProcAddr);
  if (std::strcmp(pName, "vkCreateInstance") == 0)
    return reinterpret_cast<PFN_vkVoidFunction>(wuwa_vkCreateInstance);
  if (std::strcmp(pName, "vkDestroyInstance") == 0)
    return reinterpret_cast<PFN_vkVoidFunction>(wuwa_vkDestroyInstance);
  if (std::strcmp(pName, "vkEnumeratePhysicalDevices") == 0)
    return reinterpret_cast<PFN_vkVoidFunction>(wuwa_vkEnumeratePhysicalDevices);
  if (std::strcmp(pName, "vkEnumeratePhysicalDeviceGroups") == 0 ||
      std::strcmp(pName, "vkEnumeratePhysicalDeviceGroupsKHR") == 0)
    return reinterpret_cast<PFN_vkVoidFunction>(wuwa_vkEnumeratePhysicalDeviceGroups);
  if (std::strcmp(pName, "vkCreateDevice") == 0)
    return reinterpret_cast<PFN_vkVoidFunction>(wuwa_vkCreateDevice);
  if (std::strcmp(pName, "vkDestroyDevice") == 0)
    return reinterpret_cast<PFN_vkVoidFunction>(wuwa_vkDestroyDevice);
  if (std::strcmp(pName, "vkCreateShaderModule") == 0)
    return reinterpret_cast<PFN_vkVoidFunction>(wuwa_vkCreateShaderModule);
  if (std::strcmp(pName, "vkCreateGraphicsPipelines") == 0)
    return reinterpret_cast<PFN_vkVoidFunction>(wuwa_vkCreateGraphicsPipelines);
  if (std::strcmp(pName, "vkCreateComputePipelines") == 0)
    return reinterpret_cast<PFN_vkVoidFunction>(wuwa_vkCreateComputePipelines);
  if (std::strcmp(pName, "vkAllocateCommandBuffers") == 0)
    return reinterpret_cast<PFN_vkVoidFunction>(wuwa_vkAllocateCommandBuffers);
  if (std::strcmp(pName, "vkFreeCommandBuffers") == 0)
    return reinterpret_cast<PFN_vkVoidFunction>(wuwa_vkFreeCommandBuffers);
  if (std::strcmp(pName, "vkDestroyPipeline") == 0)
    return reinterpret_cast<PFN_vkVoidFunction>(wuwa_vkDestroyPipeline);
  if (std::strcmp(pName, "vkCmdBindPipeline") == 0)
    return reinterpret_cast<PFN_vkVoidFunction>(wuwa_vkCmdBindPipeline);
  if (std::strcmp(pName, "vkCmdDraw") == 0)
    return reinterpret_cast<PFN_vkVoidFunction>(wuwa_vkCmdDraw);
  if (std::strcmp(pName, "vkCmdDrawIndexed") == 0)
    return reinterpret_cast<PFN_vkVoidFunction>(wuwa_vkCmdDrawIndexed);
  if (std::strcmp(pName, "vkCmdDrawIndirect") == 0)
    return reinterpret_cast<PFN_vkVoidFunction>(wuwa_vkCmdDrawIndirect);
  if (std::strcmp(pName, "vkCmdDrawIndexedIndirect") == 0)
    return reinterpret_cast<PFN_vkVoidFunction>(wuwa_vkCmdDrawIndexedIndirect);
  if (std::strcmp(pName, "vkEnumerateInstanceLayerProperties") == 0)
    return reinterpret_cast<PFN_vkVoidFunction>(wuwa_vkEnumerateInstanceLayerProperties);
  if (std::strcmp(pName, "vkEnumerateInstanceExtensionProperties") == 0)
    return reinterpret_cast<PFN_vkVoidFunction>(wuwa_vkEnumerateInstanceExtensionProperties);

  InstanceData inst_data;
  {
    std::lock_guard<std::mutex> lock(g_lock);
    auto it = g_instance_dispatch.find(get_dispatch_key(instance));
    if (it != g_instance_dispatch.end())
      inst_data = it->second;
  }

  if (inst_data.get_instance_proc_addr)
    return inst_data.get_instance_proc_addr(instance, pName);

  return nullptr;
}

extern "C" VKAPI_ATTR VkResult VKAPI_CALL wuwa_vkCreateInstance(
    const VkInstanceCreateInfo*                 pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkInstance*                                 pInstance) {

  VkLayerInstanceCreateInfo* chain_info = get_instance_chain_info(pCreateInfo, VK_LAYER_LINK_INFO);
  if (!chain_info || !chain_info->u.pLayerInfo)
    return VK_ERROR_INITIALIZATION_FAILED;

  PFN_vkGetInstanceProcAddr fpGetInstanceProcAddr = chain_info->u.pLayerInfo->pfnNextGetInstanceProcAddr;

  // Advance link info for subsequent layers
  chain_info->u.pLayerInfo = chain_info->u.pLayerInfo->pNext;

  PFN_vkCreateInstance fpCreateInstance = (PFN_vkCreateInstance)fpGetInstanceProcAddr(VK_NULL_HANDLE, "vkCreateInstance");
  if (!fpCreateInstance)
    return VK_ERROR_INITIALIZATION_FAILED;

  VkResult result = fpCreateInstance(pCreateInfo, pAllocator, pInstance);
  if (result != VK_SUCCESS || !pInstance || !*pInstance)
    return result;

  InstanceData inst_data;
  inst_data.get_instance_proc_addr = fpGetInstanceProcAddr;
  inst_data.destroy_instance = (PFN_vkDestroyInstance)fpGetInstanceProcAddr(*pInstance, "vkDestroyInstance");
  inst_data.create_device = (PFN_vkCreateDevice)fpGetInstanceProcAddr(*pInstance, "vkCreateDevice");
  inst_data.enumerate_physical_devices = (PFN_vkEnumeratePhysicalDevices)fpGetInstanceProcAddr(*pInstance, "vkEnumeratePhysicalDevices");
  inst_data.enumerate_physical_device_groups = (PFN_vkEnumeratePhysicalDeviceGroups)fpGetInstanceProcAddr(*pInstance, "vkEnumeratePhysicalDeviceGroups");
  if (!inst_data.enumerate_physical_device_groups)
    inst_data.enumerate_physical_device_groups = (PFN_vkEnumeratePhysicalDeviceGroups)fpGetInstanceProcAddr(*pInstance, "vkEnumeratePhysicalDeviceGroupsKHR");

  {
    std::lock_guard<std::mutex> lock(g_lock);
    g_instance_dispatch[get_dispatch_key(*pInstance)] = inst_data;
  }

  if (game_logger::is_nte()) {
    static std::once_flag s_nte_mem_probe_flag;
    std::call_once(s_nte_mem_probe_flag, []() {
      std::thread([]() {
        game_logger::log_msg("[NTE-Addon] 启动相机防裁剪热补丁服务线程...\n");
        int stable_confirm_count = 0;
        for (int attempt = 1; attempt <= 30; ++attempt) {
          std::this_thread::sleep_for(std::chrono::seconds(2));

          bool ok = nte_mem::apply_anti_hide_camera_patch();
          if (ok) {
            stable_confirm_count++;
            if (stable_confirm_count >= 3) {
              game_logger::log_msg("[NTE-Addon] 相机防裁剪热补丁已确认稳定常驻 (轮次: %d)\n", attempt);
              break;
            }
          } else {
            stable_confirm_count = 0;
          }
        }
      }).detach();
    });
  }

  return VK_SUCCESS;
}

#if defined(__GNUC__) && __GNUC__ >= 4
#define VK_LAYER_EXPORT extern "C" __attribute__((visibility("default")))
#elif defined(_MSC_VER)
#define VK_LAYER_EXPORT extern "C" __declspec(dllexport)
#else
#define VK_LAYER_EXPORT extern "C"
#endif

// Loader Layer Interface Negotiation (Version 2)
VK_LAYER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL vkNegotiateLoaderLayerInterfaceVersion(
    VkNegotiateLayerInterface*                  pVersionStruct) {

  if (!pVersionStruct || pVersionStruct->sType != LAYER_NEGOTIATE_INTERFACE_STRUCT)
    return VK_ERROR_INITIALIZATION_FAILED;

  if (pVersionStruct->loaderLayerInterfaceVersion < 2)
    return VK_ERROR_INITIALIZATION_FAILED;

  pVersionStruct->loaderLayerInterfaceVersion = 2;
  pVersionStruct->pfnGetInstanceProcAddr = wuwa_vkGetInstanceProcAddr;
  pVersionStruct->pfnGetDeviceProcAddr = wuwa_vkGetDeviceProcAddr;
  pVersionStruct->pfnGetPhysicalDeviceProcAddr = nullptr;

  return VK_SUCCESS;
}

VK_LAYER_EXPORT VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetInstanceProcAddr(
    VkInstance                                  instance,
    const char*                                 pName) {
  return wuwa_vkGetInstanceProcAddr(instance, pName);
}

VK_LAYER_EXPORT VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetDeviceProcAddr(
    VkDevice                                    device,
    const char*                                 pName) {
  return wuwa_vkGetDeviceProcAddr(device, pName);
}

VK_LAYER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateInstanceLayerProperties(
    uint32_t*                                   pPropertyCount,
    VkLayerProperties*                          pProperties) {
  return wuwa_vkEnumerateInstanceLayerProperties(pPropertyCount, pProperties);
}

VK_LAYER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateInstanceExtensionProperties(
    const char*                                 pLayerName,
    uint32_t*                                   pPropertyCount,
    VkExtensionProperties*                      pProperties) {
  return wuwa_vkEnumerateInstanceExtensionProperties(pLayerName, pPropertyCount, pProperties);
}

VK_LAYER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateDeviceExtensionProperties(
    VkPhysicalDevice                            physicalDevice,
    const char*                                 pLayerName,
    uint32_t*                                   pPropertyCount,
    VkExtensionProperties*                      pProperties) {
  return wuwa_vkEnumerateDeviceExtensionProperties(physicalDevice, pLayerName, pPropertyCount, pProperties);
}

