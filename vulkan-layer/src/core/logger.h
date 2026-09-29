#pragma once

#include <cstdint>
#include <cstddef>
#include <string>

namespace game_core {

  void log_msg(const char* fmt, ...);

  void rotate_file_backups(const std::string& base_path, int max_backups = 3);
  void rotate_directory_backups(const std::string& base_dir, int max_backups = 3);

  void dump_shader_bundle(const uint32_t* orig_code, size_t orig_words,
                         const uint32_t* patched_code, size_t patched_words,
                         uint32_t hash, uint32_t dither_nops, uint32_t preserved,
                         bool has_frag_coord, bool has_sample);

  void dump_character_vertex_shader(const uint32_t* vs_code, size_t vs_words,
                                    uint32_t vs_hash, uint32_t fs_hash);

} // namespace game_core
