#pragma once

#include "logger.h"
#include "util.h"
#include "ConfigValueParser.h"

#include <INIReader.h>
#include <ShlObj.h>
#include <limits>
#include <memory>
#include <string>

// INI configuration reader class
class IniConfigReader {
public:
    explicit IniConfigReader(const std::string& ini_file_path)
        : ini_path_(ini_file_path)
        , reader_(std::make_shared<INIReader>(ini_file_path)) {
        
        if (reader_->ParseError() != 0) {
            LOG(LL_WRN, "INI file parse error at line ", reader_->ParseError(), " in file: ", ini_file_path);
        }
    }
    
    // Read a boolean value
    bool readBool(const std::string& section, const std::string& key, bool default_value) {
        const std::string str_value = reader_->GetString(section, key, "");
        ConfigValueParser::ConfigValue<bool> config_val(key, default_value);
        config_val.parse(str_value);
        return config_val.get();
    }
    
    // Read an integer value with range constraints
    template<typename T>
    T readInt(const std::string& section, const std::string& key, T default_value, T min_val = (std::numeric_limits<T>::min)(), T max_val = (std::numeric_limits<T>::max)()) {
        const std::string str_value = reader_->GetString(section, key, "");
        
        try {
            if (str_value.empty() || ConfigValueParser::trim(str_value).empty()) {
                LOG(LL_NON, "Failed to parse value for \"", key, "\": empty value");
                LOG(LL_NON, "Using default value of \"", default_value, "\" for \"", key, "\"");
                return default_value;
            }
            
            T value = ConfigValueParser::parseInt<T>(str_value, min_val, max_val);
            
            // Log if value was clamped
            const int64_t raw_value = std::stoll(ConfigValueParser::trim(str_value));
            if (raw_value != static_cast<int64_t>(value)) {
                LOG(LL_NON, "Specified ", key, " value ", raw_value, " clamped to range [", min_val, ", ", max_val, "]");
                LOG(LL_NON, "Using value of ", value);
            } else {
                LOG(LL_NON, "Loaded value for \"", key, "\": ", value);
            }
            
            return value;
            
        } catch (const std::exception& ex) {
            LOG(LL_ERR, ex.what());
            LOG(LL_NON, "Failed to parse value for \"", key, "\": ", str_value);
            LOG(LL_NON, "Using default value of \"", default_value, "\" for \"", key, "\"");
            return default_value;
        }
    }
    
    // Read a float value with range constraints
    float readFloat(const std::string& section, const std::string& key, float default_value, float min_val = -(std::numeric_limits<float>::max)(), float max_val = (std::numeric_limits<float>::max)()) {
        const std::string str_value = reader_->GetString(section, key, "");
        
        try {
            if (str_value.empty() || ConfigValueParser::trim(str_value).empty()) {
                LOG(LL_NON, "Failed to parse value for \"", key, "\": empty value");
                LOG(LL_NON, "Using default value of \"", default_value, "\" for \"", key, "\"");
                return default_value;
            }
            
            float value = ConfigValueParser::parseFloat(str_value, min_val, max_val);
            LOG(LL_NON, "Loaded value for \"", key, "\": ", value);
            return value;
            
        } catch (const std::exception& ex) {
            LOG(LL_ERR, ex.what());
            LOG(LL_NON, "Failed to parse value for \"", key, "\": ", str_value);
            LOG(LL_NON, "Using default value of \"", default_value, "\" for \"", key, "\"");
            return default_value;
        }
    }
    
    // Read a string value
    std::string readString(const std::string& section, const std::string& key, const std::string& default_value) {
        const std::string str_value = reader_->GetString(section, key, "");
        const std::string trimmed = ConfigValueParser::trim(str_value);
        
        if (trimmed.empty()) {
            if (!default_value.empty()) {
                LOG(LL_NON, "Failed to parse value for \"", key, "\": empty value");
                LOG(LL_NON, "Using default value of \"", default_value, "\" for \"", key, "\"");
            }
            return default_value;
        }
        
        LOG(LL_NON, "Loaded value for \"", key, "\": ", trimmed);
        return trimmed;
    }
    
    // Read a fraction/fps value (e.g., "30/1" or "29.97")
    std::pair<int32_t, int32_t> readFraction(const std::string& section, const std::string& key, std::pair<int32_t, int32_t> default_value) {
        const std::string str_value = reader_->GetString(section, key, "");
        ConfigValueParser::ConfigValue<std::pair<int32_t, int32_t>> config_val(key, default_value);
        config_val.parse(str_value);
        return config_val.get();
    }
    
    // Read log level from string
    LogLevel readLogLevel(const std::string& section, const std::string& key, LogLevel default_value) {
        const std::string str_value = ConfigValueParser::toLower(ConfigValueParser::trim(reader_->GetString(section, key, "")));
        
        try {
            if (str_value == "error") {
                LOG(LL_NON, "Loaded value for \"", key, "\": error");
                return LL_ERR;
            } else if (str_value == "warn") {
                LOG(LL_NON, "Loaded value for \"", key, "\": warn");
                return LL_WRN;
            } else if (str_value == "info") {
                LOG(LL_NON, "Loaded value for \"", key, "\": info");
                return LL_NFO;
            } else if (str_value == "debug") {
                LOG(LL_NON, "Loaded value for \"", key, "\": debug");
                return LL_DBG;
            } else if (str_value == "trace") {
                LOG(LL_NON, "Loaded value for \"", key, "\": trace");
                return LL_TRC;
            }
        } catch (const std::exception& ex) {
            LOG(LL_ERR, ex.what());
        }
        
        LOG(LL_NON, "Failed to parse value for \"", key, "\": ", str_value);
        LOG(LL_NON, "Using default value of \"error\" for \"", key, "\"");
        return default_value;
    }
    
    // Get output directory with fallback to Videos folder
    std::string getOutputDirectory(const std::string& section, const std::string& key) {
        std::string str_value = reader_->GetString(section, key, "");
        str_value = ConfigValueParser::trim(str_value);
        
        if (!str_value.empty() && str_value.find_first_not_of(' ') != std::string::npos) {
            LOG(LL_NON, "Loaded value for \"", key, "\": ", str_value);
            return str_value;
        }
        
        // Fallback to Videos directory
        char buffer[MAX_PATH] = {0};
        if (FAILED(getVideosDirectory(buffer))) {
            throw std::runtime_error("Failed to get Videos directory for the current user.");
        }
        
        LOG(LL_NON, "Failed to parse value for \"", key, "\": ", str_value);
        LOG(LL_NON, "Using default value of \"", buffer, "\" for \"", key, "\"");
        return std::string(buffer);
    }
    
    const std::string& getPath() const { return ini_path_; }
    
private:
    HRESULT getVideosDirectory(LPSTR output) {
        PWSTR vidPath = nullptr;
        
        if (SHGetKnownFolderPath(FOLDERID_Videos, 0, nullptr, &vidPath) != S_OK) {
            LOG(LL_ERR, "Failed to get Videos directory for the current user.");
            return E_FAIL;
        }
        
        const int pathlen = lstrlenW(vidPath);
        int buflen = WideCharToMultiByte(CP_UTF8, 0, vidPath, pathlen, nullptr, 0, nullptr, nullptr);
        
        if (buflen <= 0) {
            CoTaskMemFree(vidPath);
            return E_FAIL;
        }
        
        buflen = WideCharToMultiByte(CP_UTF8, 0, vidPath, pathlen, output, buflen, nullptr, nullptr);
        output[buflen] = 0;
        
        CoTaskMemFree(vidPath);
        return S_OK;
    }
    
    std::string ini_path_;
    std::shared_ptr<INIReader> reader_;
};
