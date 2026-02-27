#pragma once

#include "logger.h"
#include "FFmpegTypes.h"

#include <nlohmann/json.hpp>
#include <fstream>
#include <string>
#include <sstream>
#include <cstring>
#include <vector>

class JsonPresetReader {
public:
    explicit JsonPresetReader(const std::string& preset_file_path)
        : preset_path_(preset_file_path) {}
    
    FFmpeg::FFENCODERCONFIG readEncoderConfig() {
        try {
            std::ifstream ifs(preset_path_);
            
            if (!ifs.is_open()) {
                LOG(LL_WRN, "Failed to open preset file: ", preset_path_);
                return getDefaultEncoderConfig();
            }
            
            nlohmann::json j = nlohmann::json::parse(ifs);
            
            FFmpeg::FFENCODERCONFIG config = {};
            config.version = 2;
            
            if (j.contains("format")) {
                auto& fmt = j["format"];
                copyJsonString(fmt.value("container", "mp4"), config.format.container, sizeof(config.format.container));
                config.format.faststart = fmt.value("faststart", false);
            }
            
            if (j.contains("video")) {
                auto& vid = j["video"];
                copyJsonString(vid.value("codec", "libx264"), config.video.encoder, sizeof(config.video.encoder));
                
                std::string videoOptions = buildVideoOptionsString(vid);
                copyJsonString(videoOptions, config.video.options, sizeof(config.video.options));
                
                std::string videoFilters = buildVideoFiltersString(j);
                copyJsonString(videoFilters, config.video.filters, sizeof(config.video.filters));
                
                copyJsonString(std::string(""), config.video.sidedata, sizeof(config.video.sidedata));
            }
            
            if (j.contains("audio")) {
                auto& aud = j["audio"];
                copyJsonString(aud.value("codec", "aac"), config.audio.encoder, sizeof(config.audio.encoder));
                
                std::string audioOptions = buildAudioOptionsString(aud);
                copyJsonString(audioOptions, config.audio.options, sizeof(config.audio.options));
                
                std::string audioFilters = buildAudioFiltersString(j);
                copyJsonString(audioFilters, config.audio.filters, sizeof(config.audio.filters));
                
                copyJsonString(std::string(""), config.audio.sidedata, sizeof(config.audio.sidedata));
            }
            
            LOG(LL_NFO, "Successfully loaded encoder configuration from: ", preset_path_);
            LOG(LL_DBG, "Video encoder: ", config.video.encoder);
            LOG(LL_DBG, "Video options: ", config.video.options);
            LOG(LL_DBG, "Video filters: ", config.video.filters);
            LOG(LL_DBG, "Audio encoder: ", config.audio.encoder);
            LOG(LL_DBG, "Audio options: ", config.audio.options);
            LOG(LL_DBG, "Audio filters: ", config.audio.filters);
            
            return config;
            
        } catch (const std::exception& ex) {
            LOG(LL_ERR, "Failed to load preset.json: ", ex.what());
            LOG(LL_ERR, "Loading default values for encoder!");
            return getDefaultEncoderConfig();
        }
    }
    
    bool writeEncoderConfig(const FFmpeg::FFENCODERCONFIG& config) {
        try {
            nlohmann::json j;
            
            j["version"] = config.version;
            
            j["format"]["container"] = config.format.container;
            j["format"]["faststart"] = config.format.faststart;
            
            j["video"]["encoder"] = config.video.encoder;
            j["video"]["options"] = config.video.options;
            j["video"]["filters"] = config.video.filters;
            j["video"]["sidedata"] = config.video.sidedata;
            
            j["audio"]["encoder"] = config.audio.encoder;
            j["audio"]["options"] = config.audio.options;
            j["audio"]["filters"] = config.audio.filters;
            j["audio"]["sidedata"] = config.audio.sidedata;
            
            std::ofstream ofs(preset_path_);
            if (!ofs.is_open()) {
                LOG(LL_ERR, "Failed to open preset file for writing: ", preset_path_);
                return false;
            }
            
            ofs << j.dump(2);
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
    static void copyJsonString(const std::string& str, char* dest, size_t dest_size) {
        strncpy_s(dest, dest_size, str.c_str(), _TRUNCATE);
    }
    
    static void copyJsonString(const nlohmann::json& json_field, char* dest, size_t dest_size) {
        const std::string str = json_field.get<std::string>();
        strncpy_s(dest, dest_size, str.c_str(), _TRUNCATE);
    }
    
    static std::string buildVideoOptionsString(const nlohmann::json& video) {
        std::ostringstream oss;
        std::vector<std::string> options;
        std::vector<std::string> x265_params; // For x265-specific params
        
        // Check if this is x265 codec
        bool is_x265 = false;
        if (video.contains("codec")) {
            std::string codec = video["codec"].get<std::string>();
            is_x265 = (codec == "libx265" || codec == "hevc");
        }
        
        if (video.contains("pixel_format") && video["pixel_format"] != "auto") {
            options.push_back("_pixelFormat=" + video["pixel_format"].get<std::string>());
        }
        
        if (video.contains("crf")) {
            if (video["crf"].is_string()) {
                std::string crf_str = video["crf"].get<std::string>();
                if (crf_str != "auto" && crf_str != "none") {
                    options.push_back("crf=" + crf_str);
                }
            } else if (video["crf"].is_number_integer()) {
                options.push_back("crf=" + std::to_string(video["crf"].get<int>()));
            } else if (video["crf"].is_number()) {
                options.push_back("crf=" + std::to_string(video["crf"].get<double>()));
            }
        }
        
        if (video.contains("bitrate") && video["bitrate"] != "auto") {
            if (video["bitrate"].is_string()) {
                std::string br = video["bitrate"].get<std::string>();
                if (!br.empty()) {
                    char last_char = br.back();
                    if (last_char == 'k' || last_char == 'K' || last_char == 'm' || last_char == 'M') {
                        options.push_back("b=" + br);
                    } else {
                        options.push_back("b=" + br + "k");
                    }
                }
            } else if (video["bitrate"].is_number()) {
                options.push_back("b=" + std::to_string(video["bitrate"].get<int>()) + "k");
            }
        }
        
        if (video.contains("minrate") && video["minrate"] != "auto") {
            if (video["minrate"].is_string()) {
                std::string mr = video["minrate"].get<std::string>();
                if (!mr.empty() && mr.find_first_of("kKmM") == std::string::npos) {
                    options.push_back("rc_min_rate=" + mr + "k");
                } else {
                    options.push_back("rc_min_rate=" + mr);
                }
            } else if (video["minrate"].is_number()) {
                options.push_back("rc_min_rate=" + std::to_string(video["minrate"].get<int>()) + "k");
            }
        }
        if (video.contains("maxrate") && video["maxrate"] != "auto") {
            if (video["maxrate"].is_string()) {
                std::string mr = video["maxrate"].get<std::string>();
                if (!mr.empty() && mr.find_first_of("kKmM") == std::string::npos) {
                    options.push_back("rc_max_rate=" + mr + "k");
                } else {
                    options.push_back("rc_max_rate=" + mr);
                }
            } else if (video["maxrate"].is_number()) {
                options.push_back("rc_max_rate=" + std::to_string(video["maxrate"].get<int>()) + "k");
            }
        }
        
        if (video.contains("bufsize") && video["bufsize"] != "auto") {
            if (video["bufsize"].is_string()) {
                std::string bs = video["bufsize"].get<std::string>();
                if (!bs.empty() && bs.find('k') == std::string::npos && bs.find('M') == std::string::npos) {
                    options.push_back("bufsize=" + bs + "k");
                } else {
                    options.push_back("bufsize=" + bs);
                }
            } else if (video["bufsize"].is_number()) {
                options.push_back("bufsize=" + std::to_string(video["bufsize"].get<int>()) + "k");
            }
        }
        
        if (video.contains("preset") && video["preset"] != "none" && video["preset"] != "auto") {
            options.push_back("preset=" + video["preset"].get<std::string>());
        }
        
        if (video.contains("tune") && video["tune"] != "none" && video["tune"] != "auto") {
            options.push_back("tune=" + video["tune"].get<std::string>());
        }
        
        if (video.contains("profile")) {
            bool should_add = false;
            std::string profile_value;
            
            if (video["profile"].is_string()) {
                profile_value = video["profile"].get<std::string>();
                if (profile_value != "none" && profile_value != "auto") {
                    should_add = true;
                }
            } else if (video["profile"].is_number()) {
                int prof_num = video["profile"].get<int>();
                if (prof_num >= 0) {
                    profile_value = std::to_string(prof_num);
                    should_add = true;
                }
            }
            
            if (should_add && !profile_value.empty()) {
                options.push_back("profile=" + profile_value);
            }
        }
        
        // For x265, level goes into x265-params, for others use direct option
        if (video.contains("level") && video["level"] != "none" && video["level"] != "auto") {
            std::string level_value;
            if (video["level"].is_string()) {
                level_value = video["level"].get<std::string>();
                // Convert "5.1" format to "5.1" for x265-params (x265 accepts both)
                size_t dot_pos = level_value.find('.');
                if (is_x265 && dot_pos != std::string::npos) {
                    // x265-params accepts "5.1" format
                    x265_params.push_back("level=" + level_value);
                } else {
                    options.push_back("level=" + level_value);
                }
            } else if (video["level"].is_number()) {
                double level_num = video["level"].get<double>();
                if (is_x265) {
                    x265_params.push_back("level=" + std::to_string(level_num));
                } else {
                    options.push_back("level=" + std::to_string(level_num));
                }
            }
        }
        
        // For x265, keyint goes into x265-params, for others use g=
        if (video.contains("gopsize") && video["gopsize"] != "auto") {
            std::string gop_value;
            if (video["gopsize"].is_string()) {
                gop_value = video["gopsize"].get<std::string>();
            } else if (video["gopsize"].is_number()) {
                gop_value = std::to_string(video["gopsize"].get<int>());
            }
            
            if (!gop_value.empty()) {
                if (is_x265) {
                    x265_params.push_back("keyint=" + gop_value);
                } else {
                    options.push_back("g=" + gop_value);
                }
            }
        }
        
        if (video.contains("frame_rate") && video["frame_rate"] != "auto") {
            if (video["frame_rate"].is_string()) {
                std::string fr = video["frame_rate"].get<std::string>();
                if (!fr.empty() && fr != "auto") {
                    options.push_back("r=" + fr);
                }
            } else if (video["frame_rate"].is_number()) {
                options.push_back("r=" + std::to_string(video["frame_rate"].get<int>()));
            }
        }
        
        if (video.contains("speed") && video["speed"] != "auto" && video["speed"] != "none") {
            if (video["speed"].is_string()) {
                options.push_back("speed=" + video["speed"].get<std::string>());
            } else {
                options.push_back("speed=" + std::to_string(video["speed"].get<int>()));
            }
        }
        
        if (video.contains("pass") && video["pass"] != "auto") {
            if (video["pass"].is_string()) {
                std::string pass_str = video["pass"].get<std::string>();
                if (pass_str != "1") {
                    options.push_back("pass=" + pass_str);
                }
            }
        }
        
        if (video.contains("aspect") && video["aspect"] != "auto") {
            options.push_back("aspect=" + video["aspect"].get<std::string>());
        }
        
        if (video.contains("codec_options") && !video["codec_options"].get<std::string>().empty()) {
            std::string codec_opts = video["codec_options"].get<std::string>();
            if (!codec_opts.empty() && codec_opts[0] == '-') {
                codec_opts = codec_opts.substr(1);
            }

            size_t start = codec_opts.find_first_not_of(" \t");
            if (start != std::string::npos) {
                codec_opts = codec_opts.substr(start);
                if (codec_opts.find('=') != std::string::npos) {
                    options.push_back(codec_opts);
                }
            }
        }

        if (video.contains("scaling") && video["scaling"] != "auto" && video["scaling"] != "none") {
            if (video["scaling"].is_string()) {
                options.push_back("_scaling=" + video["scaling"].get<std::string>());
            }
        }
        
        for (size_t i = 0; i < options.size(); ++i) {
            if (i > 0) oss << "|";
            oss << options[i];
        }
        
        // Add x265-params if any x265-specific options were collected
        if (!x265_params.empty()) {
            if (!options.empty()) oss << "|";
            oss << "x265-params=";
            for (size_t i = 0; i < x265_params.size(); ++i) {
                if (i > 0) oss << ":";
                oss << x265_params[i];
            }
        }
        
        return oss.str();
    }
    
    static std::string buildAudioOptionsString(const nlohmann::json& audio) {
        std::ostringstream oss;
        std::vector<std::string> options;
        
        std::string codec = audio.value("codec", "");
        bool is_pcm = (codec.find("pcm_") == 0);
        bool is_copy = (codec == "copy");
        
        if (!is_pcm && !is_copy && audio.contains("quality") && audio["quality"] != "auto") {
            if (audio["quality"].is_string()) {
                std::string qual = audio["quality"].get<std::string>();
                if (!qual.empty()) {
                    if (qual.find('k') != std::string::npos || qual.find('K') != std::string::npos) {
                        options.push_back("b=" + qual);
                    } else {
                        options.push_back("b=" + qual + "k");
                    }
                }
            } else if (audio["quality"].is_number()) {
                options.push_back("b=" + std::to_string(audio["quality"].get<int>()) + "k");
            }
        }
        
        if (!is_copy && audio.contains("sampleRate") && audio["sampleRate"] != "auto") {
            if (audio["sampleRate"].is_string()) {
                std::string sr = audio["sampleRate"].get<std::string>();
                if (!sr.empty() && sr != "auto") {
                    options.push_back("ar=" + sr);
                }
            } else if (audio["sampleRate"].is_number()) {
                options.push_back("ar=" + std::to_string(audio["sampleRate"].get<int>()));
            }
        }
        
        if (!is_copy && audio.contains("channel") && audio["channel"] != "source" && audio["channel"] != "auto") {
            if (audio["channel"].is_string()) {
                std::string ch = audio["channel"].get<std::string>();
                if (ch == "mono") options.push_back("ac=1");
                else if (ch == "stereo") options.push_back("ac=2");
                else if (ch == "5.1") options.push_back("ac=6");
                else options.push_back("ac=" + ch);
            } else {
                options.push_back("ac=" + std::to_string(audio["channel"].get<int>()));
            }
        }
        
        if (codec == "aac" || codec == "libfdk_aac") {
            options.push_back("profile=aac_low");
            options.push_back("_sampleFormat=fltp");
        } else if (is_copy) {
            // For copy codec, no additional options needed
            // The copy codec streams directly without re-encoding
        } else if (is_pcm) {
            // PCM codecs need the sample format to match the codec
            // Extract format from codec name (e.g., "pcm_s16le" -> "s16")
            if (codec == "pcm_s16le" || codec == "pcm_s16be") {
                options.push_back("_sampleFormat=s16");
            } else if (codec == "pcm_s24le" || codec == "pcm_s24be") {
                options.push_back("_sampleFormat=s32");  // s24 is stored as s32 in FFmpeg
            } else if (codec == "pcm_s32le" || codec == "pcm_s32be") {
                options.push_back("_sampleFormat=s32");
            } else if (codec == "pcm_f32le" || codec == "pcm_f32be") {
                options.push_back("_sampleFormat=flt");
            } else if (codec == "pcm_f64le" || codec == "pcm_f64be") {
                options.push_back("_sampleFormat=dbl");
            }
        }
        
        for (size_t i = 0; i < options.size(); ++i) {
            if (i > 0) oss << "|";
            oss << options[i];
        }
        
        return oss.str();
    }
    
    static std::string buildVideoFiltersString(const nlohmann::json& root) {
        if (!root.contains("filter")) {
            return "";
        }
        
        auto& filter = root["filter"];
        std::vector<std::string> filters;
        
        if (filter.contains("scaling") && filter["scaling"] != "auto" && filter["scaling"] != "none") {
            std::string scale_algo = filter["scaling"].get<std::string>();
        }
        
        if (root.contains("video")) {
            auto& video = root["video"];
            bool needs_scale = false;
            std::string width, height;
            
            if (video.contains("width") && video["width"] != "auto") {
                if (video["width"].is_string()) {
                    width = video["width"].get<std::string>();
                } else {
                    width = std::to_string(video["width"].get<int>());
                }
                needs_scale = true;
            }
            
            if (video.contains("height") && video["height"] != "auto") {
                if (video["height"].is_string()) {
                    height = video["height"].get<std::string>();
                } else {
                    height = std::to_string(video["height"].get<int>());
                }
                needs_scale = true;
            }
            
            if (needs_scale && !width.empty() && !height.empty()) {
                std::string scale_filter = "scale=" + width + ":" + height;
                if (filter.contains("scaling") && filter["scaling"] != "auto") {
                    scale_filter += ":flags=" + filter["scaling"].get<std::string>();
                }
                filters.push_back(scale_filter);
            }
        }
        
        if (filter.contains("deinterlace") && filter["deinterlace"] != "none") {
            std::string deint = filter["deinterlace"].get<std::string>();
            if (deint == "field" || deint == "frame") {
                filters.push_back("yadif=mode=" + deint);
            } else if (deint == "bob") {
                filters.push_back("yadif=mode=1");
            }
        }
        
        if (filter.contains("denoise") && filter["denoise"] != "none") {
            std::string denoise = filter["denoise"].get<std::string>();
            if (denoise == "light") {
                filters.push_back("hqdn3d=1.5:1.5:6:6");
            } else if (denoise == "medium") {
                filters.push_back("hqdn3d=3:3:6:6");
            } else if (denoise == "heavy") {
                filters.push_back("hqdn3d=6:6:12:12");
            }
        }
        
        if (filter.contains("deband") && filter["deband"].get<bool>()) {
            filters.push_back("deband");
        }
        
        if (filter.contains("deshake") && filter["deshake"].get<bool>()) {
            filters.push_back("deshake");
        }
        
        if (filter.contains("deflicker") && filter["deflicker"].get<bool>()) {
            filters.push_back("deflicker");
        }
        
        if (filter.contains("dejudder") && filter["dejudder"].get<bool>()) {
            filters.push_back("dejudder");
        }

        std::vector<std::string> eq_params;
        
        if (filter.contains("brightness") && filter["brightness"] != "0") {
            if (filter["brightness"].is_string()) {
                eq_params.push_back("brightness=" + filter["brightness"].get<std::string>());
            } else {
                double val = filter["brightness"].get<double>();
                if (val != 0.0) {
                    eq_params.push_back("brightness=" + std::to_string(val));
                }
            }
        }
        
        if (filter.contains("contrast") && filter["contrast"] != "1") {
            if (filter["contrast"].is_string()) {
                eq_params.push_back("contrast=" + filter["contrast"].get<std::string>());
            } else {
                double val = filter["contrast"].get<double>();
                if (val != 1.0) {
                    eq_params.push_back("contrast=" + std::to_string(val));
                }
            }
        }
        
        if (filter.contains("saturation")) {
            if (filter["saturation"].is_string()) {
                std::string sat_str = filter["saturation"].get<std::string>();
                double val = std::stod(sat_str);
                if (std::abs(val - 1.0) > 1e-6) {
                    eq_params.push_back("saturation=" + sat_str);
                }
            } else if (filter["saturation"].is_number()) {
                double val = filter["saturation"].get<double>();
                if (std::abs(val - 1.0) > 1e-6) {
                    eq_params.push_back("saturation=" + std::to_string(val));
                }
            }
        }
        
        if (filter.contains("gamma")) {
            if (filter["gamma"].is_string()) {
                std::string gamma_str = filter["gamma"].get<std::string>();
                double val = std::stod(gamma_str);
                if (std::abs(val - 1.0) > 1e-6) {
                    eq_params.push_back("gamma=" + gamma_str);
                }
            } else if (filter["gamma"].is_number()) {
                double val = filter["gamma"].get<double>();
                if (std::abs(val - 1.0) > 1e-6) {
                    eq_params.push_back("gamma=" + std::to_string(val));
                }
            }
        }
        
        if (!eq_params.empty()) {
            std::ostringstream eq_filter;
            eq_filter << "eq";
            for (size_t i = 0; i < eq_params.size(); ++i) {
                eq_filter << (i == 0 ? "=" : ":") << eq_params[i];
            }
            filters.push_back(eq_filter.str());
        }
        
        std::ostringstream oss;
        for (size_t i = 0; i < filters.size(); ++i) {
            if (i > 0) oss << ",";
            oss << filters[i];
        }
        
        return oss.str();
    }
    
    static std::string buildAudioFiltersString(const nlohmann::json& root) {
        std::vector<std::string> filters;

        if (root.contains("audio")) {
            auto& audio = root["audio"];
            if (audio.contains("volume") && audio["volume"] != "100") {
                if (audio["volume"].is_string()) {
                    std::string vol = audio["volume"].get<std::string>();
                    int vol_int = std::stoi(vol);
                    if (vol_int != 100) {
                        double vol_factor = vol_int / 100.0;
                        filters.push_back("volume=" + std::to_string(vol_factor));
                    }
                } else if (audio["volume"].is_number()) {
                    int vol_int = audio["volume"].get<int>();
                    if (vol_int != 100) {
                        double vol_factor = vol_int / 100.0;
                        filters.push_back("volume=" + std::to_string(vol_factor));
                    }
                }
            }
        }

        if (root.contains("filter")) {
            auto& filter = root["filter"];
            if (filter.contains("acontrast") && filter["acontrast"] != "33") {
                if (filter["acontrast"].is_string()) {
                    filters.push_back("acontrast=" + filter["acontrast"].get<std::string>());
                } else {
                    filters.push_back("acontrast=" + std::to_string(filter["acontrast"].get<int>()));
                }
            }
        }

        std::ostringstream oss;
        for (size_t i = 0; i < filters.size(); ++i) {
            if (i > 0) oss << ",";
            oss << filters[i];
        }

        return oss.str();
    }
    
    static FFmpeg::FFENCODERCONFIG getDefaultEncoderConfig() {
        FFmpeg::FFENCODERCONFIG config = {
            .version = 2,
            .video{
                .encoder{"libx264"},
                .options{
                    "_pixelFormat=yuv420p|crf=17|preset=medium|"
                    "g=480|b=5000k"
                },
                .filters{""},
                .sidedata{""}
            },
            .audio{
                .encoder{"aac"},
                .options{"_sampleFormat=fltp|b=320k|profile=aac_low"},
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
