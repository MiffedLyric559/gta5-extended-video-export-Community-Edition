#pragma once

// Configuration file names
#define INI_FILE_NAME "EVER\\" TARGET_NAME ".ini"
#define PRESET_FILE_NAME "EVER\\preset.json"

// Main section keys
#define CFG_AUTO_RELOAD_CONFIG "auto_reload_config"
#define CFG_ENABLE_XVX "enable_mod"
#define CFG_OUTPUT_DIR "output_folder"
#define CFG_LOG_LEVEL "log_level"

// Export section
#define CFG_EXPORT_SECTION "EXPORT"
#define CFG_EXPORT_MB_SAMPLES "motion_blur_samples"
#define CFG_EXPORT_MB_STRENGTH "motion_blur_strength"
#define CFG_EXPORT_FPS "fps"
#define CFG_EXPORT_OPENEXR "export_openexr"
#define CFG_DISABLE_WATERMARK "disable_watermark"

// Format section
#define CFG_FORMAT_SECTION "FORMAT"
#define CFG_EXPORT_FORMAT "format"
#define CFG_FORMAT_EXT "extension"
#define CFG_FORMAT_CFG "options"

// Video section
#define CFG_VIDEO_SECTION "VIDEO"
#define CFG_VIDEO_ENC "encoder"
#define CFG_VIDEO_FMT "pixel_format"
#define CFG_VIDEO_CFG "options"

// Audio section
#define CFG_AUDIO_SECTION "AUDIO"
#define CFG_AUDIO_ENC "encoder"
#define CFG_AUDIO_FMT "sample_format"
#define CFG_AUDIO_CFG "options"
