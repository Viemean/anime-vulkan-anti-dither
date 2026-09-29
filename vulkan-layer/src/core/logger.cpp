#include "logger.h"
#include "config.h"

#include <cstdio>
#include <cstdarg>
#include <ctime>
#include <string>
#include <filesystem>
#include <mutex>

namespace game_core {

  namespace {

    FILE* g_log_file = nullptr;
    bool g_log_file_initialized = false;

    bool g_dump_dir_initialized = false;
    std::string g_dump_dir_path;

    struct LogFileGuard {
      ~LogFileGuard() {
        if (g_log_file) {
          std::fclose(g_log_file);
          g_log_file = nullptr;
        }
      }
    };
    LogFileGuard s_log_guard;

    void ensure_log_file_open() {
      if (g_log_file_initialized)
        return;
      g_log_file_initialized = true;

      std::string proc = get_clean_process_name();
      std::string log_dir = "/tmp/game_anti_dither/logs";
      std::error_code ec;
      std::filesystem::create_directories(log_dir, ec);

      std::string base_log = log_dir + "/" + proc + ".log";
      rotate_file_backups(base_log, 3);

      g_log_file = std::fopen(base_log.c_str(), "w");
    }

    const std::string& ensure_dump_dir_ready() {
      if (g_dump_dir_initialized)
        return g_dump_dir_path;
      g_dump_dir_initialized = true;

      std::string proc = get_clean_process_name();
      g_dump_dir_path = "/tmp/game_anti_dither/dumps/" + proc;

      rotate_directory_backups(g_dump_dir_path, 3);
      std::error_code ec;
      std::filesystem::create_directories(g_dump_dir_path, ec);

      return g_dump_dir_path;
    }

  } // namespace

  void rotate_file_backups(const std::string& base_path, int max_backups) {
    std::error_code ec;
    std::string oldest = base_path + "." + std::to_string(max_backups);
    std::filesystem::remove(oldest, ec);

    for (int i = max_backups - 1; i >= 1; --i) {
      std::string from = base_path + "." + std::to_string(i);
      std::string to = base_path + "." + std::to_string(i + 1);
      if (std::filesystem::exists(from, ec)) {
        std::filesystem::rename(from, to, ec);
      }
    }

    if (std::filesystem::exists(base_path, ec)) {
      std::filesystem::rename(base_path, base_path + ".1", ec);
    }
  }

  void rotate_directory_backups(const std::string& base_dir, int max_backups) {
    std::error_code ec;
    std::string oldest = base_dir + "." + std::to_string(max_backups);
    std::filesystem::remove_all(oldest, ec);

    for (int i = max_backups - 1; i >= 1; --i) {
      std::string from = base_dir + "." + std::to_string(i);
      std::string to = base_dir + "." + std::to_string(i + 1);
      if (std::filesystem::exists(from, ec)) {
        std::filesystem::rename(from, to, ec);
      }
    }

    if (std::filesystem::exists(base_dir, ec)) {
      std::filesystem::rename(base_dir, base_dir + ".1", ec);
    }
  }

  void log_msg(const char* fmt, ...) {
#ifdef DISABLE_LOGGING
    (void)fmt;
    return;
#else
    if (!is_log_enabled()) {
      return;
    }

    std::lock_guard<std::recursive_mutex> lock(get_logger_mutex());
    ensure_log_file_open();
    if (!g_log_file) {
      return;
    }

    char time_buf[64];
    std::time_t t = std::time(nullptr);
    std::strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", std::localtime(&t));

    std::fprintf(g_log_file, "[%s] ", time_buf);
    va_list args;
    va_start(args, fmt);
    std::vfprintf(g_log_file, fmt, args);
    va_end(args);
    std::fflush(g_log_file);
#endif
  }

  void dump_shader_bundle(const uint32_t* orig_code, size_t orig_words,
                         const uint32_t* patched_code, size_t patched_words,
                         uint32_t hash, uint32_t dither_nops, uint32_t preserved,
                         bool has_frag_coord, bool has_sample) {
#ifdef DISABLE_LOGGING
    (void)orig_code; (void)orig_words; (void)patched_code; (void)patched_words;
    (void)hash; (void)dither_nops; (void)preserved; (void)has_frag_coord; (void)has_sample;
    return;
#else
    init_config();
    if (!is_dump_enabled())
      return;

    std::lock_guard<std::recursive_mutex> lock(get_logger_mutex());
    const std::string& dir = ensure_dump_dir_ready();

    char path_orig[512];
    char path_patched[512];
    char path_report[512];

    std::snprintf(path_orig, sizeof(path_orig), "%s/shader_%08x_orig.spv", dir.c_str(), hash);
    std::snprintf(path_patched, sizeof(path_patched), "%s/shader_%08x_patched.spv", dir.c_str(), hash);
    std::snprintf(path_report, sizeof(path_report), "%s/shader_%08x_report.txt", dir.c_str(), hash);

    FILE* f_orig = std::fopen(path_orig, "wb");
    if (f_orig) {
      std::fwrite(orig_code, sizeof(uint32_t), orig_words, f_orig);
      std::fclose(f_orig);
    }

    FILE* f_patched = std::fopen(path_patched, "wb");
    if (f_patched) {
      std::fwrite(patched_code, sizeof(uint32_t), patched_words, f_patched);
      std::fclose(f_patched);
    }

    FILE* f_rep = std::fopen(path_report, "w");
    if (f_rep) {
      std::fprintf(f_rep, "Shader Hash: 0x%08x\n", hash);
      std::fprintf(f_rep, "Process: %s\n", g_detected_process.c_str());
      std::fprintf(f_rep, "Words: %zu\n", orig_words);
      std::fprintf(f_rep, "Dither NOPed: %u\n", dither_nops);
      std::fprintf(f_rep, "Cutout Preserved: %u\n", preserved);
      std::fprintf(f_rep, "Has FragCoord: %s\n", has_frag_coord ? "true" : "false");
      std::fprintf(f_rep, "Has Image Sample: %s\n", has_sample ? "true" : "false");
      std::fclose(f_rep);
    }
#endif
  }

  void dump_character_vertex_shader(const uint32_t* vs_code, size_t vs_words,
                                    uint32_t vs_hash, uint32_t fs_hash) {
#ifdef DISABLE_LOGGING
    (void)vs_code; (void)vs_words; (void)vs_hash; (void)fs_hash;
    return;
#else
    init_config();
    if (!is_dump_enabled())
      return;

    std::lock_guard<std::recursive_mutex> lock(get_logger_mutex());
    const std::string& dir = ensure_dump_dir_ready();

    char path_vs[512];
    char path_report[512];

    std::snprintf(path_vs, sizeof(path_vs), "%s/shader_vs_%08x_fs_%08x.spv", dir.c_str(), vs_hash, fs_hash);
    std::snprintf(path_report, sizeof(path_report), "%s/shader_vs_%08x_fs_%08x_report.txt", dir.c_str(), vs_hash, fs_hash);

    FILE* f_vs = std::fopen(path_vs, "wb");
    if (f_vs) {
      std::fwrite(vs_code, sizeof(uint32_t), vs_words, f_vs);
      std::fclose(f_vs);
    }

    FILE* f_rep = std::fopen(path_report, "w");
    if (f_rep) {
      std::fprintf(f_rep, "Vertex Shader Hash: 0x%08x\n", vs_hash);
      std::fprintf(f_rep, "Paired Fragment Shader Hash: 0x%08x\n", fs_hash);
      std::fprintf(f_rep, "Process: %s\n", g_detected_process.c_str());
      std::fprintf(f_rep, "Words: %zu\n", vs_words);
      std::fclose(f_rep);
    }
#endif
  }

} // namespace game_core
