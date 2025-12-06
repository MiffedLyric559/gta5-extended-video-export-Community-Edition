#include "PatternScanner.h"
#include "logger.h"
#include <polyhook2/Misc.hpp>

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
                const auto address = PLH::findPattern(
                    reinterpret_cast<uint64_t>(moduleInfo_.lpBaseOfDll),
                    moduleInfo_.SizeOfImage, 
                    entry.pattern.c_str()
                );
                
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
