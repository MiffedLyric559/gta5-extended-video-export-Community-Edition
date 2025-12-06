#pragma once

#include "logger.h"
#include "VoukoderTypeLib_h.h"

#include <nlohmann/json.hpp>
#include <fstream>
#include <string>
#include <cstring>

// JSON preset reader/writer for Voukoder encoder configuration
class JsonPresetReader {
public:
    explicit JsonPresetReader(const std::string& preset_file_path)
        : preset_path_(preset_file_path) {}
    
    // Read encoder configuration from JSON preset file
    VKENCODERCONFIG readEncoderConfig() {
        try {
            std::ifstream ifs(preset_path_);
            
            if (!ifs.is_open()) {
                LOG(LL_WRN, "Failed to open preset file: ", preset_path_);
                return getDefaultEncoderConfig();
            }
            
            nlohmann::json j = nlohmann::json::parse(ifs);
            
            VKENCODERCONFIG config = {};
            
            // Read version
            config.version = j["version"];
            
            // Read format settings
            copyJsonString(j["format"]["container"], config.format.container, sizeof(config.format.container));
            config.format.faststart = j["format"]["faststart"];
            
            // Read video settings
            copyJsonString(j["video"]["encoder"], config.video.encoder, sizeof(config.video.encoder));
            copyJsonString(j["video"]["options"], config.video.options, sizeof(config.video.options));
            copyJsonString(j["video"]["filters"], config.video.filters, sizeof(config.video.filters));
            copyJsonString(j["video"]["sidedata"], config.video.sidedata, sizeof(config.video.sidedata));
            
            // Read audio settings
            copyJsonString(j["audio"]["encoder"], config.audio.encoder, sizeof(config.audio.encoder));
            copyJsonString(j["audio"]["options"], config.audio.options, sizeof(config.audio.options));
            copyJsonString(j["audio"]["filters"], config.audio.filters, sizeof(config.audio.filters));
            copyJsonString(j["audio"]["sidedata"], config.audio.sidedata, sizeof(config.audio.sidedata));
            
            LOG(LL_NFO, "Successfully loaded encoder configuration from: ", preset_path_);
            return config;
            
        } catch (const std::exception& ex) {
            LOG(LL_ERR, "Failed to load preset.json: ", ex.what());
            LOG(LL_ERR, "Loading default values for encoder!");
            return getDefaultEncoderConfig();
        }
    }
    
    // Write encoder configuration to JSON preset file
    bool writeEncoderConfig(const VKENCODERCONFIG& config) {
        try {
            nlohmann::json j;
            
            // Write version
            j["version"] = config.version;
            
            // Write format settings
            j["format"]["container"] = config.format.container;
            j["format"]["faststart"] = config.format.faststart;
            
            // Write video settings
            j["video"]["encoder"] = config.video.encoder;
            j["video"]["options"] = config.video.options;
            j["video"]["filters"] = config.video.filters;
            j["video"]["sidedata"] = config.video.sidedata;
            
            // Write audio settings
            j["audio"]["encoder"] = config.audio.encoder;
            j["audio"]["options"] = config.audio.options;
            j["audio"]["filters"] = config.audio.filters;
            j["audio"]["sidedata"] = config.audio.sidedata;
            
            std::ofstream ofs(preset_path_);
            if (!ofs.is_open()) {
                LOG(LL_ERR, "Failed to open preset file for writing: ", preset_path_);
                return false;
            }
            
            ofs << j;
            ofs.flush();
            
            LOG(LL_NFO, "Successfully wrote encoder configuration to: ", preset_path_);
            return true;
            
        } catch (const std::exception& ex) {
            LOG(LL_ERR, "Failed to write preset.json: ", ex.what());
            return false;
        }
    }
    
    const std::string& getPath() const { return preset_path_; }
    
private:
    // Helper to safely copy JSON string to fixed-size char array
    static void copyJsonString(const nlohmann::json& json_field, char* dest, size_t dest_size) {
        const std::string str = json_field.get<std::string>();
        strncpy_s(dest, dest_size, str.c_str(), _TRUNCATE);
    }
    
    // Get default encoder configuration
    static VKENCODERCONFIG getDefaultEncoderConfig() {
        VKENCODERCONFIG config = {
            .version = 1,
            .video{
                .encoder{"libx264"},
                .options{
                    "_pixelFormat=yuv420p|crf=17.000|opencl=1|preset=medium|rc=crf|"
                    "x264-params=qpmax=22:aq-mode=2:aq-strength=0.700:rc-lookahead=180:"
                    "keyint=480:min-keyint=3:bframes=11:b-adapt=2:ref=3:deblock=0:0:direct="
                    "auto:me=umh:merange=32:subme=10:trellis=2:no-fast-pskip=1"
                },
                .filters{""},
                .sidedata{""}
            },
            .audio{
                .encoder{"aac"},
                .options{"_sampleFormat=fltp|b=320000|profile=aac_main"},
                .filters{""},
                .sidedata{""}
            },
            .format{
                .container{"mp4"},
                .faststart = true
            }
        };
        
        return config;
    }
    
    std::string preset_path_;
};
