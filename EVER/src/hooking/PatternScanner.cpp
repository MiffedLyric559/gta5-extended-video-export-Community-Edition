#include "PatternScanner.h"
#include "logger.h"
#include <polyhook2/Misc.hpp>

namespace {
    uint64_t findPatternSafe(uint64_t moduleBase, size_t moduleSize, const char* pattern, DWORD* outExceptionCode) {
        if (outExceptionCode) {
            *outExceptionCode = 0;
        }

        __try {
            return PLH::findPattern(moduleBase, moduleSize, pattern);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            if (outExceptionCode) {
                *outExceptionCode = ::GetExceptionCode();
            }
            return 0;
        }
    }
}

namespace ever {
    namespace hooking {
        void PatternScanner::initialize() {
            PRE();
            
            moduleHandle_ = ::GetModuleHandle(nullptr);
            const HANDLE processHandle = ::GetCurrentProcess();
            
            moduleInfo_ = {
                .lpBaseOfDll = nullptr,
                .SizeOfImage = 0,
                .EntryPoint = nullptr,
            };

            if (GetModuleInformation(processHandle, moduleHandle_, &moduleInfo_, sizeof(moduleInfo_)) != TRUE) {
                const DWORD err = ::GetLastError();
                LOG(LL_ERR, "Failed to get module information: ", err, " ", Logger::hex(err, 8));
                POST();
                return;
            }
            
            LOG(LL_NFO, "Process module: base = ", Logger::hex(reinterpret_cast<uint64_t>(moduleInfo_.lpBaseOfDll), 16),
                ", size = ", Logger::hex(moduleInfo_.SizeOfImage, 16));
            
            POST();
        }

        void PatternScanner::performScan() {
            PRE();

            for (const auto& [name, entry] : patterns_) {
                LOG(LL_DBG, "Scanning pattern '", name, "' (len=", entry.pattern.size(), ")");

                DWORD scanException = 0;
                const uint64_t address = findPatternSafe(
                    reinterpret_cast<uint64_t>(moduleInfo_.lpBaseOfDll),
                    static_cast<size_t>(moduleInfo_.SizeOfImage),
                    entry.pattern.c_str(),
                    &scanException
                );

                if (scanException != 0) {
                    *entry.destination = 0;
                    LOG(LL_ERR, "Pattern '", name, "' scan crashed with SEH ", Logger::hex(scanException, 8));
                    continue;
                }
                
                *entry.destination = address;
                
                if (address) {
                    LOG(LL_NFO, "Pattern '", name, "' found at: ", Logger::hex(address, 16));
                } else {
                    LOG(LL_WRN, "Pattern '", name, "' not found");
                }
            }
            
            POST();
        }

        void PatternScanner::addPattern(const std::string& name, const std::string& pattern, uint64_t* destination) {
            patterns_[name] = PatternEntry{
                .pattern = pattern, 
                .destination = destination
            };
        }

    }
}
