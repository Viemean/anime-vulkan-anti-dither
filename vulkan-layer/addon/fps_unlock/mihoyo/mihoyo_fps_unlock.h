#pragma once

#include "../../../src/logger.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <thread>
#include <chrono>
#include <atomic>
#include <sys/mman.h>
#include <unistd.h>

namespace mihoyo_fps {

  struct MemoryRegion {
    uintptr_t start_addr = 0;
    uintptr_t end_addr = 0;
    size_t size = 0;
    std::string permissions;
    std::string pathname;

    bool is_executable() const {
      return permissions.find('x') != std::string::npos;
    }

    bool is_readable() const {
      return permissions.find('r') != std::string::npos;
    }

    bool is_writable() const {
      return permissions.find('w') != std::string::npos;
    }
  };

  inline std::vector<MemoryRegion> parse_process_maps(const std::string& filter_name = "") {
    std::vector<MemoryRegion> regions;
    std::ifstream maps_file("/proc/self/maps");
    if (!maps_file.is_open()) {
      return regions;
    }

    std::string line;
    while (std::getline(maps_file, line)) {
      if (line.empty()) continue;

      if (!filter_name.empty() && line.find(filter_name) == std::string::npos) {
        continue;
      }

      std::istringstream iss(line);
      std::string addr_range, perms, offset, dev, inode;
      if (!(iss >> addr_range >> perms >> offset >> dev >> inode)) {
        continue;
      }
      std::string pathname;
      std::getline(iss >> std::ws, pathname);

      size_t dash_pos = addr_range.find('-');
      if (dash_pos == std::string::npos) continue;

      uintptr_t start = 0, end = 0;
      try {
        start = std::stoull(addr_range.substr(0, dash_pos), nullptr, 16);
        end = std::stoull(addr_range.substr(dash_pos + 1), nullptr, 16);
      } catch (...) {
        continue;
      }

      MemoryRegion reg;
      reg.start_addr = start;
      reg.end_addr = end;
      reg.size = (end > start) ? (end - start) : 0;
      reg.permissions = perms;
      reg.pathname = pathname;

      regions.push_back(reg);
    }

    return regions;
  }

  class SignaturePattern {
  public:
    std::vector<int16_t> tokens;

    SignaturePattern() = default;
    explicit SignaturePattern(const std::string& pattern_str) {
      parse(pattern_str);
    }

    void parse(const std::string& str) {
      tokens.clear();
      std::istringstream iss(str);
      std::string token;
      while (iss >> token) {
        if (token == "?" || token == "??") {
          tokens.push_back(-1);
        } else {
          try {
            uint8_t byte_val = static_cast<uint8_t>(std::stoul(token, nullptr, 16));
            tokens.push_back(byte_val);
          } catch (...) {
            tokens.push_back(-1);
          }
        }
      }
    }

    bool empty() const {
      return tokens.empty();
    }

    size_t size() const {
      return tokens.size();
    }
  };

  inline uintptr_t scan_memory(uintptr_t start_addr, size_t size, const SignaturePattern& pattern) {
    if (start_addr == 0 || size < pattern.size() || pattern.empty()) {
      return 0;
    }

    const uint8_t* scan_start = reinterpret_cast<const uint8_t*>(start_addr);
    size_t scan_limit = size - pattern.size();
    const int16_t first_token = pattern.tokens[0];
    const size_t pattern_len = pattern.size();

    for (size_t i = 0; i <= scan_limit; ++i) {
      if (first_token != -1 && scan_start[i] != static_cast<uint8_t>(first_token)) {
        continue;
      }

      bool match = true;
      for (size_t p = 1; p < pattern_len; ++p) {
        int16_t expected = pattern.tokens[p];
        if (expected != -1 && scan_start[i + p] != static_cast<uint8_t>(expected)) {
          match = false;
          break;
        }
      }

      if (match) {
        return start_addr + i;
      }
    }

    return 0;
  }

  inline std::vector<uintptr_t> scan_memory_all(uintptr_t start_addr, size_t size, const SignaturePattern& pattern) {
    std::vector<uintptr_t> results;
    if (start_addr == 0 || size < pattern.size() || pattern.empty()) {
      return results;
    }

    const uint8_t* scan_start = reinterpret_cast<const uint8_t*>(start_addr);
    size_t scan_limit = size - pattern.size();
    const int16_t first_token = pattern.tokens[0];
    const size_t pattern_len = pattern.size();

    for (size_t i = 0; i <= scan_limit; ++i) {
      if (first_token != -1 && scan_start[i] != static_cast<uint8_t>(first_token)) {
        continue;
      }

      bool match = true;
      for (size_t p = 1; p < pattern_len; ++p) {
        int16_t expected = pattern.tokens[p];
        if (expected != -1 && scan_start[i + p] != static_cast<uint8_t>(expected)) {
          match = false;
          break;
        }
      }

      if (match) {
        results.push_back(start_addr + i);
      }
    }

    return results;
  }

  inline bool safe_write_int32(uintptr_t target_addr, int32_t val) {
    if (target_addr == 0) return false;

    long page_size = sysconf(_SC_PAGESIZE);
    if (page_size <= 0) page_size = 4096;

    uintptr_t page_start = target_addr & ~(static_cast<uintptr_t>(page_size) - 1);
    uintptr_t page_end = (target_addr + sizeof(int32_t) + page_size - 1) & ~(static_cast<uintptr_t>(page_size) - 1);
    size_t protect_len = page_end - page_start;

    // 检查页面可写性并提权
    if (mprotect(reinterpret_cast<void*>(page_start), protect_len, PROT_READ | PROT_WRITE | PROT_EXEC) != 0) {
      return false;
    }

    *reinterpret_cast<volatile int32_t*>(target_addr) = val;

    // 恢复常规读写权限
    mprotect(reinterpret_cast<void*>(page_start), protect_len, PROT_READ | PROT_WRITE);
    return true;
  }

#pragma pack(push, 1)
  struct PeDosHeader {
    uint16_t e_magic;    // 0x5A4D
    uint8_t  e_cblp[58];
    uint32_t e_lfanew;   // PE header offset
  };

  struct PeFileHeader {
    uint16_t Machine;
    uint16_t NumberOfSections;
    uint32_t TimeDateStamp;
    uint32_t PointerToSymbolTable;
    uint32_t NumberOfSymbols;
    uint16_t SizeOfOptionalHeader;
    uint16_t Characteristics;
  };

  struct PeSectionHeader {
    char     Name[8];
    uint32_t VirtualSize;
    uint32_t VirtualAddress;
    uint32_t SizeOfRawData;
    uint32_t PointerToRawData;
    uint32_t PointerToRelocations;
    uint32_t PointerToLinenumbers;
    uint16_t NumberOfRelocations;
    uint16_t NumberOfLinenumbers;
    uint32_t Characteristics;
  };
#pragma pack(pop)

  /**
   * @brief 根据星穹铁道及 Unity 引擎通用 targetFrameRate 特征解析目标帧率指针
   *
   * 特征: 66 0F 6E 05 [disp32] F2 0F 10 3D [disp32] 0F 5B C0
   * 指令: movd xmm0, dword ptr [rip + disp32]
   */
  inline int32_t* resolve_framerate_pointer_from_hsr_pattern(uintptr_t match_addr) {
    if (match_addr == 0) return nullptr;

    const uint8_t* code = reinterpret_cast<const uint8_t*>(match_addr);
    if (code[0] != 0x66 || code[1] != 0x0F || code[2] != 0x6E || code[3] != 0x05) {
      return nullptr;
    }

    int32_t disp = *reinterpret_cast<const int32_t*>(code + 4);
    uintptr_t rip = match_addr + 8;
    uintptr_t target_addr = rip + disp;

    if (target_addr != 0 && (target_addr % sizeof(int32_t) == 0)) {
      int32_t current_val = *reinterpret_cast<const int32_t*>(target_addr);
      if (current_val >= 20 && current_val <= 360) {
        return reinterpret_cast<int32_t*>(target_addr);
      }
    }
    return nullptr;
  }

  /**
   * @brief 根据标准 il2cpp set_targetFrameRate 调用点解析全局目标帧率指针
   */
  inline int32_t* resolve_framerate_pointer_from_callsite(uintptr_t callsite_addr) {
    if (callsite_addr == 0) return nullptr;

    const uint8_t* code = reinterpret_cast<const uint8_t*>(callsite_addr);
    // 验证指令特征: mov ecx, 3Ch; call ...
    if (code[0] != 0xB9 || code[1] != 0x3C || code[2] != 0x00 || code[3] != 0x00 || code[4] != 0x00 || code[5] != 0xE8) {
      return nullptr;
    }

    const uint8_t* rip = code + 5;
    int32_t call_disp = *reinterpret_cast<const int32_t*>(rip + 1);
    const uint8_t* dest = rip + call_disp + 5;

    // 处理跨模块跳转或 jmp thunk
    int jump_depth = 0;
    while ((dest[0] == 0xE8 || dest[0] == 0xE9) && jump_depth < 8) {
      int32_t next_disp = *reinterpret_cast<const int32_t*>(dest + 1);
      dest = dest + next_disp + 5;
      jump_depth++;
    }

    // 在目标函数头部扫描 mov [rip + disp], ecx (89 0D) 或 mov [rip + disp], eax (89 05)
    for (size_t offset = 0; offset < 64; ++offset) {
      if ((dest[offset] == 0x89 && dest[offset + 1] == 0x0D) ||
          (dest[offset] == 0x89 && dest[offset + 1] == 0x05)) {
        const uint8_t* insn = dest + offset;
        int32_t disp = *reinterpret_cast<const int32_t*>(insn + 2);
        uintptr_t target_addr = reinterpret_cast<uintptr_t>(insn + 6 + disp);

        // 验证地址对齐与当前数值在合理区间 (20 - 360)
        if (target_addr != 0 && (target_addr % sizeof(int32_t) == 0)) {
          int32_t current_val = *reinterpret_cast<const int32_t*>(target_addr);
          if (current_val >= 20 && current_val <= 360) {
            return reinterpret_cast<int32_t*>(target_addr);
          }
        }
      }
    }

    return nullptr;
  }

  /**
   * @brief 在指定的 PE 模块镜像中扫描帧率目标地址
   */
  inline int32_t* scan_pe_module_for_framerate(uintptr_t pe_base) {
    if (pe_base == 0) return nullptr;

    const auto* dos_hdr = reinterpret_cast<const PeDosHeader*>(pe_base);
    if (!dos_hdr || dos_hdr->e_magic != 0x5A4D || dos_hdr->e_lfanew >= 0x1000) {
      return nullptr;
    }

    const uint8_t* nt_hdr_raw = reinterpret_cast<const uint8_t*>(pe_base + dos_hdr->e_lfanew);
    uint32_t pe_sig = *reinterpret_cast<const uint32_t*>(nt_hdr_raw);
    if (pe_sig != 0x00004550) { // "PE\0\0"
      return nullptr;
    }

    const auto* file_hdr = reinterpret_cast<const PeFileHeader*>(nt_hdr_raw + 4);
    uint16_t num_sections = file_hdr->NumberOfSections;
    uint16_t opt_size = file_hdr->SizeOfOptionalHeader;
    const auto* sec_hdrs = reinterpret_cast<const PeSectionHeader*>(nt_hdr_raw + 24 + opt_size);

    SignaturePattern hsr_pattern("66 0F 6E 05 ? ? ? ? F2 0F 10 3D ? ? ? ? 0F 5B C0");
    SignaturePattern call_pattern("B9 3C 00 00 00 E8");

    for (uint16_t i = 0; i < num_sections; ++i) {
      char sec_name[9] = {0};
      std::memcpy(sec_name, sec_hdrs[i].Name, 8);

      // 重点扫描 il2cpp 节、.text 节或包含代码的节
      if (std::strcmp(sec_name, "il2cpp") == 0 || std::strcmp(sec_name, ".text") == 0 ||
          (sec_hdrs[i].Characteristics & 0x20000020)) {
        uintptr_t sec_vaddr = pe_base + sec_hdrs[i].VirtualAddress;
        size_t sec_vsize = sec_hdrs[i].VirtualSize;

        // 优先匹配星穹铁道及 Unity 通用 targetFrameRate 计算点
        auto hsr_matches = scan_memory_all(sec_vaddr, sec_vsize, hsr_pattern);
        for (uintptr_t match : hsr_matches) {
          int32_t* p_fps = resolve_framerate_pointer_from_hsr_pattern(match);
          if (p_fps) {
            game_logger::log_msg("[FPS解锁-MIHOYO] 在 PE 模块 0x%lx 节 [%s] 命中 HSR/Unity 帧率变量: 0x%lx\n",
                                 pe_base, sec_name, reinterpret_cast<uintptr_t>(p_fps));
            return p_fps;
          }
        }

        // 备选匹配原神 il2cpp 调用点
        auto call_matches = scan_memory_all(sec_vaddr, sec_vsize, call_pattern);
        for (uintptr_t match : call_matches) {
          int32_t* p_fps = resolve_framerate_pointer_from_callsite(match);
          if (p_fps) {
            game_logger::log_msg("[FPS解锁-MIHOYO] 在 PE 模块 0x%lx 节 [%s] 命中原神 il2cpp 帧率变量: 0x%lx\n",
                                 pe_base, sec_name, reinterpret_cast<uintptr_t>(p_fps));
            return p_fps;
          }
        }
      }
    }

    return nullptr;
  }

  /**
   * @brief 在进程内存空间中寻找米哈游游戏的全局帧率目标地址
   *
   * 依次检索:
   * 1. UnityPlayer.dll (星穹铁道等独立引擎 DLL 架构)
   * 2. 游戏主程序 (原神等单体整合架构)
   * 3. 内存可执行映射段保底扫描 (应对 Wine/Proton 虚拟段重映射)
   */
  inline int32_t* find_target_framerate_address() {
    auto regions = parse_process_maps();
    if (regions.empty()) return nullptr;

    // 1. 尝试检索 UnityPlayer.dll 基址 (星穹铁道主要逻辑位于此)
    for (const auto& reg : regions) {
      if (reg.pathname.find("UnityPlayer.dll") != std::string::npos && reg.is_readable()) {
        const auto* dos_hdr = reinterpret_cast<const PeDosHeader*>(reg.start_addr);
        if (dos_hdr && dos_hdr->e_magic == 0x5A4D && dos_hdr->e_lfanew < 0x1000) {
          int32_t* p_fps = scan_pe_module_for_framerate(reg.start_addr);
          if (p_fps) return p_fps;
        }
      }
    }

    // 2. 尝试检索游戏主程序 EXE (原神等单体整合架构)
    for (const auto& reg : regions) {
      if (reg.start_addr >= 0x140000000 && reg.is_readable() &&
          (reg.pathname.find(".exe") != std::string::npos ||
           reg.pathname.find("YuanShen") != std::string::npos ||
           reg.pathname.find("StarRail") != std::string::npos)) {
        const auto* dos_hdr = reinterpret_cast<const PeDosHeader*>(reg.start_addr);
        if (dos_hdr && dos_hdr->e_magic == 0x5A4D && dos_hdr->e_lfanew < 0x1000) {
          int32_t* p_fps = scan_pe_module_for_framerate(reg.start_addr);
          if (p_fps) return p_fps;
        }
      }
    }

    // 3. 备选方案：遍历进程中所有可执行段进行特征扫描 (应对 Wine 内存重映射)
    SignaturePattern hsr_pattern("66 0F 6E 05 ? ? ? ? F2 0F 10 3D ? ? ? ? 0F 5B C0");
    SignaturePattern call_pattern("B9 3C 00 00 00 E8");

    for (const auto& reg : regions) {
      if (!reg.is_readable() || !reg.is_executable()) continue;
      if (reg.size < 1024 * 1024) continue; // 允许扫描 1MB 以上的代码段

      auto hsr_matches = scan_memory_all(reg.start_addr, reg.size, hsr_pattern);
      for (uintptr_t match : hsr_matches) {
        int32_t* p_fps = resolve_framerate_pointer_from_hsr_pattern(match);
        if (p_fps) {
          game_logger::log_msg("[FPS解锁-MIHOYO] 在内存映射段 0x%lx 命中 HSR/Unity 目标帧率变量: 0x%lx\n",
                               reg.start_addr, reinterpret_cast<uintptr_t>(p_fps));
          return p_fps;
        }
      }

      auto call_matches = scan_memory_all(reg.start_addr, reg.size, call_pattern);
      for (uintptr_t match : call_matches) {
        int32_t* p_fps = resolve_framerate_pointer_from_callsite(match);
        if (p_fps) {
          game_logger::log_msg("[FPS解锁-MIHOYO] 在内存映射段 0x%lx 命中原神 il2cpp 目标帧率变量: 0x%lx\n",
                               reg.start_addr, reinterpret_cast<uintptr_t>(p_fps));
          return p_fps;
        }
      }
    }

    return nullptr;
  }

  inline std::atomic<bool>& get_service_running() {
    static std::atomic<bool> s_running{false};
    return s_running;
  }

  /**
   * @brief 启动米哈游帧率解锁与防重置常驻守护服务
   */
  inline void start_fps_unlock_service() {
    if (!game_logger::is_fps_unlock_enabled()) {
      return;
    }

    bool expected = false;
    if (!get_service_running().compare_exchange_strong(expected, true)) {
      return;
    }

    std::thread([]() {
      int32_t configured_fps = game_logger::get_target_fps();
      int32_t engine_fps = game_logger::get_effective_engine_fps();

      if (configured_fps == 0) {
        game_logger::log_msg("[FPS解锁-MIHOYO] 启动守护服务 | 设定目标帧率: 0 (解除最大上限, 引擎值: -1)\n");
      } else {
        game_logger::log_msg("[FPS解锁-MIHOYO] 启动守护服务 | 设定目标帧率: %d\n", engine_fps);
      }

      int32_t* p_fps = nullptr;

      // 前期阶段轮询定位目标地址 (至多尝试 45 轮，每轮间隔 2 秒)
      for (int attempt = 1; attempt <= 45; ++attempt) {
        std::this_thread::sleep_for(std::chrono::seconds(2));

        p_fps = find_target_framerate_address();
        if (p_fps) {
          int32_t old_val = *p_fps;
          safe_write_int32(reinterpret_cast<uintptr_t>(p_fps), engine_fps);
          if (configured_fps == 0) {
            game_logger::log_msg("[FPS解锁-MIHOYO] 成功定位目标帧率变量: 0x%lx | 原值: %d -> 解除最大上限 (引擎值: -1, 轮次: %d)\n",
                                 reinterpret_cast<uintptr_t>(p_fps), old_val, attempt);
          } else {
            game_logger::log_msg("[FPS解锁-MIHOYO] 成功定位目标帧率变量: 0x%lx | 原值: %d -> 写入目标帧率: %d (轮次: %d)\n",
                                 reinterpret_cast<uintptr_t>(p_fps), old_val, engine_fps, attempt);
          }
          break;
        }
      }

      if (!p_fps) {
        game_logger::log_msg("[FPS解锁-MIHOYO] 未能在目标模块中命中帧率变量特征\n");
        get_service_running().store(false);
        return;
      }

      // 常驻轻量守护：每 500ms 检查一次，游戏切图/重新进入菜单将帧率回退为 60 或 30 时自动恢复
      while (get_service_running().load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));

        int32_t cur_conf = game_logger::get_target_fps();
        int32_t cur_engine = game_logger::get_effective_engine_fps();

        int32_t current_val = *p_fps;
        if (current_val != cur_engine) {
          safe_write_int32(reinterpret_cast<uintptr_t>(p_fps), cur_engine);
          if (cur_conf == 0) {
            game_logger::log_msg("[FPS解锁-MIHOYO] 检测到游戏重置帧率 (%d)，平滑恢复解除最大上限\n",
                                 current_val);
          } else {
            game_logger::log_msg("[FPS解锁-MIHOYO] 检测到游戏重置帧率 (%d)，平滑恢复目标帧率: %d\n",
                                 current_val, cur_engine);
          }
        }
      }
    }).detach();
  }

} // namespace mihoyo_fps
