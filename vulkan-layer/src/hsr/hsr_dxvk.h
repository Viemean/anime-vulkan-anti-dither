#pragma once

#include "../mihoyo/mihoyo_dxvk.h"

namespace hsr_dxvk {

  using namespace mihoyo_dxvk;

  /**
   * @brief 崩坏：星穹铁道片段着色器反虚化分发
   */
  inline void process_spirv_anti_dither(uint32_t* spirv_code, size_t word_count) {
    mihoyo_dxvk::process_spirv_anti_dither(spirv_code, word_count, "HSR");
  }

  /**
   * @brief 崩坏：星穹铁道顶点着色器几何坍缩消除分发
   */
  inline void process_vertex_shader(uint32_t* spirv_code, size_t word_count) {
    mihoyo_dxvk::process_vertex_shader(spirv_code, word_count, "HSR");
  }

} // namespace hsr_dxvk
