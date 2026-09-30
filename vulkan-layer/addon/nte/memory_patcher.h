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
#include <unordered_map>

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
                                std::vector<uint8_t>* original_bytes_backup = nullptr,
                                bool restore_exec = true) {
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

    if (restore_exec) {
      __builtin___clear_cache(reinterpret_cast<char*>(target_addr),
                              reinterpret_cast<char*>(target_addr + patch_size));
      mprotect(reinterpret_cast<void*>(page_start), protect_len, PROT_READ | PROT_EXEC);
    } else {
      mprotect(reinterpret_cast<void*>(page_start), protect_len, PROT_READ | PROT_WRITE);
    }

    game_logger::log_msg("[NTE-Addon] 安全写入内存成功: 地址 0x%lx | 长度: %zu 字节\n", target_addr, patch_size);
    return true;
  }

  inline bool safe_nop_memory(uintptr_t target_addr, size_t length, std::vector<uint8_t>* original_bytes_backup = nullptr) {
    if (target_addr == 0 || length == 0) return false;
    std::vector<uint8_t> nops(length, 0x90);
    return safe_write_memory(target_addr, nops.data(), nops.size(), original_bytes_backup, true);
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

    // 1. CameraDistanceFadeAlpha::ForceOpaqueBranch (0x07BB8BC6):
    //    原版汇编: test r9, r9; jne +0D; movss xmm0, [1.0f]; jmp epilogue (恢复 rbx, rbp, rsi, rdi 并 ret)
    //    将 jne (75 0D) 改为 NOP (90 90)，使其无条件走函数内置的 1.0f 常量加载与安全栈帧恢复路径。
    //    既能确保角色全视角 100% 不透明，又完美遵守非叶子函数堆栈 ABI
    // 2. CameraOcclusion::BranchGateSkipHide (0x07BC010C):
    //    将条件跳转 ja (77 1A) 改为无条件跳转 jmp (EB 1A)，直接跳过 ExecuteHideCharacter 调用。
    struct MemoryPatchEntry {
      const char* name;
      uintptr_t rva;
      std::vector<uint8_t> patch_bytes;
      std::vector<uint8_t> expected_orig_bytes;
      std::string pattern_fallback;
      size_t pattern_offset;
    };

    const MemoryPatchEntry targets[] = {
      // 1. 距离淡化计算函数内置分支：恒定走 1.0f 路径与栈帧安全返回
      //    原版: 4D 85 C9 (test r9, r9) 75 0D (jne +0D) ...
      //    修改点位于特征码起始位置 +3 字节处，将 75 0D 改为 90 90 (NOP NOP)
      {
        "CameraDistanceFadeAlpha::ForceOpaqueBranch",
        0x07BB8F56, // 更新后新版 RVA (旧版: 0x07BB8BC6)
        { 0x90, 0x90 },
        { 0x75, 0x0D },
        "4D 85 C9 75 0D F3 0F 10 05",
        3
      },
      // 2. 相机透明度隐藏判断门禁：将 ja 改为 jmp，跳过 ExecuteHideCharacter 调用
      //    原版: 77 1A (ja +1A) 4C 8B C7 48 8B CB ...
      //    修改点位于特征码起始位置 (+0 字节)，将 77 1A 改为 EB 1A (jmp +1A)
      {
        "CameraOcclusion::BranchGateSkipHide",
        0x07BC049C, // 更新后新版 RVA (旧版: 0x07BC010C)
        { 0xEB, 0x1A },
        { 0x77, 0x1A },
        "77 1A 4C 8B C7 48 8B CB",
        0
      }
    };

    // 缓存已解析定位的有效内存地址，保障多次轮询下的幂等性
    static std::unordered_map<std::string, uintptr_t> s_resolved_addrs;

    bool all_patched = true;

    for (const auto& tgt : targets) {
      uintptr_t target_addr = 0;
      auto it_resolved = s_resolved_addrs.find(tgt.name);
      if (it_resolved != s_resolved_addrs.end() && it_resolved->second != 0) {
        target_addr = it_resolved->second;
      } else {
        target_addr = module_base + tgt.rva;
      }

      bool target_valid = (target_addr >= code_start && target_addr + tgt.patch_bytes.size() <= code_end);

      // 检查是否已经打过该补丁 (幂等性保护)
      if (target_valid) {
        bool already_patched = true;
        const uint8_t* cur_ptr = reinterpret_cast<const uint8_t*>(target_addr);
        for (size_t i = 0; i < tgt.patch_bytes.size(); ++i) {
          if (cur_ptr[i] != tgt.patch_bytes[i]) {
            already_patched = false;
            break;
          }
        }
        if (already_patched) {
          s_resolved_addrs[tgt.name] = target_addr;
          continue;
        }
      }

      // 前置一致性检查：校验目标地址处的预期机器码是否匹配
      bool orig_matched = false;
      if (target_valid) {
        orig_matched = true;
        const uint8_t* cur_ptr = reinterpret_cast<const uint8_t*>(target_addr);
        for (size_t i = 0; i < tgt.expected_orig_bytes.size(); ++i) {
          if (cur_ptr[i] != tgt.expected_orig_bytes[i]) {
            orig_matched = false;
            break;
          }
        }
      }

      // 若当前地址不匹配预期原始机器码，触发动态特征码扫描
      if (!orig_matched) {
        game_logger::log_msg("[NTE-Addon] 目标 %s 当前地址 (0x%lx) 预期原始机器码不匹配，触发动态特征码扫描...\n",
                             tgt.name, target_addr);
        if (!tgt.pattern_fallback.empty()) {
          uintptr_t matched_addr = scan_module_pattern(module_path, tgt.pattern_fallback, true);
          if (matched_addr != 0) {
            target_addr = matched_addr + tgt.pattern_offset;
            uintptr_t new_rva = target_addr - module_base;
            game_logger::log_msg("[NTE-Addon] 动态特征码扫描成功定位 %s: 新地址 0x%lx (新 RVA: 0x%lx, 偏移: +%zu)\n",
                                 tgt.name, target_addr, new_rva, tgt.pattern_offset);
            target_valid = (target_addr >= code_start && target_addr + tgt.patch_bytes.size() <= code_end);

            // 再次检查扫描出的地址是否已补丁
            if (target_valid) {
              bool already_patched = true;
              const uint8_t* cur_ptr = reinterpret_cast<const uint8_t*>(target_addr);
              for (size_t i = 0; i < tgt.patch_bytes.size(); ++i) {
                if (cur_ptr[i] != tgt.patch_bytes[i]) {
                  already_patched = false;
                  break;
                }
              }
              if (already_patched) {
                s_resolved_addrs[tgt.name] = target_addr;
                continue;
              }
            }
          } else {
            game_logger::log_msg("[NTE-Addon] [FAILED] 目标 %s 动态特征码未命中，拒绝写入\n", tgt.name);
            all_patched = false;
            continue;
          }
        } else {
          game_logger::log_msg("[NTE-Addon] [FAILED] 目标 %s 缺少特征码兜底，拒绝写入\n", tgt.name);
          all_patched = false;
          continue;
        }
      }

      if (!target_valid) {
        game_logger::log_msg("[NTE-Addon] 目标补丁 %s 最终地址 0x%lx 超出代码段 (0x%lx - 0x%lx)\n",
                             tgt.name, target_addr, code_start, code_end);
        all_patched = false;
        continue;
      }

      // 严格安全核验：目标地址当前字节必须 100% 匹配预期原始操作码
      const uint8_t* cur_ptr = reinterpret_cast<const uint8_t*>(target_addr);
      bool bytes_verified = true;
      for (size_t i = 0; i < tgt.expected_orig_bytes.size(); ++i) {
        if (cur_ptr[i] != tgt.expected_orig_bytes[i]) {
          bytes_verified = false;
          break;
        }
      }
      if (!bytes_verified) {
        game_logger::log_msg("[NTE-Addon] [FAILED] 目标 %s 地址 0x%lx 机器码不匹配预期原始操作码，拒绝写入\n",
                             tgt.name, target_addr);
        all_patched = false;
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
        s_resolved_addrs[tgt.name] = target_addr;
      } else {
        game_logger::log_msg("[NTE-Addon] [FAILED] 热补丁 %s 写入失败\n", tgt.name);
        all_patched = false;
      }
    }

    return all_patched;
  }

} // namespace nte_mem
