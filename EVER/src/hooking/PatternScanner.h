#pragma once

#define NOMINMAX
#include <Windows.h>
#include <Psapi.h>
#include <map>
#include <string>
#include <cstdint>

namespace ever {
    namespace hooking {

        class PatternScanner {
        public:
            PatternScanner() = default;
            ~PatternScanner() = default;

            PatternScanner(const PatternScanner&) = delete;
            PatternScanner& operator=(const PatternScanner&) = delete;

            void initialize();

            void performScan();

            void addPattern(const std::string& name, const std::string& pattern, uint64_t* destination);

        private:
            struct PatternEntry {
                std::string pattern;
                uint64_t* destination;
            };

            std::map<std::string, PatternEntry> patterns_;
            HMODULE moduleHandle_{nullptr};
            MODULEINFO moduleInfo_{};
        };

    }
}
