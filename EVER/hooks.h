// ReSharper disable CppInconsistentNaming
// ReSharper disable CppClangTidyCppcoreguidelinesMacroUsage
#pragma once
#include "logger.h"

#include <polyhook2/Detour/x64Detour.hpp>
#include <polyhook2/PE/EatHook.hpp>
#include <polyhook2/PE/IatHook.hpp>
#include <polyhook2/Virtuals/VFuncSwapHook.hpp>
#include <polyhook2/ZydisDisassembler.hpp>
#include <eh.h>

class SehException : public std::exception {
    public:
        explicit SehException(unsigned int code) : code(code) {}
        const char* what() const noexcept override { return "SEH exception"; }
        unsigned int getCode() const noexcept { return code; }

    private:
        unsigned int code;
};

inline void __cdecl SehTranslator(unsigned int code, EXCEPTION_POINTERS*) {
        throw SehException(code);
}

class SehTranslatorGuard {
    public:
        explicit SehTranslatorGuard(_se_translator_function translator) : previous(_set_se_translator(translator)) {}
        ~SehTranslatorGuard() { _set_se_translator(previous); }

    private:
        _se_translator_function previous;
};

struct MemberHookInfoStruct {
    explicit MemberHookInfoStruct(const uint16_t index) : index(index) {}

    uint16_t index;
    std::shared_ptr<PLH::VFuncSwapHook> hook;
};

template <class CLASS_TYPE, class FUNC_TYPE>
HRESULT hookMemberFunction(CLASS_TYPE* pInstance, FUNC_TYPE hookFunc, FUNC_TYPE* originalFunc,
                           MemberHookInfoStruct& info) {
    if (info.hook.get() != nullptr) {
        return S_OK;
    }

    const PLH::VFuncMap hookMap = {{info.index, reinterpret_cast<uint64_t>(hookFunc)}};
    PLH::VFuncMap originalFunctions;
    const auto newHook = new PLH::VFuncSwapHook(reinterpret_cast<uint64_t>(pInstance), hookMap, &originalFunctions);
    if (!newHook->hook()) {
        delete newHook;
        LOG(LL_ERR, "Failed to hook function!");
        return E_FAIL;
    }

    *originalFunc = ForceCast<FUNC_TYPE, uint64_t>(originalFunctions[info.index]);
    info.hook.reset(newHook);
    return S_OK;
}

template <class FUNC_TYPE>
HRESULT hookNamedFunction(const std::string& dllName, const std::string& apiName, LPVOID hookFunc,
                          FUNC_TYPE* originalFunc, std::shared_ptr<PLH::IatHook>& IatHook) {

    if (IatHook.get() != nullptr) {
        return S_OK;
    }

    const auto newHook = new PLH::IatHook(dllName, apiName, reinterpret_cast<uint64_t>(hookFunc),
                                          reinterpret_cast<uint64_t*>(originalFunc), L"");
    if (!newHook->hook()) {
        *originalFunc = nullptr;
        delete newHook;
        LOG(LL_ERR, "Failed to hook function!");
        return E_FAIL;
    }

    IatHook.reset(newHook);
    return S_OK;
}

template <class FUNC_TYPE>
HRESULT hookNamedExportFunction(const std::wstring& dllName, const std::string& apiName, LPVOID hookFunc,
                                FUNC_TYPE* originalFunc, std::shared_ptr<PLH::EatHook>& EatHook) {

    if (EatHook.get() != nullptr) {
        return S_OK;
    }

    const auto newHook = new PLH::EatHook(apiName, dllName, reinterpret_cast<uint64_t>(hookFunc),
                                          reinterpret_cast<uint64_t*>(originalFunc));
    if (!newHook->hook()) {
        *originalFunc = nullptr;
        delete newHook;
        LOG(LL_ERR, "Failed to hook function!");
        return E_FAIL;
    }

    EatHook.reset(newHook);
    return S_OK;
}

template <class FUNC_TYPE>
HRESULT hookX64Function(uint64_t func, void* hookFunc, FUNC_TYPE* originalFunc,
                        std::shared_ptr<PLH::x64Detour>& x64Detour,
                        PLH::x64Detour::detour_scheme_t scheme = PLH::x64Detour::detour_scheme_t::RECOMMENDED) {
    if (x64Detour.get() != nullptr) {
        return S_OK;
    }

    static PLH::ZydisDisassembler Disassembler(PLH::Mode::x64);

    const auto newHook =
        new PLH::x64Detour(func, reinterpret_cast<uint64_t>(hookFunc), reinterpret_cast<uint64_t*>(originalFunc));
    newHook->setDetourScheme(scheme);

    bool hookSucceeded = false;
    SehTranslatorGuard translatorGuard(&SehTranslator);
    try {
        hookSucceeded = newHook->hook();
    } catch (const SehException& ex) {
        LOG(LL_ERR, "SEH while installing x64 detour at", Logger::hex(func, 16),
            ": code = ", Logger::hex(ex.getCode(), 8));
    } catch (const std::exception& ex) {
        LOG(LL_ERR, "Exception while installing x64 detour at", Logger::hex(func, 16), ": ", ex.what());
    }

    if (!hookSucceeded) {
        *originalFunc = nullptr;
        delete newHook;
        LOG(LL_ERR, "Failed to hook x64 function.");
        return E_FAIL;
    }

    x64Detour.reset(newHook);
    return S_OK;
}

#define DEFINE_MEMBER_HOOK(BASE_CLASS, METHOD_NAME, VFUNC_INDEX, RETURN_TYPE, ...)                                     \
    namespace BASE_CLASS##Hooks {                                                                                      \
        namespace METHOD_NAME {                                                                                        \
        RETURN_TYPE Implementation(BASE_CLASS* pThis, __VA_ARGS__);                                                    \
        typedef RETURN_TYPE (*##Type)(BASE_CLASS * pThis, __VA_ARGS__);                                                \
        Type OriginalFunc = nullptr;                                                                                   \
        MemberHookInfoStruct Info(VFUNC_INDEX);                                                                        \
        }                                                                                                              \
    }

#define DEFINE_NAMED_IMPORT_HOOK(DLL_NAME_STRING, FUNCTION_NAME, RETURN_TYPE, ...)                                     \
    namespace Import##Hooks {                                                                                          \
        namespace FUNCTION_NAME {                                                                                      \
        RETURN_TYPE Implementation(__VA_ARGS__);                                                                       \
        typedef RETURN_TYPE (*##Type)(__VA_ARGS__);                                                                    \
        Type OriginalFunc = nullptr;                                                                                   \
        std::shared_ptr<PLH::IatHook> Hook;                                                                            \
        }                                                                                                              \
    }

#define DEFINE_NAMED_EXPORT_HOOK(DLL_NAME_STRING, FUNCTION_NAME, RETURN_TYPE, ...)                                     \
    namespace Export##Hooks {                                                                                          \
        namespace FUNCTION_NAME {                                                                                      \
        RETURN_TYPE Implementation(__VA_ARGS__);                                                                       \
        typedef RETURN_TYPE (*##Type)(__VA_ARGS__);                                                                    \
        Type OriginalFunc = nullptr;                                                                                   \
        std::shared_ptr<PLH::EatHook> Hook;                                                                            \
        std::shared_ptr<PLH::x64Detour> Detour;                                                                        \
        }                                                                                                              \
    }

#define DEFINE_X64_HOOK(FUNCTION_NAME, RETURN_TYPE, ...)                                                               \
    namespace Game##Hooks {                                                                                            \
        namespace FUNCTION_NAME {                                                                                      \
        RETURN_TYPE Implementation(__VA_ARGS__);                                                                       \
        typedef RETURN_TYPE (*##Type)(__VA_ARGS__);                                                                    \
        Type OriginalFunc = nullptr;                                                                                   \
        std::shared_ptr<PLH::x64Detour> Hook;                                                                          \
        }                                                                                                              \
    }

#define PERFORM_MEMBER_HOOK_REQUIRED(BASE_CLASS, METHOD_NAME, P_INSTANCE)                                              \
    REQUIRE(hookMemberFunction(P_INSTANCE, BASE_CLASS##Hooks::METHOD_NAME::Implementation,                             \
                               &BASE_CLASS##Hooks::METHOD_NAME::OriginalFunc, BASE_CLASS##Hooks::METHOD_NAME::Info),   \
            "Failed to hook " #BASE_CLASS "::" #METHOD_NAME)

#define PERFORM_NAMED_IMPORT_HOOK_REQUIRED(DLL_NAME_STRING, FUNCTION_NAME)                                             \
    REQUIRE(hookNamedFunction(DLL_NAME_STRING, #FUNCTION_NAME, ImportHooks::FUNCTION_NAME::Implementation,             \
                              &ImportHooks::FUNCTION_NAME::OriginalFunc, ImportHooks::FUNCTION_NAME::Hook),            \
            "Failed to hook " #FUNCTION_NAME "in " DLL_NAME_STRING)

#define PERFORM_NAMED_EXPORT_HOOK_REQUIRED(DLL_NAME_STRING, FUNCTION_NAME)                                             \
    REQUIRE(hookNamedExportFunction(DLL_NAME_STRING, #FUNCTION_NAME, ExportHooks::FUNCTION_NAME::Implementation,       \
                                    &ExportHooks::FUNCTION_NAME::OriginalFunc, ExportHooks::FUNCTION_NAME::Hook),      \
            "Failed to hook " #FUNCTION_NAME)

#define PERFORM_X64_HOOK_REQUIRED(FUNCTION_NAME, ADDRESS)                                                              \
    REQUIRE(hookX64Function(ADDRESS, GameHooks::FUNCTION_NAME::Implementation,                                         \
                            &GameHooks::FUNCTION_NAME::OriginalFunc, GameHooks::FUNCTION_NAME::Hook),                  \
            "Failed to hook " #FUNCTION_NAME)

#define PERFORM_X64_HOOK_WITH_SCHEME_REQUIRED(FUNCTION_NAME, ADDRESS, SCHEME)                                           \
    REQUIRE(hookX64Function(ADDRESS, GameHooks::FUNCTION_NAME::Implementation,                                         \
                            &GameHooks::FUNCTION_NAME::OriginalFunc, GameHooks::FUNCTION_NAME::Hook, SCHEME),          \
            "Failed to hook " #FUNCTION_NAME)
