#pragma once

#include "../../src/logger.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <sys/mman.h>
#include <unistd.h>

namespace nte_mem {

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
      game_logger::log_msg("[NTE-Addon] 无法读取 /proc/self/maps\n");
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
    std::vector<int16_t> tokens; // -1 represents wildcard

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

  inline uintptr_t scan_module_pattern(const std::string& module_filter,
                                       const std::string& pattern_str,
                                       bool executable_only = true) {
    SignaturePattern pattern(pattern_str);
    if (pattern.empty()) {
      game_logger::log_msg("[NTE-Addon] 特征码解析为空: %s\n", pattern_str.c_str());
      return 0;
    }

    auto regions = parse_process_maps(module_filter);
    if (regions.empty()) {
      game_logger::log_msg("[NTE-Addon] 未在进程映射中定位到目标模块: %s\n", module_filter.c_str());
      return 0;
    }

    for (const auto& reg : regions) {
      if (!reg.is_readable()) continue;
      if (executable_only && !reg.is_executable()) continue;

      uintptr_t match = scan_memory(reg.start_addr, reg.size, pattern);
      if (match != 0) {
        game_logger::log_msg("[NTE-Addon] 命中特征码: 0x%lx | 模块段: 0x%lx - 0x%lx (%s) | 权限: %s\n",
                             match, reg.start_addr, reg.end_addr, reg.pathname.c_str(), reg.permissions.c_str());
        return match;
      }
    }

    return 0;
  }

  inline bool safe_write_memory(uintptr_t target_addr,
                                const uint8_t* patch_bytes,
                                size_t patch_size,
                                std::vector<uint8_t>* original_bytes_backup = nullptr) {
    if (target_addr == 0 || !patch_bytes || patch_size == 0) {
      return false;
    }

    long page_size = sysconf(_SC_PAGESIZE);
    if (page_size <= 0) page_size = 4096;

    uintptr_t page_start = target_addr & ~(static_cast<uintptr_t>(page_size) - 1);
    uintptr_t page_end = (target_addr + patch_size + page_size - 1) & ~(static_cast<uintptr_t>(page_size) - 1);
    size_t protect_len = page_end - page_start;

    if (mprotect(reinterpret_cast<void*>(page_start), protect_len, PROT_READ | PROT_WRITE | PROT_EXEC) != 0) {
      game_logger::log_msg("[NTE-Addon] mprotect 提权失败: 地址 0x%lx, 长度 %zu\n", page_start, protect_len);
      return false;
    }

    if (original_bytes_backup) {
      original_bytes_backup->assign(reinterpret_cast<const uint8_t*>(target_addr),
                                    reinterpret_cast<const uint8_t*>(target_addr) + patch_size);
    }

    std::memcpy(reinterpret_cast<void*>(target_addr), patch_bytes, patch_size);

    __builtin___clear_cache(reinterpret_cast<char*>(target_addr),
                            reinterpret_cast<char*>(target_addr + patch_size));

    mprotect(reinterpret_cast<void*>(page_start), protect_len, PROT_READ | PROT_EXEC);

    game_logger::log_msg("[NTE-Addon] 安全写入内存成功: 地址 0x%lx | 长度: %zu 字节\n", target_addr, patch_size);
    return true;
  }

  inline bool safe_nop_memory(uintptr_t target_addr, size_t length, std::vector<uint8_t>* original_bytes_backup = nullptr) {
    if (target_addr == 0 || length == 0) return false;
    std::vector<uint8_t> nops(length, 0x90);
    return safe_write_memory(target_addr, nops.data(), nops.size(), original_bytes_backup);
  }

  struct FunctionPatchTarget {
    const char* name;
    uintptr_t rva;
  };

  inline bool apply_anti_hide_camera_patch() {
    auto regions = parse_process_maps();
    uintptr_t code_start = 0;
    uintptr_t code_end = 0;
    std::string module_path;

    for (const auto& r : regions) {
      if (r.is_executable() && (r.pathname.find("HTGame") != std::string::npos ||
                                r.pathname.find(".exe") != std::string::npos ||
                                r.size >= 50 * 1024 * 1024)) {
        code_start = r.start_addr;
        code_end = r.end_addr;
        module_path = r.pathname;
        break;
      }
    }

    if (code_start == 0) {
      return false;
    }

    uintptr_t module_base = 0x140000000;
    if ((code_start & 0xFFF) == 0x000 && code_start >= 0x1000) {
      module_base = code_start - 0x1000;
    }

    struct MemoryPatchEntry {
      const char* name;
      uintptr_t rva;
      std::vector<uint8_t> patch_bytes;
      std::vector<uint8_t> expected_orig_bytes;
    };

    static const MemoryPatchEntry targets[] = {
      // 1. 相机距离与俯仰角曲线淡化因子计算函数：入口强制返回 1.0f (mov eax, 0x3f800000; movd xmm0, eax; ret)
      {
        "CameraDistanceFadeAlpha::Compute",
        0x07BB8BA0,
        { 0xB8, 0x00, 0x00, 0x80, 0x3F, 0x66, 0x0F, 0x6E, 0xC0, 0xC3 },
        { 0x48, 0x89, 0x5C, 0x24, 0x08 }
      },
      // 2. 相机透明度隐藏判断门禁：将 ja (77 1A) 改为 jmp (EB 1A)，无条件跳过隐藏函数调用
      {
        "CameraOcclusion::BranchGateSkipHide",
        0x07BC010C,
        { 0xEB, 0x1A },
        { 0x77, 0x1A }
      },
      // 3. 角色隐藏核心执行函数：入口置为 RET (C3)，彻底阻止将角色 HiddenInGame 设为 true
      {
        "CameraOcclusion::ExecuteHideCharacter",
        0x07BDB5A0,
        { 0xC3 },
        { 0x4D, 0x85, 0xC0 }
      },
      // 4. 角色网格隐藏函数原生实现：入口置为 RET (C3)
      {
        "AHTCharacter::HideCharacterMesh",
        0x06EA52B0,
        { 0xC3 },
        { 0x48, 0x83, 0xEC, 0x28 }
      },
      // 5. 角色网格隐藏函数蓝图封装：入口置为 RET (C3)
      {
        "execHideCharacterMesh",
        0x06EA55A0,
        { 0xC3 },
        { 0x48, 0x83, 0xEC, 0x28 }
      }
    };

    bool all_patched = true;

    for (const auto& tgt : targets) {
      uintptr_t target_addr = module_base + tgt.rva;
      if (target_addr < code_start || target_addr + tgt.patch_bytes.size() > code_end) {
        game_logger::log_msg("[NTE-Addon] 目标补丁 %s 地址 0x%lx 超出代码段 (0x%lx - 0x%lx)\n",
                             tgt.name, target_addr, code_start, code_end);
        all_patched = false;
        continue;
      }

      // 检查是否已经打过该补丁
      bool already_patched = true;
      const uint8_t* cur_ptr = reinterpret_cast<const uint8_t*>(target_addr);
      for (size_t i = 0; i < tgt.patch_bytes.size(); ++i) {
        if (cur_ptr[i] != tgt.patch_bytes[i]) {
          already_patched = false;
          break;
        }
      }
      if (already_patched) {
        continue;
      }

      uint8_t preview[8] = {0};
      std::memcpy(preview, reinterpret_cast<const void*>(target_addr), sizeof(preview));

      game_logger::log_msg("[NTE-Addon] 锁定拦截点 %s (0x%lx) | 当前机器码: %02X %02X %02X %02X %02X %02X %02X %02X\n",
                           tgt.name, target_addr,
                           preview[0], preview[1], preview[2], preview[3],
                           preview[4], preview[5], preview[6], preview[7]);

      std::vector<uint8_t> backup;
      if (safe_write_memory(target_addr, tgt.patch_bytes.data(), tgt.patch_bytes.size(), &backup)) {
        game_logger::log_msg("[NTE-Addon] [SUCCESS] 已成功热补丁 %s | 写入 %zu 字节\n",
                             tgt.name, tgt.patch_bytes.size());
      } else {
        game_logger::log_msg("[NTE-Addon] [FAILED] 热补丁 %s 写入失败\n", tgt.name);
        all_patched = false;
      }
    }

    return all_patched;
  }

} // namespace nte_mem
