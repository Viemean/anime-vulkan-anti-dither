#pragma once

#include <cstdint>

namespace zmd_mod {

  /**
   * @brief 判断当前绘制图元是否属于终末地管理员面具组件 (Arknights: Endfield - Administrator Mask)
   * 
   * 数据来源: 3DMigoto / EFMI NoMaskMod
   * - 配件/镜架 (IB Hash: 4cd1ad3b): 高模 indexCount = 117, LOD 远模 indexCount = 69, 51
   * - 远景 LOD (IB Hash: 18220d55): indexCount = 2028
   * - 主面具高模 (IB Hash: 18220d55): indexCount = 4524
   */
  inline bool should_skip_mask_draw(uint32_t index_count) {

    static thread_local uint32_t s_last_index_count = 0;
    uint32_t prev = s_last_index_count;
    s_last_index_count = index_count;

    switch (index_count) {
      case 4524:
        // 陈千语专属特征保护门禁：
        // 陈千语 Submesh 包含: 139392 (身体), 46728 (头发), 9477 (靴子), 15138 (腿带), 888 (饰品), 714 (面部)
        // 仅当紧邻前序属于陈千语专属组件时，判定为陈千语大腿，予以保护放行！
        if (prev == 888 || prev == 714 || prev == 9477 || prev == 15138 || prev == 46728 || prev == 139392) {
          return false;
        }
        // 其余一切 4524（管理员主面具在所有 Pass 下）完全恢复基线行为无条件消除！
        return true;

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
