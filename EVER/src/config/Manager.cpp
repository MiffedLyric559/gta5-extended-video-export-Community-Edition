#include "stdafx.h"
#include "Manager.h"
#include "util.h"
#include "IniConfigReader.h"
#include "JsonPresetReader.h"

#include <fstream>
#include <cmath>

using std::string;
using std::ofstream;
using std::ios;

namespace Config {
    bool Manager::is_mod_enabled;
    bool Manager::auto_reload_config;
    string Manager::output_dir;
    LogLevel Manager::log_level;
    pair<uint32_t, uint32_t> Manager::fps;
    uint8_t Manager::motion_blur_samples;
    float Manager::motion_blur_strength;
    bool Manager::export_openexr;
    bool Manager::disable_watermark;
    FFmpeg::FFENCODERCONFIG Manager::encoder_config;

    static string logLevelToString(LogLevel level) {
        switch (level) {
            case LL_ERR: return "error";
            case LL_WRN: return "warn";
            case LL_NFO: return "info";
            case LL_DBG: return "debug";
            case LL_TRC: return "trace";
            default: return "error";
        }
    }

    void Manager::reload() {
        const string ini_path = AsiPath() + "\\" INI_FILE_NAME;
        IniConfigReader reader(ini_path);
        
        is_mod_enabled = reader.readBool("", CFG_ENABLE_XVX, true);
        auto_reload_config = reader.readBool("", CFG_AUTO_RELOAD_CONFIG, true);
        output_dir = reader.getOutputDirectory("", CFG_OUTPUT_DIR);
        log_level = reader.readLogLevel("", CFG_LOG_LEVEL, LL_ERR);
        fps = reader.readFraction(CFG_EXPORT_SECTION, CFG_EXPORT_FPS, {30000, 1001});
        motion_blur_samples = reader.readInt<uint8_t>(CFG_EXPORT_SECTION, CFG_EXPORT_MB_SAMPLES, 0, 0, 255);
        motion_blur_strength = reader.readFloat(CFG_EXPORT_SECTION, CFG_EXPORT_MB_STRENGTH, 0.5f, 0.0f, 1.0f);
        export_openexr = reader.readBool(CFG_EXPORT_SECTION, CFG_EXPORT_OPENEXR, false);
        disable_watermark = reader.readBool(CFG_EXPORT_SECTION, CFG_DISABLE_WATERMARK, false);
        
        readEncoderConfig();
    }

    void Manager::save() {
        try {
            const string path = AsiPath() + "\\" INI_FILE_NAME;
            ofstream file(path, ios::trunc);
            
            if (!file) {
                LOG(LL_ERR, "Failed to write INI: ", path);
                return;
            }
            
            file << "enable_mod = " << (is_mod_enabled ? "true" : "false") << "\n"
                << "auto_reload_config = " << (auto_reload_config ? "true" : "false") << "\n"
                << "output_folder = " << output_dir << "\n"
                << "log_level = " << logLevelToString(log_level) << "\n"
                << "\n[EXPORT]\n";
            
            // Write FPS as integer if close to whole number, otherwise as fraction
            float fps_value = static_cast<float>(fps.first) / static_cast<float>(fps.second);
            int fps_int = static_cast<int>(fps_value + 0.5f);
            
            if (std::abs(fps_value - fps_int) < 0.01f) {
                file << "fps = " << fps_int << "\n";
            } else {
                file << "fps = " << fps.first << "/" << fps.second << "\n";
            }
            
            file << "motion_blur_samples = " << static_cast<int>(motion_blur_samples) << "\n"
                << "motion_blur_strength = " << motion_blur_strength << "\n"
                << "export_openexr = " << (export_openexr ? "true" : "false") << "\n"
                << "disable_watermark = " << (disable_watermark ? "true" : "false") << "\n";
            
            LOG(LL_NFO, "Saved configuration");
        } catch (const std::exception& ex) {
            LOG(LL_ERR, "Failed to save config: ", ex.what());
        }
    }

    void Manager::readEncoderConfig() {
        JsonPresetReader reader(AsiPath() + "\\" PRESET_FILE_NAME);
        encoder_config = reader.readEncoderConfig();
    }

    void Manager::writeEncoderConfig() {
        JsonPresetReader reader(AsiPath() + "\\" PRESET_FILE_NAME);
        reader.writeEncoderConfig(encoder_config);
    }
}

