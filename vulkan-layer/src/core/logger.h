#pragma once

#include <cstdint>
#include <cstddef>
#include <string>

namespace game_core {

  /**
   * @brief 格式化输出运行时诊断日志至控制台及游戏专有日志文件。
   *
   * @param fmt printf 格式化控制字符串。
   * @param ... 可变参数列表。
   */
  void log_msg(const char* fmt, ...);

  /**
   * @brief 执行日志文件的历史归档轮转。
   *
   * @param base_path 目标日志文件基准绝对路径。
   * @param max_backups 保留的历史备份文件最大数量，默认为 3。
   */
  void rotate_file_backups(const std::string& base_path, int max_backups = 3);

  /**
   * @brief 执行转储目录的历史归档轮转。
   *
   * @param base_dir 目标转储目录基准绝对路径。
   * @param max_backups 保留的历史归档目录最大数量，默认为 3。
   */
  void rotate_directory_backups(const std::string& base_dir, int max_backups = 3);

  /**
   * @brief 将处理前后的 SPIR-V 字节码成对导出至转储目录，包含反编译文本与统计信息。
   *
   * @param orig_code 原始 SPIR-V 字节码指针。
   * @param orig_words 原始字长。
   * @param patched_code 改写后 SPIR-V 字节码指针。
   * @param patched_words 改写后字长。
   * @param hash 着色器原始 32 位哈希值。
   * @param dither_nops 消除网点 Discard 指令计数。
   * @param preserved 保留正常镂空 Discard 指令计数。
   * @param has_frag_coord 是否引用 FragCoord 内建变量。
   * @param has_sample 是否包含纹理采样操作。
   */
  void dump_shader_bundle(const uint32_t* orig_code, size_t orig_words,
                         const uint32_t* patched_code, size_t patched_words,
                         uint32_t hash, uint32_t dither_nops, uint32_t preserved,
                         bool has_frag_coord, bool has_sample);

  /**
   * @brief 导出与网点消隐片段着色器关联的角色顶点着色器二进制数据。
   *
   * @param vs_code 顶点着色器 SPIR-V 字节码指针。
   * @param vs_words 顶点着色器字长。
   * @param vs_hash 顶点着色器哈希值。
   * @param fs_hash 关联的片段着色器哈希值。
   */
  void dump_character_vertex_shader(const uint32_t* vs_code, size_t vs_words,
                                    uint32_t vs_hash, uint32_t fs_hash);

} // namespace game_core
