#pragma once

#include "HookUtility.h"
#include "logger.h"
#include <polyhook2/Detour/x64Detour.hpp>
#include <polyhook2/ZydisDisassembler.hpp>
#include <memory>
#include <cstdint>

namespace ever {
    namespace hooking {

        template <class FuncType>
            HRESULT hookX64Function(uint64_t targetAddress, void* hookFunc, FuncType* originalFunc,
                                std::shared_ptr<PLH::x64Detour>& detour,
                                PLH::x64Detour::detour_scheme_t scheme = PLH::x64Detour::detour_scheme_t::RECOMMENDED) {
            if (detour.get() != nullptr) {
                return S_OK;
            }

            const auto newHook = new PLH::x64Detour(
                targetAddress, 
                reinterpret_cast<uint64_t>(hookFunc), 
                reinterpret_cast<uint64_t*>(originalFunc)
            );
            newHook->setDetourScheme(scheme);

            bool hookSucceeded = false;
            SehTranslatorGuard translatorGuard(&sehTranslator);
            
            try {
                hookSucceeded = newHook->hook();
            } catch (const SehException& ex) {
                LOG(LL_ERR, "SEH exception while installing x64 detour at ", Logger::hex(targetAddress, 16),
                    ": code = ", Logger::hex(ex.getCode(), 8));
            } catch (const std::exception& ex) {
                LOG(LL_ERR, "Exception while installing x64 detour at ", Logger::hex(targetAddress, 16), 
                    ": ", ex.what());
            }

            if (!hookSucceeded) {
                *originalFunc = nullptr;
                delete newHook;
                LOG(LL_ERR, "Failed to hook x64 function at ", Logger::hex(targetAddress, 16));
                return E_FAIL;
            }

            detour.reset(newHook);
            return S_OK;
        }

    }
}

#define DEFINE_X64_HOOK(FUNCTION_NAME, RETURN_TYPE, ...)                                               \
    namespace GameHooks {                                                                              \
        namespace FUNCTION_NAME {                                                                      \
        RETURN_TYPE Implementation(__VA_ARGS__);                                                       \
        typedef RETURN_TYPE (*Type)(__VA_ARGS__);                                                      \
        inline Type OriginalFunc = nullptr;                                                            \
        inline std::shared_ptr<PLH::x64Detour> Hook;                                                   \
        }                                                                                              \
    }

#define PERFORM_X64_HOOK_REQUIRED(FUNCTION_NAME, ADDRESS)                                              \
    REQUIRE(::ever::hooking::hookX64Function(                                                          \
                ADDRESS,                                                                               \
                GameHooks::FUNCTION_NAME::Implementation,                                              \
                &GameHooks::FUNCTION_NAME::OriginalFunc,                                               \
                GameHooks::FUNCTION_NAME::Hook),                                                       \
            "Failed to hook " #FUNCTION_NAME)

#define PERFORM_X64_HOOK_WITH_SCHEME_REQUIRED(FUNCTION_NAME, ADDRESS, SCHEME)                          \
    REQUIRE(::ever::hooking::hookX64Function(                                                          \
                ADDRESS,                                                                               \
                GameHooks::FUNCTION_NAME::Implementation,                                              \
                &GameHooks::FUNCTION_NAME::OriginalFunc,                                               \
                GameHooks::FUNCTION_NAME::Hook,                                                        \
                SCHEME),                                                                               \
            "Failed to hook " #FUNCTION_NAME)
