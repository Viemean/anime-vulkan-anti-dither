#pragma once

#include "../mihoyo/mihoyo_dxvk.h"

namespace hi3_dxvk {

  using namespace mihoyo_dxvk;

  /**
   * @brief 《崩坏3》（Honkai Impact 3rd）片段着色器反虚化处理
   */
  inline void process_spirv_anti_dither(uint32_t* spirv_code, size_t word_count) {
    mihoyo_dxvk::process_spirv_anti_dither(spirv_code, word_count, "HI3RD");
  }

  /**
   * @brief 《崩坏3》顶点着色器几何裁剪坍缩消除
   */
  inline void process_vertex_shader(uint32_t* spirv_code, size_t word_count) {
    mihoyo_dxvk::process_vertex_shader(spirv_code, word_count, "HI3RD");
  }

} // namespace hi3_dxvk
