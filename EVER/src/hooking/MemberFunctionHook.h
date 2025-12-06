#pragma once

#include "HookUtility.h"
#include "logger.h"
#include <polyhook2/Virtuals/VFuncSwapHook.hpp>
#include <memory>
#include <cstdint>

namespace ever {
    namespace hooking {

        struct MemberHookInfo {
            explicit MemberHookInfo(const uint16_t vtableIndex) : vtableIndex(vtableIndex) {}

            uint16_t vtableIndex;
            std::shared_ptr<PLH::VFuncSwapHook> hook;
        };

        template <class ClassType, class FuncType>
        HRESULT hookMemberFunction(ClassType* instance, FuncType hookFunc, FuncType* originalFunc,
                                MemberHookInfo& hookInfo) {
            if (hookInfo.hook.get() != nullptr) {
                return S_OK;
            }

            const PLH::VFuncMap hookMap = {{hookInfo.vtableIndex, reinterpret_cast<uint64_t>(hookFunc)}};
            PLH::VFuncMap originalFunctions;
            
            const auto newHook = new PLH::VFuncSwapHook(
                reinterpret_cast<uint64_t>(instance), 
                hookMap, 
                &originalFunctions
            );
            
            if (!newHook->hook()) {
                delete newHook;
                LOG(LL_ERR, "Failed to hook virtual member function at index ", hookInfo.vtableIndex);
                return E_FAIL;
            }

            *originalFunc = ForceCast<FuncType, uint64_t>(originalFunctions[hookInfo.vtableIndex]);
            hookInfo.hook.reset(newHook);
            
            return S_OK;
        }

    }
}

#define DEFINE_MEMBER_HOOK(BASE_CLASS, METHOD_NAME, VFUNC_INDEX, RETURN_TYPE, ...)                     \
    namespace BASE_CLASS##Hooks {                                                                      \
        namespace METHOD_NAME {                                                                        \
        RETURN_TYPE Implementation(BASE_CLASS* pThis, __VA_ARGS__);                                    \
        typedef RETURN_TYPE (*Type)(BASE_CLASS * pThis, __VA_ARGS__);                                  \
        inline Type OriginalFunc = nullptr;                                                            \
        inline ::ever::hooking::MemberHookInfo Info(VFUNC_INDEX);                                      \
        }                                                                                              \
    }

#define PERFORM_MEMBER_HOOK_REQUIRED(BASE_CLASS, METHOD_NAME, P_INSTANCE)                              \
    REQUIRE(::ever::hooking::hookMemberFunction(                                                       \
                P_INSTANCE,                                                                            \
                BASE_CLASS##Hooks::METHOD_NAME::Implementation,                                        \
                &BASE_CLASS##Hooks::METHOD_NAME::OriginalFunc,                                         \
                BASE_CLASS##Hooks::METHOD_NAME::Info),                                                 \
            "Failed to hook " #BASE_CLASS "::" #METHOD_NAME)
