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

            uint64_t getModuleBase() const { return reinterpret_cast<uint64_t>(moduleInfo_.lpBaseOfDll); }
            size_t   getModuleSize() const { return static_cast<size_t>(moduleInfo_.SizeOfImage); }

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
