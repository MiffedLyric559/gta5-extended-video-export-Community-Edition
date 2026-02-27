#pragma once

#include <string>

namespace ever {
    namespace hooking {
        namespace patterns {

            // Rendering and timing functions
            const std::string getRenderTimeBase = 
                "48 83 EC 28 48 8B 01 FF 50 ?? 8B C8 48 83 C4 28 E9 ?? ?? ?? ??";

            const std::string getGameSpeedMultiplier = 
                "8B 91 90 00 00 00 85 D2 74 65 FF CA 74 58 FF CA 74 4B FF CA 74 3E 83 EA 02 74 30 FF CA 74 23 FF CA 74 16 FF CA 74 09";

            // Audio functions - Can probably be removed in the future since this is not needed due to the dual render pass.
            const std::string stepAudio = 
                "48 8B C4 48 89 58 10 48 89 70 18 48 89 78 20 55 41 54 41 55 41 56 41 57 48 8D 68 98 48 81 EC 40 01 00 00 48 8B D9";

            const std::string audioUnknown01 = 
                "8A 81 78 4D 00 00 C0 E8 02 24 01 C3";

            // Threading functions
            const std::string createThread = 
                "48 83 EC 48 48 8B 84 24 80 00 00 00 48 89 44 24 30 48 8B 44 24 70 83 4C 24 28 FF 48 89 44 24 20";

            const std::string waitForSingleObject = 
                "48 89 5C 24 08 57 48 83 EC 20 8B DA 48 8B F9 83 CA FF 48 8B CF";

            // Texture functions
            const std::string createTexture = 
                "48 8B C4 48 89 58 08 48 89 68 10 48 89 70 18 48 89 78 20 41 56 48 83 EC 30 41 8B F9 41 8B F0 48 8B DA 4C 8B F1";

            const std::string linearizeTexture = 
                "48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 57 41 54 41 56 48 83 EC 20 8B 2D ?? ?? ?? ?? 65 48 8B 04 25 ?? ?? ?? ?? BF B8 01 00 00";

            // Export context
            const std::string createExportContext = 
                "40 53 48 81 EC A0 00 00 00 89 11 48 8B D9 44 89 41 04 44 89 49 08 48 8D 4C 24 40 BA FF FF 00 00";

            // Dual-pass audio rendering functions
            const std::string startBakeProject = 
                "48 89 5C 24 08 48 89 74 24 10 57 48 83 EC 30 40 32 FF 48 83 3D ?? ?? ?? ?? 00 48 8B DA 48 8B F1";

            // Watermark rendering
            const std::string watermarkRendererRender = 
                "48 83 EC ?? 8B 0D ?? ?? ?? ?? 65 48 8B 04 25 ?? ?? ?? ?? BA ?? ?? ?? ?? 48 8B 04 C8 8B 0C 02 D1 E9 F6 C1 ?? 74 ?? 83 3D";

            // Cleanup replay playback internal
            const std::string cleanupReplayPlayback =
                "48 83 EC ?? E8 ?? ?? ?? ?? 48 8D 0D ?? ?? ?? ?? E8 ?? ?? ?? ?? 8B 05 ?? ?? ?? ?? B9 ?? ?? ?? ?? 3B C1";

            // Is pending bake start
            const std::string isPendingBakeStart =
                "83 3D ?? ?? ?? ?? ?? 0F 94 C0 C3 CC 40 53 48 83 EC ?? 8B 0D";

            // Kill playback or bake
            // Need to get inline setState function to work properly.
            const std::string killPlaybackOrBake =
                "48 89 5C 24 08 48 89 74 24 10 48 89 7C 24 18 55 41 56 41 57 48 8B EC 48 81 EC 80 00 00 00 40 8A 7D 50 4D 8B F9 41 8A F0 48 8B DA 4C 8B F1 45 84 C0 74";

        }
    }
}
