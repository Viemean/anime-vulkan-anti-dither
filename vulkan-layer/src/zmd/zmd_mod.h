#pragma once

#include <cstdint>

namespace zmd_mod {

  /**
   * @brief 判断当前绘制图元是否属于终末地管理员面具组件 (Arknights: Endfield - Administrator Mask)
   * 
   * 数据来源: 3DMigoto / EFMI NoMaskMod
   * - 主面具 (IB Hash: 18220d55): 高模 indexCount = 4524, LOD 远模 indexCount = 2028
   * - 面具零件/镜架 (IB Hash: 4cd1ad3b): 高模 indexCount = 117, LOD 远模 indexCount = 69, 51
   */
  inline bool should_skip_mask_draw(uint32_t index_count) {
    switch (index_count) {
      case 4524: // 主面具 高模
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
