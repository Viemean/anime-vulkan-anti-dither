#include <vulkan/vulkan.h>
#include <vulkan/vk_layer.h>

#include "wuwa/wuwa_anti_dither.h"
#include "azur_promilia/azur_promilia_anti_dither.h"
#include "nte/nte_anti_dither.h"
#include "hsr/hsr_anti_dither.h"
#include "genshin/genshin_anti_dither.h"
#include "zzz/zzz_anti_dither.h"
#include "HI3rd/hi3_anti_dither.h"
#include "zmd/zmd_anti_dither.h"
#include "gf2/gf2_anti_dither.h"
#include "tof/tof_anti_dither.h"
#include "dna/dna_anti_dither.h"
#include "star/star_anti_dither.h"
#include "../addon/nte/memory_patcher.h"

#include <mutex>
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
    if (game_logger::is_genshin()) {
      genshin_layer::process_spirv_anti_dither(code, word_count);
    } else if (game_logger::is_hsr()) {
      hsr_layer::process_spirv_anti_dither(code, word_count);
    } else if (game_logger::is_zzz()) {
      zzz_layer::process_spirv_anti_dither(code, word_count);
    } else if (game_logger::is_wuwa()) {
      wuwa_layer::process_spirv_anti_dither(code, word_count);
    } else if (game_logger::is_nte()) {
      nte_layer::process_spirv_anti_dither(code, word_count);
    } else if (game_logger::is_azur_promilia()) {
      azur_promilia_layer::process_spirv_anti_dither(code, word_count);
    } else if (game_logger::is_hi3()) {
      hi3_layer::process_spirv_anti_dither(code, word_count);
    } else if (game_logger::is_zmd()) {
      zmd_layer::process_spirv_anti_dither(code, word_count);
    } else if (game_logger::is_gf2()) {
      gf2_layer::process_spirv_anti_dither(code, word_count);
    } else if (game_logger::is_tof()) {
      tof_layer::process_spirv_anti_dither(code, word_count);
    } else if (game_logger::is_dna()) {
      dna_layer::process_spirv_anti_dither(code, word_count);
    } else if (game_logger::is_star()) {
      star_layer::process_spirv_anti_dither(code, word_count);
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
  };

  std::mutex g_lock;
  std::unordered_map<void*, InstanceData> g_instance_dispatch;
  std::unordered_map<void*, DeviceData> g_device_dispatch;
  std::unordered_map<VkPhysicalDevice, void*> g_phys_device_to_instance;

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

    dispatch_anti_dither_process(patched_code.data(), patched_code.size());

    VkShaderModuleCreateInfo modified_info = *pCreateInfo;
    modified_info.pCode = patched_code.data();

    return dev_data.create_shader_module(device, &modified_info, pAllocator, pShaderModule);
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

  for (uint32_t i = 0; i < createInfoCount; ++i) {
    const auto& orig_info = pCreateInfos[i];
    if (!orig_info.pStages || orig_info.stageCount == 0)
      continue;

    modified_stages[i].assign(orig_info.pStages, orig_info.pStages + orig_info.stageCount);
    modified_module_infos[i].resize(orig_info.stageCount);
    patched_codes[i].resize(orig_info.stageCount);

    for (uint32_t s = 0; s < orig_info.stageCount; ++s) {
      auto& stage = modified_stages[i][s];
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

        if (stage.stage == VK_SHADER_STAGE_FRAGMENT_BIT ||
            ((game_logger::is_nte() || game_logger::is_hsr() || game_logger::is_genshin() || game_logger::is_tof()) && stage.stage == VK_SHADER_STAGE_VERTEX_BIT)) {
          dispatch_anti_dither_process(patched_codes[i][s].data(), patched_codes[i][s].size());
        }

        modified_module_infos[i][s] = *mod_info;
        modified_module_infos[i][s].pCode = patched_codes[i][s].data();
        stage.pNext = &modified_module_infos[i][s];
      }
    }

    modified_infos[i].pStages = modified_stages[i].data();
  }

  return dev_data.create_graphics_pipelines(device, pipelineCache, createInfoCount, modified_infos.data(), pAllocator, pPipelines);
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

  {
    std::lock_guard<std::mutex> lock(g_lock);
    g_device_dispatch[get_dispatch_key(*pDevice)] = dev_data;
  }

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

