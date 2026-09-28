#pragma once

#include "dna_vkd3d.h"

namespace dna_layer {

  inline void process_spirv_anti_dither(uint32_t* spirv_code, size_t word_count) {
    if (!spirv_code || word_count < 5)
      return;

    // 当前聚焦 VKD3D 后端
    dna_vkd3d::process_spirv(spirv_code, word_count);
  }

} // namespace dna_layer
