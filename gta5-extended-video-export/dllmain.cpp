#include "encoder.h"
#include "logger.h"
#include "script.h"
#include "stdafx.h"
#include "polyhook2/ErrorLog.hpp"

#include <filesystem>

// Reshade and ImGui integration
#define IMGUI_DISABLE_INCLUDE_IMCONFIG_H
#define ImTextureID ImU64
#include <imgui.h>
#include <reshade.hpp>

namespace eve {
    bool isExportActive();
}

static bool g_overlay_was_open_before_export = false;
static reshade::api::effect_runtime* g_current_runtime = nullptr;

static bool on_reshade_open_overlay(reshade::api::effect_runtime *runtime, bool open, reshade::api::input_source source)
{
    g_current_runtime = runtime;
    
    // If export is active, prevent overlay from opening
    if (eve::isExportActive() && open) {
        return true;
    }
    
    return false;
}

class PolyHookLogger : public PLH::Logger {
    void log(const std::string& msg, PLH::ErrorLevel level) override {
        switch (level) {
        case PLH::ErrorLevel::INFO:
            LOG(LL_TRC, msg);
            break;
        case PLH::ErrorLevel::WARN:
            LOG(LL_WRN, msg);
            break;
        case PLH::ErrorLevel::SEV:
            LOG(LL_ERR, msg);
            break;
        case PLH::ErrorLevel::NONE:
        default:
            LOG(LL_DBG, msg);
            break;
        }
    }
};

// ImGui settings overlay for Reshade
static void draw_eve_settings(reshade::api::effect_runtime *runtime)
{
    const bool is_rendering = eve::isExportActive();
    
    ImGui::TextUnformatted("EVE - Extended Video Export Settings");
    ImGui::Separator();
    
    if (is_rendering) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.5f, 0.0f, 1.0f));
        ImGui::TextUnformatted("[RENDER IN PROGRESS - Settings Locked]");
        ImGui::PopStyleColor();
        ImGui::Separator();
    }
    
    ImGui::BeginDisabled(is_rendering);
    
    // Enable/Disable Mod
    if (ImGui::Checkbox("Enable Mod", &config::is_mod_enabled)) {
        config::save();
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Enable or disable the Extended Video Export mod");
    
    ImGui::Spacing();
    ImGui::TextUnformatted("Export Settings");
    ImGui::Separator();
    
    // FPS Settings
    float current_fps = static_cast<float>(config::fps.first) / static_cast<float>(config::fps.second);
    int fps_display = static_cast<int>(current_fps + 0.5f); // Round to nearest integer
    
    if (ImGui::InputInt("FPS", &fps_display, 1, 10)) {
        if (fps_display > 0 && fps_display <= 1000) {
            config::fps.first = static_cast<uint32_t>(fps_display * 1000);
            config::fps.second = 1000;
            config::save();
        }
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Frames per second for video export (e.g., 30, 60, 120)");
    
    // Motion Blur Settings
    ImGui::Spacing();
    int mb_samples = static_cast<int>(config::motion_blur_samples);
    if (ImGui::SliderInt("Motion Blur Samples", &mb_samples, 0, 255)) {
        config::motion_blur_samples = static_cast<uint8_t>(mb_samples);
        config::save();
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Number of motion blur samples (0 = disabled, higher = smoother but slower)");
    
    if (ImGui::SliderFloat("Motion Blur Strength", &config::motion_blur_strength, 0.0f, 1.0f, "%.2f")) {
        config::save();
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Motion blur strength (0.0 = none, 1.0 = maximum)");
    
    // Export Options
    ImGui::Spacing();
    if (ImGui::Checkbox("Export OpenEXR", &config::export_openexr)) {
        config::save();
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Export OpenEXR image sequence alongside video");
    
    if (ImGui::Checkbox("Disable Watermark", &config::disable_watermark)) {
        config::save();
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Remove Rockstar Editor watermark from exports");
    
    if (ImGui::Checkbox("Auto Reload Config", &config::auto_reload_config)) {
        config::save();
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Automatically reload config when INI file changes");
    
    // Output Directory
    ImGui::Spacing();
    ImGui::TextUnformatted("Output Directory");
    ImGui::Separator();
    
    char output_path[512];
    strncpy_s(output_path, config::output_dir.c_str(), sizeof(output_path) - 1);
    if (ImGui::InputText("##OutputDir", output_path, sizeof(output_path))) {
        config::output_dir = output_path;
        config::save();
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Directory where exported videos will be saved");
    
    // Log Level
    ImGui::Spacing();
    ImGui::TextUnformatted("Log Level");
    ImGui::Separator();
    
    const char* log_levels[] = { "Error", "Warning", "Info", "Debug", "Trace" };
    int current_log_level = static_cast<int>(config::log_level);
    if (ImGui::Combo("Log Level", &current_log_level, log_levels, IM_ARRAYSIZE(log_levels))) {
        config::log_level = static_cast<LogLevel>(current_log_level);
        Logger::instance().level = config::log_level;
        config::save();
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Set logging verbosity level");
    
    // Actions
    ImGui::Spacing();
    ImGui::Separator();
    if (ImGui::Button("Reload Config from INI")) {
        config::reload();
        Logger::instance().level = config::log_level;
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Reload all settings from the INI file");
    
    ImGui::SameLine();
    if (ImGui::Button("Save All Settings")) {
        config::save();
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Force save all current settings to INI file");
    
    ImGui::EndDisabled();
    
    // Status Info
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextUnformatted("Status Information");
    ImGui::Text("Current FPS: %.2f", static_cast<float>(config::fps.first) / static_cast<float>(config::fps.second));
    ImGui::Text("Export Active: %s", is_rendering ? "YES" : "NO");
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID lpReserved) {
    switch (reason) {
    case DLL_PROCESS_ATTACH:
        LOG(LL_NON, std::filesystem::current_path());
        SetDllDirectoryW(utf8_decode(AsiPath() + R"(\EVE\dlls\)").c_str());
        LOG(LL_DBG, "Initializing PolyHook logger");
        PLH::Log::registerLogger(std::make_unique<PolyHookLogger>());
        
        // Register with Reshade
        if (reshade::register_addon(hModule)) {
            LOG(LL_NON, "Successfully registered with Reshade!");
            // Register settings overlay
            reshade::register_overlay(nullptr, draw_eve_settings);
            // Register event to block overlay during export
            reshade::register_event<reshade::addon_event::reshade_open_overlay>(on_reshade_open_overlay);
        } else {
            LOG(LL_ERR, "Failed to register with Reshade! EVE requires Reshade to function.");
            return FALSE;
        }
        
        config::reload();
        if (!config::is_mod_enabled) {
            LOG(LL_NON, "Extended Video Export mod is disabled in the config file. Exiting...");
            return TRUE;
        } else {
            LOG(LL_NON, "Extended Video Export mod is enabled. Initializing...");
        }
        Logger::instance().level = config::log_level;
        LOG_CALL(LL_DBG, eve::initialize());
        LOG(LL_NFO, "Registering script...");
        LOG_CALL(LL_DBG, scriptRegister(hModule, eve::ScriptMain));
        break;
    case DLL_PROCESS_DETACH:
        LOG(LL_NFO, "Unregistering DXGI callback");
        LOG_CALL(LL_DBG, scriptUnregister(hModule));
        LOG_CALL(LL_DBG, eve::finalize());
        reshade::unregister_addon(hModule);
        break;
    }
    return TRUE;
}
