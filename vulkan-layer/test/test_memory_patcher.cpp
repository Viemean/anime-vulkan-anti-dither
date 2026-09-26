#include "../addon/nte/memory_patcher.h"
#include <iostream>
#include <cassert>
#include <vector>
#include <sys/mman.h>

void test_maps_parsing() {
  std::cout << "[Test 1] Process maps parsing... ";
  auto regions = nte_mem::parse_process_maps();
  assert(!regions.empty());

  bool found_exec = false;
  for (const auto& r : regions) {
    if (r.is_executable()) {
      found_exec = true;
      break;
    }
  }
  (void)found_exec;
  assert(found_exec);
  std::cout << "PASSED (Found " << regions.size() << " regions)\n";
}

void test_pattern_matching() {
  std::cout << "[Test 2] Signature pattern scanning with wildcards... ";
  std::vector<uint8_t> buffer = {
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10,
    0x57, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xD9, 0x80, 0x79
  };

  nte_mem::SignaturePattern pattern("48 89 5C 24 ? 48 89 74 24 ? 57 48 83 EC");
  assert(pattern.size() == 14);

  uintptr_t base = reinterpret_cast<uintptr_t>(buffer.data());
  uintptr_t match = nte_mem::scan_memory(base, buffer.size(), pattern);
  (void)match;
  assert(match == base);

  nte_mem::SignaturePattern pattern2("48 8B D9 ?? 79");
  uintptr_t match2 = nte_mem::scan_memory(base, buffer.size(), pattern2);
  (void)match2;
  assert(match2 == base + 15);

  nte_mem::SignaturePattern no_match("FF FF FF");
  uintptr_t match_fail = nte_mem::scan_memory(base, buffer.size(), no_match);
  (void)match_fail;
  assert(match_fail == 0);

  std::cout << "PASSED\n";
}

void test_safe_memory_patching() {
  std::cout << "[Test 3] Safe memory write & NOP on mprotected pages... ";
  long page_size = sysconf(_SC_PAGESIZE);
  assert(page_size > 0);

  // Allocate an anonymous page and set to Read-Only
  void* page = mmap(nullptr, page_size, PROT_READ, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
  assert(page != MAP_FAILED);

  uintptr_t target = reinterpret_cast<uintptr_t>(page) + 128;

  // Safe patch
  std::vector<uint8_t> patch = { 0x90, 0x90, 0xEB, 0x05 };
  std::vector<uint8_t> backup;
  bool ok = nte_mem::safe_write_memory(target, patch.data(), patch.size(), &backup);
  (void)ok;
  assert(ok);
  assert(backup.size() == patch.size());

  // Verify written data
  const uint8_t* read_ptr = reinterpret_cast<const uint8_t*>(target);
  (void)read_ptr;
  assert(read_ptr[0] == 0x90);
  assert(read_ptr[1] == 0x90);
  assert(read_ptr[2] == 0xEB);
  assert(read_ptr[3] == 0x05);

  // Safe NOP
  bool nop_ok = nte_mem::safe_nop_memory(target, 4);
  (void)nop_ok;
  assert(nop_ok);
  for (int i = 0; i < 4; ++i) {
    assert(read_ptr[i] == 0x90);
  }

  munmap(page, page_size);
  std::cout << "PASSED\n";
}

void test_rva_validation_and_fallback() {
  std::cout << "[Test 4] RVA expected byte validation & AOB fallback simulation... ";
  std::vector<uint8_t> fake_code = {
    // 0x00: 伪造的漂移代码 (不是预期字节)
    0x90, 0x90, 0x90, 0x90,
    // 0x04: 实际目标函数漂移到了这里
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0xC3
  };

  uintptr_t base = reinterpret_cast<uintptr_t>(fake_code.data());
  uintptr_t wrong_rva = 0; // 错误 RVA (模拟版本漂移)
  std::vector<uint8_t> expected = { 0x48, 0x89, 0x5C, 0x24, 0x08 };

  // 1. 模拟静态校验失败
  const uint8_t* ptr_wrong = reinterpret_cast<const uint8_t*>(base + wrong_rva);
  bool matched = true;
  for (size_t i = 0; i < expected.size(); ++i) {
    if (ptr_wrong[i] != expected[i]) {
      matched = false;
      break;
    }
  }
  (void)matched;
  assert(!matched); // 预期静态校验失败

  // 2. 模拟触发 AOB 特征码兜底搜索
  nte_mem::SignaturePattern fallback_pat("48 89 5C 24 08 48 89 6C");
  uintptr_t fallback_addr = nte_mem::scan_memory(base, fake_code.size(), fallback_pat);
  (void)fallback_addr;
  assert(fallback_addr == base + 4); // 成功定位到漂移后的新地址

  // 3. 模拟熔断：特征码完全不存在时必须拒绝
  nte_mem::SignaturePattern invalid_pat("DE AD BE EF");
  uintptr_t no_addr = nte_mem::scan_memory(base, fake_code.size(), invalid_pat);
  (void)no_addr;
  assert(no_addr == 0); // 熔断保护

  std::cout << "PASSED\n";
}

int main() {
  std::cout << "Running memory_patcher unit tests...\n";
  test_maps_parsing();
  test_pattern_matching();
  test_safe_memory_patching();
  test_rva_validation_and_fallback();
  std::cout << "All memory_patcher unit tests PASSED.\n";
  return 0;
}
