#pragma once

#include "../mihoyo/mihoyo_dxvk.h"

namespace genshin_dxvk {

  using namespace mihoyo_dxvk;

  /**
   * @brief 原神片段着色器反虚化分发
   */
  inline void process_spirv_anti_dither(uint32_t* spirv_code, size_t word_count) {
    mihoyo_dxvk::process_spirv_anti_dither(spirv_code, word_count, "GENSHIN");
  }

  /**
   * @brief 原神顶点着色器几何坍缩消除分发
   */
  inline void process_vertex_shader(uint32_t* spirv_code, size_t word_count) {
    mihoyo_dxvk::process_vertex_shader(spirv_code, word_count, "GENSHIN");
  }

} // namespace genshin_dxvk
