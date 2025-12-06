#pragma once

#include "HookUtility.h"
#include "logger.h"
#include <polyhook2/PE/IatHook.hpp>
#include <polyhook2/PE/EatHook.hpp>
#include <memory>
#include <string>
#include <Windows.h>

namespace ever {
    namespace hooking {

    template <class FuncType>
        HRESULT hookNamedImport(const std::string& dllName, const std::string& apiName, LPVOID hookFunc,
                            FuncType* originalFunc, std::shared_ptr<PLH::IatHook>& iatHook) {
        if (iatHook.get() != nullptr) {
            return S_OK;
        }

        const auto newHook = new PLH::IatHook(
            dllName, 
            apiName, 
            reinterpret_cast<uint64_t>(hookFunc),
            reinterpret_cast<uint64_t*>(originalFunc), 
            L""
        );
        
        // Install the hook
        if (!newHook->hook()) {
            *originalFunc = nullptr;
            delete newHook;
            LOG(LL_ERR, "Failed to hook IAT function: ", dllName, "!", apiName);
            return E_FAIL;
        }

        iatHook.reset(newHook);
        return S_OK;
    }

    template <class FuncType>
        HRESULT hookNamedExport(const std::wstring& dllName, const std::string& apiName, LPVOID hookFunc,
                                FuncType* originalFunc, std::shared_ptr<PLH::EatHook>& eatHook) {
            if (eatHook.get() != nullptr) {
                return S_OK;
            }

            const auto newHook = new PLH::EatHook(
                apiName, 
                dllName, 
                reinterpret_cast<uint64_t>(hookFunc),
                reinterpret_cast<uint64_t*>(originalFunc)
            );
            
            if (!newHook->hook()) {
                *originalFunc = nullptr;
                delete newHook;
                LOG(LL_ERR, "Failed to hook EAT function: ", apiName);
                return E_FAIL;
            }

            eatHook.reset(newHook);
            return S_OK;
        }

    }
}

#define DEFINE_NAMED_IMPORT_HOOK(DLL_NAME_STRING, FUNCTION_NAME, RETURN_TYPE, ...)                     \
    namespace ImportHooks {                                                                            \
        namespace FUNCTION_NAME {                                                                      \
        RETURN_TYPE Implementation(__VA_ARGS__);                                                       \
        typedef RETURN_TYPE (*Type)(__VA_ARGS__);                                                      \
        inline Type OriginalFunc = nullptr;                                                            \
        inline std::shared_ptr<PLH::IatHook> Hook;                                                     \
        }                                                                                              \
    }

#define DEFINE_NAMED_EXPORT_HOOK(DLL_NAME_STRING, FUNCTION_NAME, RETURN_TYPE, ...)                     \
    namespace ExportHooks {                                                                            \
        namespace FUNCTION_NAME {                                                                      \
        RETURN_TYPE Implementation(__VA_ARGS__);                                                       \
        typedef RETURN_TYPE (*Type)(__VA_ARGS__);                                                      \
        inline Type OriginalFunc = nullptr;                                                            \
        inline std::shared_ptr<PLH::EatHook> Hook;                                                     \
        inline std::shared_ptr<PLH::x64Detour> Detour;                                                 \
        }                                                                                              \
    }

#define PERFORM_NAMED_IMPORT_HOOK_REQUIRED(DLL_NAME_STRING, FUNCTION_NAME)                             \
    REQUIRE(::ever::hooking::hookNamedImport(                                                          \
                DLL_NAME_STRING,                                                                       \
                #FUNCTION_NAME,                                                                        \
                ImportHooks::FUNCTION_NAME::Implementation,                                            \
                &ImportHooks::FUNCTION_NAME::OriginalFunc,                                             \
                ImportHooks::FUNCTION_NAME::Hook),                                                     \
            "Failed to hook " #FUNCTION_NAME " in " DLL_NAME_STRING)

#define PERFORM_NAMED_EXPORT_HOOK_REQUIRED(DLL_NAME_STRING, FUNCTION_NAME)                             \
    REQUIRE(::ever::hooking::hookNamedExport(                                                          \
                DLL_NAME_STRING,                                                                       \
                #FUNCTION_NAME,                                                                        \
                ExportHooks::FUNCTION_NAME::Implementation,                                            \
                &ExportHooks::FUNCTION_NAME::OriginalFunc,                                             \
                ExportHooks::FUNCTION_NAME::Hook),                                                     \
            "Failed to hook " #FUNCTION_NAME)
