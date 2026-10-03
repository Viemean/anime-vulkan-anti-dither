#pragma once

#include <cstdint>
#include <cstddef>

namespace zmd_mod {

  // 线程局部滑窗深度：4 个 DrawCall 完美覆盖陈千语自身 Submesh 之间的穿插间距 (<=3)，
  // 同时由于每个渲染 Worker 线程独立维护自身上下文，彻底杜绝全局状态在多线程并发录制时的竞争交错与闪烁问题！
  constexpr size_t K_TL_WINDOW_DEPTH = 4;
  inline thread_local uint32_t tl_history[K_TL_WINDOW_DEPTH] = {0, 0, 0, 0};

  /**
   * @brief 判断指定 indexCount 是否属于陈千语专属 Submesh
   * 包含: 139392 (身体), 46728 (头发), 15138 (腿带), 9477 (靴子), 888 (饰品), 714 (面部)
   */
  inline bool is_chen_submesh(uint32_t count) {
    return count == 139392 || count == 46728 || count == 15138 ||
           count == 9477   || count == 888   || count == 714;
  }

  /**
   * @brief 判断指定 indexCount 是否属于管理员面具镜架/配件组件
   * 包含: 117 (高模镜架), 69 (远模 LOD2), 51 (远模 LOD1)
   */
  inline bool is_mask_frame_submesh(uint32_t count) {
    return count == 117 || count == 69 || count == 51;
  }

  /**
   * @brief 重置线程局部滑窗 (供单元测试与场景重置使用)
   */
  inline void reset_mod_state() {
    for (size_t i = 0; i < K_TL_WINDOW_DEPTH; ++i) {
      tl_history[i] = 0;
    }
  }

  /**
   * @brief 判断当前绘制图元是否属于终末地管理员面具组件 (Arknights: Endfield - Administrator Mask)
   * 
   * 数据来源: 3DMigoto / EFMI NoMaskMod
   * - 配件/镜架 (IB Hash: 4cd1ad3b): 高模 indexCount = 117, LOD 远模 indexCount = 69, 51
   * - 远景 LOD (IB Hash: 18220d55): indexCount = 2028
   * - 主面具高模 (IB Hash: 18220d55): indexCount = 4524
   */
  inline bool should_skip_mask_draw(uint32_t index_count, uint32_t first_index = 0, int32_t vertex_offset = 0) {
    (void)first_index;
    (void)vertex_offset;

    // 获取当前线程最近历史
    uint32_t p0 = tl_history[0];
    uint32_t p1 = tl_history[1];
    uint32_t p2 = tl_history[2];
    uint32_t p3 = tl_history[3];

    // 更新线程局部历史队列
    tl_history[3] = tl_history[2];
    tl_history[2] = tl_history[1];
    tl_history[1] = tl_history[0];
    tl_history[0] = index_count;

    switch (index_count) {
      case 4524: {
        // 陈千语专属特征保护门禁：
        // 陈千语 Submesh 包含: 139392 (身体), 46728 (头发), 9477 (靴子), 15138 (腿带), 888 (饰品), 714 (面部)
        // 只要在当前线程自身最近 4 个 DrawCall 内出现过陈千语专属组件，判定为陈千语大腿，予以保护放行！
        bool is_chen = is_chen_submesh(p0) || is_chen_submesh(p1) ||
                       is_chen_submesh(p2) || is_chen_submesh(p3);

        if (is_chen) {
          return false;
        }

        // 其余一切 4524（管理员主面具在所有 Pass 下）完全恢复基线行为无条件消除！
        return true;
      }

      case 2028: // 主面具 LOD 远模
      case 117:  // 镜架/配件 高模
      case 69:   // 镜架/配件 LOD2
      case 51:   // 镜架/配件 LOD1
        return true;

      default:
        return false;
    }
  }

} // namespace zmd_mod
