#include "game_profile.h"
#include "logger.h"

#include "wuwa/wuwa_anti_dither.h"
#include "genshin/genshin_anti_dither.h"
#include "hsr/hsr_anti_dither.h"
#include "zzz/zzz_anti_dither.h"
#include "HI3rd/hi3_anti_dither.h"
#include "nte/nte_anti_dither.h"
#include "azur_promilia/azur_promilia_anti_dither.h"
#include "zmd/zmd_anti_dither.h"
#include "zmd/zmd_mod.h"
#include "gf2/gf2_anti_dither.h"
#include "tof/tof_anti_dither.h"
#include "dna/dna_anti_dither.h"
#include "star/star_anti_dither.h"

#include "../addon/nte/memory_patcher.h"
#include "../addon/fps_unlock/mihoyo/mihoyo_fps_unlock.h"

#include <thread>
#include <chrono>
#include <mutex>
#include <cstring>
#include <array>

namespace game_core {

  namespace {

    // --------------------------------------------------------------------------
    // 各游戏专有关键词表 (进程名匹配)
    // --------------------------------------------------------------------------
    const char* const kKeywordsWuWa[] = {
      "Client-Win64-Shipping", "WutheringWaves", "Wuthering Waves", "Client-Win64", "wuwa", nullptr
    };
    const char* const kKeywordsGenshin[] = {
      "GenshinImpact", "YuanShen", "Genshin Impact", "Genshin", nullptr
    };
    const char* const kKeywordsHSR[] = {
      "StarRail", "Star Rail", nullptr
    };
    const char* const kKeywordsZZZ[] = {
      "ZenlessZoneZero", "Zenless Zone Zero", "zzz", nullptr
    };
    const char* const kKeywordsHI3[] = {
      "BH3", "Honkai Impact 3", "HonkaiImpact3", "HI3", nullptr
    };
    const char* const kKeywordsNTE[] = {
      "HTGame", "HT-Win64", "NevernessToEverness", "HT", nullptr
    };
    const char* const kKeywordsAzurPromilia[] = {
      "AzurPromilia", "Azur Promilia", "azur_promilia", "AP-Win64", "Promilia", nullptr
    };
    const char* const kKeywordsZMD[] = {
      "Endfield", "endfield", "zmd", nullptr
    };
    const char* const kKeywordsGF2[] = {
      "GF2_Exilium", "GF2", "Exilium", nullptr
    };
    const char* const kKeywordsTOF[] = {
      "QRSL", "Hotta", "TOF", "TowerOfFantasy", nullptr
    };
    const char* const kKeywordsDNA[] = {
      "EM-Win64-Shipping", "EM-Win64", "EM", "DuetNightAbyss", "DNA", nullptr
    };
    const char* const kKeywordsStar[] = {
      "Star", "star", "星痕共鸣", nullptr
    };

    // --------------------------------------------------------------------------
    // 各游戏专有生命周期回调与 Mod 过滤函数
    // --------------------------------------------------------------------------
    void on_nte_device_created(VkDevice device) {
      (void)device;
      static std::once_flag s_nte_mem_probe_flag;
      std::call_once(s_nte_mem_probe_flag, []() {
        std::thread([]() {
          game_logger::log_msg("[NTE-Addon] 启动相机防裁剪热补丁服务线程...\n");
          int stable_confirm_count = 0;
          for (int attempt = 1; attempt <= 30; ++attempt) {
            std::this_thread::sleep_for(std::chrono::seconds(2));

            bool ok = nte_mem::apply_anti_hide_camera_patch();
            if (ok) {
              stable_confirm_count++;
              if (stable_confirm_count >= 3) {
                game_logger::log_msg("[NTE-Addon] 相机防裁剪热补丁已确认稳定常驻 (轮次: %d)\n", attempt);
                break;
              }
            } else {
              stable_confirm_count = 0;
            }
          }
        }).detach();
      });
    }

    void on_mihoyo_device_created(VkDevice device) {
      (void)device;
      if (game_logger::is_fps_unlock_enabled()) {
        static std::once_flag s_mihoyo_fps_probe_flag;
        std::call_once(s_mihoyo_fps_probe_flag, []() {
          mihoyo_fps::start_fps_unlock_service();
        });
      }
    }

    bool on_zmd_should_skip_draw_indexed(uint32_t index_count) {
      if (game_logger::is_zmd_nomask_enabled()) {
        return zmd_mod::should_skip_mask_draw(index_count);
      }
      return false;
    }

    // --------------------------------------------------------------------------
    // 静态注册表定义
    // --------------------------------------------------------------------------
    const GameProfile kProfiles[] = {
      {
        GameId::WuWa,
        "Wuthering Waves",
        kKeywordsWuWa,
        wuwa_layer::process_spirv_anti_dither,
        nullptr,
        nullptr,
        nullptr
      },
      {
        GameId::Genshin,
        "Genshin Impact",
        kKeywordsGenshin,
        genshin_layer::process_spirv_anti_dither,
        nullptr,
        on_mihoyo_device_created,
        nullptr
      },
      {
        GameId::HSR,
        "Honkai: Star Rail",
        kKeywordsHSR,
        hsr_layer::process_spirv_anti_dither,
        nullptr,
        on_mihoyo_device_created,
        nullptr
      },
      {
        GameId::ZZZ,
        "Zenless Zone Zero",
        kKeywordsZZZ,
        zzz_layer::process_spirv_anti_dither,
        nullptr,
        nullptr,
        nullptr
      },
      {
        GameId::HI3rd,
        "Honkai Impact 3rd",
        kKeywordsHI3,
        hi3_layer::process_spirv_anti_dither,
        nullptr,
        nullptr,
        nullptr
      },
      {
        GameId::NTE,
        "Neverness To Everness",
        kKeywordsNTE,
        nte_layer::process_spirv_anti_dither,
        nullptr,
        on_nte_device_created,
        nullptr
      },
      {
        GameId::AzurPromilia,
        "Azur Promilia",
        kKeywordsAzurPromilia,
        azur_promilia_layer::process_spirv_anti_dither,
        nullptr,
        nullptr,
        nullptr
      },
      {
        GameId::ZMD,
        "Arknights: Endfield",
        kKeywordsZMD,
        zmd_layer::process_spirv_anti_dither,
        on_zmd_should_skip_draw_indexed,
        nullptr,
        nullptr
      },
      {
        GameId::GF2,
        "Girls' Frontline 2: Exilium",
        kKeywordsGF2,
        gf2_layer::process_spirv_anti_dither,
        nullptr,
        nullptr,
        nullptr
      },
      {
        GameId::TOF,
        "Tower of Fantasy",
        kKeywordsTOF,
        tof_layer::process_spirv_anti_dither,
        nullptr,
        nullptr,
        nullptr
      },
      {
        GameId::DNA,
        "Duet Night Abyss",
        kKeywordsDNA,
        dna_layer::process_spirv_anti_dither,
        nullptr,
        nullptr,
        nullptr
      },
      {
        GameId::Star,
        "Star",
        kKeywordsStar,
        star_layer::process_spirv_anti_dither,
        nullptr,
        nullptr,
        nullptr
      }
    };

    const GameProfile* s_active_profile = nullptr;
    GameId s_active_id = GameId::Unknown;
    std::once_flag s_init_flag;

  } // namespace

  void init_game_profiles(std::string_view process_name) {
    std::call_once(s_init_flag, [&]() {
      if (process_name.empty()) {
        return;
      }

      for (const auto& profile : kProfiles) {
        if (!profile.process_keywords)
          continue;

        for (size_t i = 0; profile.process_keywords[i] != nullptr; ++i) {
          if (process_name.find(profile.process_keywords[i]) != std::string_view::npos) {
            s_active_profile = &profile;
            s_active_id = profile.id;

            // 预先绑定高频指令过滤指针 (消除运行期双重条件判断)
            if (profile.should_skip_draw_indexed) {
              g_active_skip_draw_indexed = profile.should_skip_draw_indexed;
            }
            return;
          }
        }
      }
    });
  }

  const GameProfile* get_active_profile() {
    return s_active_profile;
  }

  GameId get_active_game_id() {
    return s_active_id;
  }

  bool is_game_active() {
    return s_active_profile != nullptr;
  }

} // namespace game_core
