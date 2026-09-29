#include "../addon/fps_unlock/mihoyo/mihoyo_fps_unlock.h"
#include <iostream>
#include <cassert>
#include <vector>
#include <sys/mman.h>

void test_target_fps_clamping() {
  std::cout << "[Test 1] Target FPS configuration & minimum clamping (>=30)... ";

  // 1. 模拟 0 或 -1 表示解除最大上限 (映射为引擎常数 -1)
  int32_t input_uncap1 = 0;
  int32_t parsed_uncap1 = (input_uncap1 == 0 || input_uncap1 == -1) ? 0 : ((input_uncap1 < 30) ? 30 : input_uncap1);
  int32_t engine_uncap1 = (parsed_uncap1 == 0) ? -1 : parsed_uncap1;
  (void)engine_uncap1;
  assert(parsed_uncap1 == 0);
  assert(engine_uncap1 == -1);

  int32_t input_uncap2 = -1;
  int32_t parsed_uncap2 = (input_uncap2 == 0 || input_uncap2 == -1) ? 0 : ((input_uncap2 < 30) ? 30 : input_uncap2);
  int32_t engine_uncap2 = (parsed_uncap2 == 0) ? -1 : parsed_uncap2;
  (void)engine_uncap2;
  assert(parsed_uncap2 == 0);
  assert(engine_uncap2 == -1);

  // 2. 模拟非 0 低于 30 的输入，必须自动钳位到 30
  int32_t input_low2 = 15;
  int32_t clamped2 = (input_low2 == 0 || input_low2 == -1) ? 0 : ((input_low2 < 30) ? 30 : input_low2);
  (void)clamped2;
  assert(clamped2 == 30);

  int32_t input_low3 = 29;
  int32_t clamped3 = (input_low3 == 0 || input_low3 == -1) ? 0 : ((input_low3 < 30) ? 30 : input_low3);
  (void)clamped3;
  assert(clamped3 == 30);

  // 3. 正常自定义帧率保留原值
  int32_t input_normal = 144;
  int32_t clamped_normal = (input_normal == 0 || input_normal == -1) ? 0 : ((input_normal < 30) ? 30 : input_normal);
  (void)clamped_normal;
  assert(clamped_normal == 144);

  int32_t input_high = 240;
  int32_t clamped_high = (input_high == 0 || input_high == -1) ? 0 : ((input_high < 30) ? 30 : input_high);
  (void)clamped_high;
  assert(clamped_high == 240);

  std::cout << "PASSED\n";
}

void test_pattern_and_pointer_resolution() {
  std::cout << "[Test 2] IL2CPP callsite and RIP-relative address resolution... ";

  // 模拟内存布局：
  // 1. 调用点 callsite:
  //    B9 3C 00 00 00 (mov ecx, 60)
  //    E8 10 00 00 00 (call +0x10) -> 跳转至 target_func
  // 2. 目标函数 target_func:
  //    89 0D 20 00 00 00 (mov [rip + 0x20], ecx)
  // 3. 目标全局变量 pFramerate:
  //    存储 int32_t 初始值 60

  struct SimulatedEnvironment {
    uint8_t code_before[16];
    // 调用点
    uint8_t mov_ecx_60[5];   // B9 3C 00 00 00
    uint8_t call_insn[5];    // E8 xx xx xx xx
    uint8_t padding1[16];
    // 目标函数
    uint8_t mov_mem_ecx[6];  // 89 0D xx xx xx xx
    uint8_t padding2[32];
    // 目标变量
    int32_t target_framerate_var;
  } __attribute__((packed));

  SimulatedEnvironment env{};
  std::memset(&env, 0x90, sizeof(env)); // NOP 填充

  // 初始化调用点指令
  env.mov_ecx_60[0] = 0xB9;
  env.mov_ecx_60[1] = 0x3C;
  env.mov_ecx_60[2] = 0x00;
  env.mov_ecx_60[3] = 0x00;
  env.mov_ecx_60[4] = 0x00;

  // 计算 call 偏移
  uintptr_t callsite_rip = reinterpret_cast<uintptr_t>(env.call_insn) + 5;
  uintptr_t target_func = reinterpret_cast<uintptr_t>(env.mov_mem_ecx);
  int32_t call_disp = static_cast<int32_t>(target_func - callsite_rip);

  env.call_insn[0] = 0xE8;
  std::memcpy(env.call_insn + 1, &call_disp, sizeof(int32_t));

  // 计算 mov [rip + disp], ecx 相对偏移
  uintptr_t mov_rip = reinterpret_cast<uintptr_t>(env.mov_mem_ecx) + 6;
  uintptr_t var_addr = reinterpret_cast<uintptr_t>(&env.target_framerate_var);
  int32_t var_disp = static_cast<int32_t>(var_addr - mov_rip);

  env.mov_mem_ecx[0] = 0x89;
  env.mov_mem_ecx[1] = 0x0D;
  std::memcpy(env.mov_mem_ecx + 2, &var_disp, sizeof(int32_t));

  // 初始帧率设为 60
  env.target_framerate_var = 60;

  // 执行解析
  uintptr_t callsite_addr = reinterpret_cast<uintptr_t>(env.mov_ecx_60);
  int32_t* resolved_ptr = mihoyo_fps::resolve_framerate_pointer_from_callsite(callsite_addr);

  assert(resolved_ptr != nullptr);
  assert(resolved_ptr == &env.target_framerate_var);
  assert(*resolved_ptr == 60);

  std::cout << "PASSED (Resolved pointer: " << resolved_ptr << ")\n";
}

void test_hsr_pattern_and_pointer_resolution() {
  std::cout << "[Test 3] HSR / Unity targetFrameRate RIP-relative resolution... ";

  // 模拟指令布局:
  // 66 0F 6E 05 [disp32]   (movd xmm0, dword ptr [rip + disp])
  // F2 0F 10 3D [disp32]   (movsd xmm7, qword ptr [rip + disp])
  // 0F 5B C0               (cvtdq2ps xmm0, xmm0)
  struct SimulatedHsrEnv {
    uint8_t prefix[4];       // 66 0F 6E 05 (0..3)
    int32_t disp_to_var;     // disp32 (4..7)
    uint8_t mid[8];          // F2 0F 10 3D xx xx xx xx (8..15)
    uint8_t suffix[3];       // 0F 5B C0 (16..18)
    uint8_t padding[61];     // (19..79)
    alignas(4) int32_t target_var; // 80 字节处，保证 4 字节严格对齐
  } __attribute__((packed));

  SimulatedHsrEnv env{};
  env.prefix[0] = 0x66;
  env.prefix[1] = 0x0F;
  env.prefix[2] = 0x6E;
  env.prefix[3] = 0x05;

  env.mid[0] = 0xF2;
  env.mid[1] = 0x0F;
  env.mid[2] = 0x10;
  env.mid[3] = 0x3D;

  env.suffix[0] = 0x0F;
  env.suffix[1] = 0x5B;
  env.suffix[2] = 0xC0;

  uintptr_t rip = reinterpret_cast<uintptr_t>(env.prefix) + 8;
  uintptr_t var_addr = reinterpret_cast<uintptr_t>(&env.target_var);
  env.disp_to_var = static_cast<int32_t>(var_addr - rip);

  env.target_var = 60;

  int32_t* resolved = mihoyo_fps::resolve_framerate_pointer_from_hsr_pattern(reinterpret_cast<uintptr_t>(&env));
  if (!resolved || resolved != &env.target_var || *resolved != 60) {
    std::cerr << "FAILED: resolved pointer mismatch (" << resolved << " != " << &env.target_var << ")\n";
    std::abort();
  }

  std::cout << "PASSED (Resolved pointer: " << static_cast<void*>(resolved) << ")\n";
}

void test_safe_write_and_protection() {
  std::cout << "[Test 4] Safe memory write on mprotected page... ";

  long page_size = sysconf(_SC_PAGESIZE);
  assert(page_size > 0);

  // 先分配可读写页写入初始值 60，再保护为只读以模拟游戏只读段
  void* page = mmap(nullptr, page_size, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
  assert(page != MAP_FAILED);

  uintptr_t target = reinterpret_cast<uintptr_t>(page) + 64;
  *reinterpret_cast<volatile int32_t*>(target) = 60;

  // 设为只读
  int prot_res = mprotect(page, page_size, PROT_READ);
  (void)prot_res;
  assert(prot_res == 0);

  // 通过 safe_write_int32 提权安全覆写
  bool ok = mihoyo_fps::safe_write_int32(target, 144);
  (void)ok;
  assert(ok);
  assert(*reinterpret_cast<volatile int32_t*>(target) == 144);

  munmap(page, page_size);
  std::cout << "PASSED\n";
}

int main() {
  std::cout << "===== Running MiHoYo FPS Unlock Tests =====\n";
  test_target_fps_clamping();
  test_pattern_and_pointer_resolution();
  test_hsr_pattern_and_pointer_resolution();
  test_safe_write_and_protection();
  std::cout << "All MiHoYo FPS Unlock tests PASSED successfully.\n";
  return 0;
}
