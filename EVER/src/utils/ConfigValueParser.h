#pragma once

#include "logger.h"

#include <algorithm>
#include <limits>
#include <sstream>
#include <string>
#include <utility>

namespace ConfigValueParser {

// Helper to trim whitespace from strings
inline std::string trim(const std::string& str) {
    const auto start = str.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) {
        return "";
    }
    const auto end = str.find_last_not_of(" \t\r\n");
    return str.substr(start, end - start + 1);
}

// Helper to convert string to lowercase
inline std::string toLower(const std::string& str) {
    std::string result = str;
    std::ranges::transform(result, result.begin(), ::tolower);
    return result;
}

// Parse boolean from string
inline bool parseBoolean(const std::string& str) {
    const std::string lower = toLower(trim(str));
    if (lower == "true" || lower == "1" || lower == "yes" || lower == "on") {
        return true;
    }
    if (lower == "false" || lower == "0" || lower == "no" || lower == "off") {
        return false;
    }
    
    // Try standard parsing
    bool value = false;
    std::istringstream(lower) >> std::boolalpha >> value;
    return value;
}

// Parse integer from string with range clamping
template<typename T>
inline T parseInt(const std::string& str, T min_val, T max_val) {
    const std::string trimmed = trim(str);
    const int64_t value = std::stoll(trimmed);
    
    if (value < static_cast<int64_t>(min_val)) {
        return min_val;
    }
    if (value > static_cast<int64_t>(max_val)) {
        return max_val;
    }
    return static_cast<T>(value);
}

// Parse float from string with range clamping
inline float parseFloat(const std::string& str, float min_val, float max_val) {
    const std::string trimmed = trim(str);
    float value = std::stof(trimmed);
    
    if (value < min_val) {
        value = min_val;
    } else if (value > max_val) {
        value = max_val;
    }
    return value;
}

// Parse fraction (e.g., "30/1" or "30000/1001") or decimal (e.g., "29.97")
inline std::pair<int32_t, int32_t> parseFraction(const std::string& str) {
    const std::string trimmed = trim(str);
    
    // Check for fraction format (e.g., "30/1")
    const size_t slash_pos = trimmed.find('/');
    if (slash_pos != std::string::npos) {
        const int32_t numerator = std::stoi(trimmed.substr(0, slash_pos));
        const int32_t denominator = std::stoi(trimmed.substr(slash_pos + 1));
        return {numerator, denominator};
    }
    
    // Otherwise treat as decimal (e.g., "29.97")
    const float value = std::stof(trimmed);
    const int32_t num = static_cast<int32_t>(value * 1000);
    const int32_t den = 1000;
    return {num, den};
}

// Template class for type-safe config value parsing with logging
template<typename T>
class ConfigValue {
public:
    ConfigValue(std::string key, T default_value)
        : key_(std::move(key))
        , default_value_(std::move(default_value))
        , value_(default_value_) {}
    
    // Parse and set value from string
    bool parse(const std::string& str_value) {
        try {
            if (str_value.empty() || trim(str_value).empty()) {
                logDefault("empty value");
                value_ = default_value_;
                return false;
            }
            
            if constexpr (std::is_same_v<T, bool>) {
                value_ = parseBoolean(str_value);
            } else if constexpr (std::is_same_v<T, std::string>) {
                value_ = trim(str_value);
            } else if constexpr (std::is_integral_v<T>) {
                // Parse as integer using numeric_limits for range
                value_ = parseInt<T>(str_value, (std::numeric_limits<T>::min)(), (std::numeric_limits<T>::max)());
            } else if constexpr (std::is_floating_point_v<T>) {
                // Parse as float using negated max for min value
                value_ = parseFloat(str_value, -(std::numeric_limits<T>::max)(), (std::numeric_limits<T>::max)());
            } else {
                // Unsupported type
                static_assert(sizeof(T) == 0, "Unsupported ConfigValue type");
            }
            
            logSuccess();
            return true;
            
        } catch (const std::exception& ex) {
            logError(str_value, ex.what());
            value_ = default_value_;
            return false;
        }
    }
    
    // Get the current value
    const T& get() const { return value_; }
    
    // Get the default value
    const T& getDefault() const { return default_value_; }
    
    // Set value directly (for programmatic changes)
    void set(const T& new_value) { value_ = new_value; }
    
private:
    void logSuccess() const {
        if constexpr (std::is_same_v<T, std::pair<int32_t, int32_t>>) {
            LOG(LL_NON, "Loaded value for \"", key_, "\": <", value_.first, ", ", value_.second, ">");
        } else {
            LOG(LL_NON, "Loaded value for \"", key_, "\": ", value_);
        }
    }
    
    void logError(const std::string& str_value, const std::string& error) const {
        LOG(LL_ERR, error);
        logDefault(str_value);
    }
    
    void logDefault(const std::string& str_value) const {
        LOG(LL_NON, "Failed to parse value for \"", key_, "\": ", str_value);
        if constexpr (std::is_same_v<T, std::pair<int32_t, int32_t>>) {
            LOG(LL_NON, "Using default value of <", default_value_.first, ", ", default_value_.second, "> for \"", key_, "\"");
        } else {
            LOG(LL_NON, "Using default value of \"", default_value_, "\" for \"", key_, "\"");
        }
    }
    
    std::string key_;
    T default_value_;
    T value_;
};

// Specialization for std::pair<int32_t, int32_t> (fractions/fps)
template<>
inline bool ConfigValue<std::pair<int32_t, int32_t>>::parse(const std::string& str_value) {
    try {
        if (str_value.empty() || trim(str_value).empty()) {
            logDefault("empty value");
            value_ = default_value_;
            return false;
        }
        
        value_ = parseFraction(str_value);
        logSuccess();
        return true;
        
    } catch (const std::exception& ex) {
        logError(str_value, ex.what());
        value_ = default_value_;
        return false;
    }
}

} // namespace ConfigValueParser
