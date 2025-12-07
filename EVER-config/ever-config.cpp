#include <Windows.h>
#include <commctrl.h>
#include <filesystem>
#include <fstream>
#include <string>
#include <nlohmann/json.hpp>
#include <vector>

#pragma comment(lib, "comctl32.lib")

namespace {
    constexpr wchar_t PRESET_FILE_NAME[] = L"preset.json";

    struct StreamConfig {
        std::string encoder;
        std::string pixFmt;
        std::string options;
        std::string filters;
        std::string sidedata;
    };

    struct PresetConfig {
        int version = 1;
        struct {
            std::string container = "mp4";
            bool faststart = true;
        } format;
        StreamConfig video{"libx264", "yuv420p", "preset=medium|crf=17", "", ""};
        StreamConfig audio{"aac", "", "_sampleFormat=fltp|b=320000", "", ""};
    };

    enum ControlId {
        IDC_CONTAINER = 100,
        IDC_FASTSTART = 101,
        IDC_VID_ENCODER = 110,
        IDC_VID_OPTIONS = 111,
        IDC_VID_FILTERS = 112,
        IDC_VID_SIDEDATA = 113,
        IDC_VID_PIXFMT = 114,
        IDC_AUD_ENCODER = 120,
        IDC_AUD_OPTIONS = 121,
        IDC_AUD_FILTERS = 122,
        IDC_AUD_SIDEDATA = 123,
        IDC_TEMPLATE = 125,
        IDC_SAVE = 130,
        IDC_RELOAD = 131
    };

    PresetConfig gPreset{};
    std::filesystem::path gPresetPath;
    HFONT gFont = nullptr;

        struct PresetTemplate {
           const char* name;
           PresetConfig cfg;
        };

        const std::vector<PresetTemplate> kTemplates = {
           {"YouTube H.264 (balanced)", [] {
               PresetConfig c; c.format.container = "mp4"; c.format.faststart = true;
               c.video.encoder = "libx264"; c.video.pixFmt = "yuv420p"; c.video.options = "preset=medium|crf=18";
               c.audio.encoder = "aac"; c.audio.options = "_sampleFormat=fltp|b=192000"; return c; }()},
           {"NVENC H.264 (fast)", [] {
               PresetConfig c; c.format.container = "mp4"; c.format.faststart = true;
               c.video.encoder = "h264_nvenc"; c.video.pixFmt = "yuv420p"; c.video.options = "preset=p5|rc=constqp|cq=19";
               c.audio.encoder = "aac"; c.audio.options = "_sampleFormat=fltp|b=192000"; return c; }()},
           {"NVENC HEVC (quality)", [] {
               PresetConfig c; c.format.container = "mp4"; c.format.faststart = true;
               c.video.encoder = "hevc_nvenc"; c.video.pixFmt = "p010le"; c.video.options = "preset=p5|rc=vbr|cq=22|profile=main10";
               c.audio.encoder = "aac"; c.audio.options = "_sampleFormat=fltp|b=256000"; return c; }()},
           {"H.265 x265 (quality)", [] {
               PresetConfig c; c.format.container = "mp4"; c.format.faststart = true;
               c.video.encoder = "libx265"; c.video.pixFmt = "yuv420p10le"; c.video.options = "preset=slow|crf=20|profile=main10";
               c.audio.encoder = "aac"; c.audio.options = "_sampleFormat=fltp|b=256000"; return c; }()},
           {"Copy streams", [] {
               PresetConfig c; c.format.container = "mkv"; c.format.faststart = false;
               c.video.encoder = "copy"; c.video.pixFmt = ""; c.video.options = "";
               c.audio.encoder = "copy"; c.audio.options = ""; return c; }()},
        };

        const std::vector<const char*> kContainers = {"mp4", "mkv", "mov", "flv"};
        const std::vector<const char*> kVideoEncoders = {"libx264", "h264_nvenc", "hevc_nvenc", "libx265", "libvpx-vp9", "copy"};
        const std::vector<const char*> kAudioEncoders = {"aac", "libopus", "ac3", "mp3", "copy"};
        const std::vector<const char*> kPixelFormats = {"yuv420p", "yuv420p10le", "nv12", "p010le"};

    std::string GetWindowTextString(HWND hwnd) {
        int len = GetWindowTextLengthA(hwnd);
        std::string text;
        text.resize(static_cast<size_t>(len));
        if (len > 0) {
            GetWindowTextA(hwnd, text.data(), len + 1);
        }
        return text;
    }

    void SetWindowTextString(HWND hwnd, const std::string& value) {
        SetWindowTextA(hwnd, value.c_str());
    }

    std::string ExtractOption(const std::string& options, const std::string& key, std::string& valueOut) {
        std::string remaining;
        valueOut.clear();
        size_t start = 0;
        while (start < options.size()) {
            size_t end = options.find('|', start);
            std::string token = options.substr(start, end == std::string::npos ? std::string::npos : end - start);
            if (!token.empty()) {
                auto eq = token.find('=');
                if (eq != std::string::npos) {
                    std::string k = token.substr(0, eq);
                    if (_stricmp(k.c_str(), key.c_str()) == 0) {
                        valueOut = token.substr(eq + 1);
                    } else {
                        if (!remaining.empty()) remaining.push_back('|');
                        remaining += token;
                    }
                } else {
                    if (!remaining.empty()) remaining.push_back('|');
                    remaining += token;
                }
            }
            if (end == std::string::npos) break;
            start = end + 1;
        }
        return remaining;
    }

    std::string UpsertOption(const std::string& options, const std::string& key, const std::string& value) {
        std::string dummy;
        std::string remaining = ExtractOption(options, key, dummy);
        if (!value.empty()) {
            if (!remaining.empty()) return remaining + "|" + key + "=" + value;
            return key + "=" + value;
        }
        return remaining;
    }

    PresetConfig LoadPreset(const std::filesystem::path& path) {
        PresetConfig cfg{};
        std::ifstream ifs(path, std::ios::in | std::ios::binary);
        if (!ifs.is_open()) {
            return cfg;
        }
        try {
            nlohmann::json j;
            ifs >> j;
            cfg.version = j.value("version", cfg.version);
            auto fmt = j.value("format", nlohmann::json::object());
            cfg.format.container = fmt.value("container", cfg.format.container);
            cfg.format.faststart = fmt.value("faststart", cfg.format.faststart);

            auto vid = j.value("video", nlohmann::json::object());
            cfg.video.encoder = vid.value("encoder", cfg.video.encoder);
            cfg.video.options = vid.value("options", cfg.video.options);
            cfg.video.options = ExtractOption(cfg.video.options, "_pixelFormat", cfg.video.pixFmt);
            cfg.video.filters = vid.value("filters", cfg.video.filters);
            cfg.video.sidedata = vid.value("sidedata", cfg.video.sidedata);

            auto aud = j.value("audio", nlohmann::json::object());
            cfg.audio.encoder = aud.value("encoder", cfg.audio.encoder);
            cfg.audio.options = aud.value("options", cfg.audio.options);
            cfg.audio.filters = aud.value("filters", cfg.audio.filters);
            cfg.audio.sidedata = aud.value("sidedata", cfg.audio.sidedata);
        } catch (...) {
            // Keep defaults if parsing fails
        }
        return cfg;
    }

    bool SavePreset(const PresetConfig& cfg, const std::filesystem::path& path) {
        try {
            nlohmann::json j;
            j["version"] = cfg.version;
            j["format"]["container"] = cfg.format.container;
            j["format"]["faststart"] = cfg.format.faststart;

            j["video"]["encoder"] = cfg.video.encoder;
            j["video"]["options"] = UpsertOption(cfg.video.options, "_pixelFormat", cfg.video.pixFmt);
            j["video"]["filters"] = cfg.video.filters;
            j["video"]["sidedata"] = cfg.video.sidedata;

            j["audio"]["encoder"] = cfg.audio.encoder;
            j["audio"]["options"] = cfg.audio.options;
            j["audio"]["filters"] = cfg.audio.filters;
            j["audio"]["sidedata"] = cfg.audio.sidedata;

            std::ofstream ofs(path, std::ios::out | std::ios::trunc | std::ios::binary);
            if (!ofs.is_open()) return false;
            ofs << j.dump(4);
            return true;
        } catch (...) {
            return false;
        }
    }

    void ApplyPresetToControls(HWND hwnd) {
        HWND container = GetDlgItem(hwnd, IDC_CONTAINER);
        SendMessageA(container, CB_SETCURSEL, -1, 0);
        for (size_t i = 0; i < kContainers.size(); ++i) {
            if (_stricmp(kContainers[i], gPreset.format.container.c_str()) == 0) {
                SendMessageA(container, CB_SETCURSEL, static_cast<WPARAM>(i), 0);
                break;
            }
        }
        SetWindowTextString(container, gPreset.format.container);
        SendMessage(GetDlgItem(hwnd, IDC_FASTSTART), BM_SETCHECK, gPreset.format.faststart ? BST_CHECKED : BST_UNCHECKED, 0);

        HWND vidEnc = GetDlgItem(hwnd, IDC_VID_ENCODER);
        SendMessageA(vidEnc, CB_SETCURSEL, -1, 0);
        for (size_t i = 0; i < kVideoEncoders.size(); ++i) {
            if (_stricmp(kVideoEncoders[i], gPreset.video.encoder.c_str()) == 0) {
                SendMessageA(vidEnc, CB_SETCURSEL, static_cast<WPARAM>(i), 0);
                break;
            }
        }
        SetWindowTextString(vidEnc, gPreset.video.encoder);

        HWND pixFmt = GetDlgItem(hwnd, IDC_VID_PIXFMT);
        SendMessageA(pixFmt, CB_SETCURSEL, -1, 0);
        for (size_t i = 0; i < kPixelFormats.size(); ++i) {
            if (_stricmp(kPixelFormats[i], gPreset.video.pixFmt.c_str()) == 0) {
                SendMessageA(pixFmt, CB_SETCURSEL, static_cast<WPARAM>(i), 0);
                break;
            }
        }
        SetWindowTextString(pixFmt, gPreset.video.pixFmt);

        SetWindowTextString(GetDlgItem(hwnd, IDC_VID_OPTIONS), gPreset.video.options);
        SetWindowTextString(GetDlgItem(hwnd, IDC_VID_FILTERS), gPreset.video.filters);
        SetWindowTextString(GetDlgItem(hwnd, IDC_VID_SIDEDATA), gPreset.video.sidedata);

        HWND audEnc = GetDlgItem(hwnd, IDC_AUD_ENCODER);
        SendMessageA(audEnc, CB_SETCURSEL, -1, 0);
        for (size_t i = 0; i < kAudioEncoders.size(); ++i) {
            if (_stricmp(kAudioEncoders[i], gPreset.audio.encoder.c_str()) == 0) {
                SendMessageA(audEnc, CB_SETCURSEL, static_cast<WPARAM>(i), 0);
                break;
            }
        }
        SetWindowTextString(audEnc, gPreset.audio.encoder);
        SetWindowTextString(GetDlgItem(hwnd, IDC_AUD_OPTIONS), gPreset.audio.options);
        SetWindowTextString(GetDlgItem(hwnd, IDC_AUD_FILTERS), gPreset.audio.filters);
        SetWindowTextString(GetDlgItem(hwnd, IDC_AUD_SIDEDATA), gPreset.audio.sidedata);
    }

    void ReadPresetFromControls(HWND hwnd) {
        gPreset.format.container = GetWindowTextString(GetDlgItem(hwnd, IDC_CONTAINER));
        gPreset.format.faststart = (SendMessage(GetDlgItem(hwnd, IDC_FASTSTART), BM_GETCHECK, 0, 0) == BST_CHECKED);

        gPreset.video.encoder = GetWindowTextString(GetDlgItem(hwnd, IDC_VID_ENCODER));
        gPreset.video.pixFmt = GetWindowTextString(GetDlgItem(hwnd, IDC_VID_PIXFMT));
        gPreset.video.options = GetWindowTextString(GetDlgItem(hwnd, IDC_VID_OPTIONS));
        gPreset.video.filters = GetWindowTextString(GetDlgItem(hwnd, IDC_VID_FILTERS));
        gPreset.video.sidedata = GetWindowTextString(GetDlgItem(hwnd, IDC_VID_SIDEDATA));

        gPreset.audio.encoder = GetWindowTextString(GetDlgItem(hwnd, IDC_AUD_ENCODER));
        gPreset.audio.options = GetWindowTextString(GetDlgItem(hwnd, IDC_AUD_OPTIONS));
        gPreset.audio.filters = GetWindowTextString(GetDlgItem(hwnd, IDC_AUD_FILTERS));
        gPreset.audio.sidedata = GetWindowTextString(GetDlgItem(hwnd, IDC_AUD_SIDEDATA));
    }

    HWND CreateLabeledEdit(HWND parent, const char* label, int x, int y, int width, int id, bool multiline = false) {
        HWND hLabel = CreateWindowExA(0, "STATIC", label, WS_CHILD | WS_VISIBLE, x, y, 90, 20, parent, nullptr, nullptr, nullptr);
        HWND hEdit = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL |
                             (multiline ? ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN : 0),
                         x + 95, y - 2, width, multiline ? 54 : 22, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), nullptr, nullptr);
        SendMessage(hLabel, WM_SETFONT, reinterpret_cast<WPARAM>(gFont), TRUE);
        SendMessage(hEdit, WM_SETFONT, reinterpret_cast<WPARAM>(gFont), TRUE);
        return hEdit;
    }

    HWND CreateLabeledCombo(HWND parent, const char* label, int x, int y, int width, int id, const std::vector<const char*>& values) {
        HWND hLabel = CreateWindowExA(0, "STATIC", label, WS_CHILD | WS_VISIBLE, x, y, 90, 20, parent, nullptr, nullptr, nullptr);
        HWND hCombo = CreateWindowExA(0, "COMBOBOX", "", WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST,
                                     x + 95, y - 2, width, 200, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), nullptr, nullptr);
        SendMessage(hLabel, WM_SETFONT, reinterpret_cast<WPARAM>(gFont), TRUE);
        SendMessage(hCombo, WM_SETFONT, reinterpret_cast<WPARAM>(gFont), TRUE);
        for (const auto* v : values) {
            SendMessageA(hCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(v));
        }
        return hCombo;
    }

    void CreateControls(HWND hwnd) {
        const int left = 20;
        const int fieldWidth = 520;
        const int comboWidth = 240;

        CreateWindowExA(0, "STATIC", "Select a template or customize settings", WS_CHILD | WS_VISIBLE,
                        left, 14, 400, 18, hwnd, nullptr, nullptr, nullptr);
        CreateLabeledCombo(hwnd, "Template", left, 34, comboWidth, IDC_TEMPLATE, {"Select..."});

        CreateWindowExA(0, "STATIC", "Format", WS_CHILD | WS_VISIBLE | SS_SIMPLE,
                        left, 70, 200, 18, hwnd, nullptr, nullptr, nullptr);
        CreateLabeledCombo(hwnd, "Container", left, 90, comboWidth, IDC_CONTAINER, kContainers);
        HWND faststart = CreateWindowExA(0, "BUTTON", "Faststart", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                                         left + comboWidth + 110, 88, 100, 24, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_FASTSTART)), nullptr, nullptr);
        SendMessage(faststart, WM_SETFONT, reinterpret_cast<WPARAM>(gFont), TRUE);

        CreateWindowExA(0, "STATIC", "Video", WS_CHILD | WS_VISIBLE | SS_SIMPLE,
                        left, 124, 200, 18, hwnd, nullptr, nullptr, nullptr);
        CreateLabeledCombo(hwnd, "Encoder", left, 144, comboWidth, IDC_VID_ENCODER, kVideoEncoders);
        CreateLabeledCombo(hwnd, "Pixel format", left, 176, comboWidth, IDC_VID_PIXFMT, kPixelFormats);
        CreateLabeledEdit(hwnd, "Options", left, 208, fieldWidth, IDC_VID_OPTIONS, true);
        CreateLabeledEdit(hwnd, "Filters", left, 276, fieldWidth, IDC_VID_FILTERS, true);
        CreateLabeledEdit(hwnd, "Sidedata", left, 344, fieldWidth, IDC_VID_SIDEDATA, true);

        CreateWindowExA(0, "STATIC", "Audio", WS_CHILD | WS_VISIBLE | SS_SIMPLE,
                        left, 418, 200, 18, hwnd, nullptr, nullptr, nullptr);
        CreateLabeledCombo(hwnd, "Encoder", left, 438, comboWidth, IDC_AUD_ENCODER, kAudioEncoders);
        CreateLabeledEdit(hwnd, "Options", left, 470, fieldWidth, IDC_AUD_OPTIONS, true);
        CreateLabeledEdit(hwnd, "Filters", left, 538, fieldWidth, IDC_AUD_FILTERS, true);
        CreateLabeledEdit(hwnd, "Sidedata", left, 606, fieldWidth, IDC_AUD_SIDEDATA, true);

        HWND saveBtn = CreateWindowExA(0, "BUTTON", "Save", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                                       left + fieldWidth - 10, 674, 80, 28, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_SAVE)), nullptr, nullptr);
        HWND reloadBtn = CreateWindowExA(0, "BUTTON", "Reload", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                                         left + fieldWidth - 100, 674, 80, 28, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_RELOAD)), nullptr, nullptr);
        SendMessage(saveBtn, WM_SETFONT, reinterpret_cast<WPARAM>(gFont), TRUE);
        SendMessage(reloadBtn, WM_SETFONT, reinterpret_cast<WPARAM>(gFont), TRUE);

        HWND tmplCombo = GetDlgItem(hwnd, IDC_TEMPLATE);
        for (const auto& t : kTemplates) {
            SendMessageA(tmplCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(t.name));
        }
    }

    LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        switch (msg) {
        case WM_CREATE:
            CreateControls(hwnd);
            ApplyPresetToControls(hwnd);
            break;
        case WM_COMMAND: {
            const int id = LOWORD(wParam);
            if (id == IDC_SAVE) {
                ReadPresetFromControls(hwnd);
                if (SavePreset(gPreset, gPresetPath)) {
                    MessageBoxA(hwnd, "preset.json saved", "Success", MB_ICONINFORMATION);
                } else {
                    MessageBoxA(hwnd, "Failed to save preset.json", "Error", MB_ICONERROR);
                }
                return 0;
            }
            if (id == IDC_RELOAD) {
                gPreset = LoadPreset(gPresetPath);
                ApplyPresetToControls(hwnd);
                MessageBoxA(hwnd, "Reloaded from preset.json", "Info", MB_ICONINFORMATION);
                return 0;
            }

            if (HIWORD(wParam) == CBN_SELCHANGE) {
                if (id == IDC_TEMPLATE) {
                    int sel = static_cast<int>(SendMessageA(GetDlgItem(hwnd, IDC_TEMPLATE), CB_GETCURSEL, 0, 0));
                    if (sel >= 1 && sel - 1 < static_cast<int>(kTemplates.size())) {
                        gPreset = kTemplates[sel - 1].cfg;
                        ApplyPresetToControls(hwnd);
                    }
                    return 0;
                }
                if (id == IDC_CONTAINER) {
                    int sel = static_cast<int>(SendMessageA(GetDlgItem(hwnd, IDC_CONTAINER), CB_GETCURSEL, 0, 0));
                    if (sel >= 0 && sel < static_cast<int>(kContainers.size())) {
                        gPreset.format.container = kContainers[sel];
                        SetWindowTextString(GetDlgItem(hwnd, IDC_CONTAINER), gPreset.format.container);
                    }
                    return 0;
                }
                if (id == IDC_VID_ENCODER) {
                    int sel = static_cast<int>(SendMessageA(GetDlgItem(hwnd, IDC_VID_ENCODER), CB_GETCURSEL, 0, 0));
                    if (sel >= 0 && sel < static_cast<int>(kVideoEncoders.size())) {
                        gPreset.video.encoder = kVideoEncoders[sel];
                        SetWindowTextString(GetDlgItem(hwnd, IDC_VID_ENCODER), gPreset.video.encoder);
                    }
                    return 0;
                }
                if (id == IDC_VID_PIXFMT) {
                    int sel = static_cast<int>(SendMessageA(GetDlgItem(hwnd, IDC_VID_PIXFMT), CB_GETCURSEL, 0, 0));
                    if (sel >= 0 && sel < static_cast<int>(kPixelFormats.size())) {
                        gPreset.video.pixFmt = kPixelFormats[sel];
                        SetWindowTextString(GetDlgItem(hwnd, IDC_VID_PIXFMT), gPreset.video.pixFmt);
                    }
                    return 0;
                }
                if (id == IDC_AUD_ENCODER) {
                    int sel = static_cast<int>(SendMessageA(GetDlgItem(hwnd, IDC_AUD_ENCODER), CB_GETCURSEL, 0, 0));
                    if (sel >= 0 && sel < static_cast<int>(kAudioEncoders.size())) {
                        gPreset.audio.encoder = kAudioEncoders[sel];
                        SetWindowTextString(GetDlgItem(hwnd, IDC_AUD_ENCODER), gPreset.audio.encoder);
                    }
                    return 0;
                }
            }
            break;
        }
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        }
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }

    std::filesystem::path GetPresetPath() {
        wchar_t buffer[MAX_PATH];
        GetModuleFileNameW(nullptr, buffer, MAX_PATH);
        std::filesystem::path exePath(buffer);
        return exePath.parent_path() / PRESET_FILE_NAME;
    }
}

INT WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, PSTR, INT) {
    INITCOMMONCONTROLSEX icc{sizeof(INITCOMMONCONTROLSEX), ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&icc);

    gFont = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    gPresetPath = GetPresetPath();
    gPreset = LoadPreset(gPresetPath);

    const wchar_t CLASS_NAME[] = L"FFmpegPresetEditor";
    WNDCLASSW wc{};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);

    if (!RegisterClassW(&wc)) {
        return -1;
    }

    HWND hwnd = CreateWindowExW(0, CLASS_NAME, L"FFmpeg Preset Editor", WS_OVERLAPPEDWINDOW ^ WS_MAXIMIZEBOX ^ WS_THICKFRAME,
                                CW_USEDEFAULT, CW_USEDEFAULT, 720, 760, nullptr, nullptr, hInstance, nullptr);
    if (!hwnd) {
        return -1;
    }

    ShowWindow(hwnd, SW_SHOWNORMAL);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return static_cast<INT>(msg.wParam);
}