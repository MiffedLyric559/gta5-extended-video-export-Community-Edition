#pragma once

#include "ConfigConstants.h"
#include "logger.h"
#include "VoukoderTypeLib_h.h"

#include <string>
#include <utility>

using std::string;
using std::pair;

namespace Config {
    class Manager {
    public:
        static bool is_mod_enabled;
        static bool auto_reload_config;
        static bool export_openexr;
        static bool disable_watermark;
        static string output_dir;
        static LogLevel log_level;
        static pair<uint32_t, uint32_t> fps;
        static uint8_t motion_blur_samples;
        static float motion_blur_strength;
        static VKENCODERCONFIG encoder_config;

        static void reload();
        static void save();
        static void readEncoderConfig();
        static void writeEncoderConfig();
    };
}

