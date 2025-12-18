#include "CrashHandler.h"
#include "logger.h"

#include <DbgHelp.h>
#include <Psapi.h>
#include <iomanip>
#include <sstream>

#pragma comment(lib, "dbghelp.lib")
#pragma comment(lib, "psapi.lib")

namespace {
static LPTOP_LEVEL_EXCEPTION_FILTER g_previousExceptionFilter = nullptr;
constexpr int MAX_STACK_FRAMES = 64;

const char* GetExceptionCodeString(DWORD code) {
    switch (code) {
    case EXCEPTION_ACCESS_VIOLATION:
        return "ACCESS_VIOLATION";
    case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
        return "ARRAY_BOUNDS_EXCEEDED";
    case EXCEPTION_BREAKPOINT:
        return "BREAKPOINT";
    case EXCEPTION_DATATYPE_MISALIGNMENT:
        return "DATATYPE_MISALIGNMENT";
    case EXCEPTION_FLT_DENORMAL_OPERAND:
        return "FLT_DENORMAL_OPERAND";
    case EXCEPTION_FLT_DIVIDE_BY_ZERO:
        return "FLT_DIVIDE_BY_ZERO";
    case EXCEPTION_FLT_INEXACT_RESULT:
        return "FLT_INEXACT_RESULT";
    case EXCEPTION_FLT_INVALID_OPERATION:
        return "FLT_INVALID_OPERATION";
    case EXCEPTION_FLT_OVERFLOW:
        return "FLT_OVERFLOW";
    case EXCEPTION_FLT_STACK_CHECK:
        return "FLT_STACK_CHECK";
    case EXCEPTION_FLT_UNDERFLOW:
        return "FLT_UNDERFLOW";
    case EXCEPTION_ILLEGAL_INSTRUCTION:
        return "ILLEGAL_INSTRUCTION";
    case EXCEPTION_IN_PAGE_ERROR:
        return "IN_PAGE_ERROR";
    case EXCEPTION_INT_DIVIDE_BY_ZERO:
        return "INT_DIVIDE_BY_ZERO";
    case EXCEPTION_INT_OVERFLOW:
        return "INT_OVERFLOW";
    case EXCEPTION_INVALID_DISPOSITION:
        return "INVALID_DISPOSITION";
    case EXCEPTION_NONCONTINUABLE_EXCEPTION:
        return "NONCONTINUABLE_EXCEPTION";
    case EXCEPTION_PRIV_INSTRUCTION:
        return "PRIV_INSTRUCTION";
    case EXCEPTION_SINGLE_STEP:
        return "SINGLE_STEP";
    case EXCEPTION_STACK_OVERFLOW:
        return "STACK_OVERFLOW";
    default:
        return "UNKNOWN_EXCEPTION";
    }
}

void WriteStackTrace(CONTEXT* context) {
    HANDLE process = GetCurrentProcess();
    HANDLE thread = GetCurrentThread();

    SymInitialize(process, NULL, TRUE);
    SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME);

    STACKFRAME64 stackFrame = {};
    DWORD machineType;

#if defined(_M_X64)
    machineType = IMAGE_FILE_MACHINE_AMD64;
    stackFrame.AddrPC.Offset = context->Rip;
    stackFrame.AddrPC.Mode = AddrModeFlat;
    stackFrame.AddrFrame.Offset = context->Rbp;
    stackFrame.AddrFrame.Mode = AddrModeFlat;
    stackFrame.AddrStack.Offset = context->Rsp;
    stackFrame.AddrStack.Mode = AddrModeFlat;
#else
    machineType = IMAGE_FILE_MACHINE_I386;
    stackFrame.AddrPC.Offset = context->Eip;
    stackFrame.AddrPC.Mode = AddrModeFlat;
    stackFrame.AddrFrame.Offset = context->Ebp;
    stackFrame.AddrFrame.Mode = AddrModeFlat;
    stackFrame.AddrStack.Offset = context->Esp;
    stackFrame.AddrStack.Mode = AddrModeFlat;
#endif

    LOG(LL_ERR, "=== STACK TRACE ===");

    for (int frame = 0; frame < MAX_STACK_FRAMES; frame++) {
        if (!StackWalk64(machineType, process, thread, &stackFrame, context, NULL, SymFunctionTableAccess64,
                         SymGetModuleBase64, NULL)) {
            break;
        }

        if (stackFrame.AddrPC.Offset == 0) {
            break;
        }

        DWORD64 moduleBase = SymGetModuleBase64(process, stackFrame.AddrPC.Offset);
        char moduleName[MAX_PATH] = {0};

        if (moduleBase) {
            GetModuleFileNameA((HINSTANCE)moduleBase, moduleName, MAX_PATH);
            char* lastSlash = strrchr(moduleName, '\\');
            if (lastSlash) {
                memmove(moduleName, lastSlash + 1, strlen(lastSlash));
            }
        }

        char symbolBuffer[sizeof(SYMBOL_INFO) + MAX_SYM_NAME * sizeof(TCHAR)];
        PSYMBOL_INFO symbol = (PSYMBOL_INFO)symbolBuffer;
        symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
        symbol->MaxNameLen = MAX_SYM_NAME;

        DWORD64 displacement = 0;
        std::stringstream ss;

        if (SymFromAddr(process, stackFrame.AddrPC.Offset, &displacement, symbol)) {
            ss << "  [" << frame << "] " << moduleName << " - " << symbol->Name << " + " << Logger::hex(displacement, 8)
               << " (" << Logger::hex(stackFrame.AddrPC.Offset, 16) << ")";
        } else {
            ss << "  [" << frame << "] " << moduleName << " - " << Logger::hex(stackFrame.AddrPC.Offset, 16);
        }

        LOG(LL_ERR, ss.str());
    }

    SymCleanup(process);
}

void WriteModuleList() {
    LOG(LL_ERR, "=== LOADED MODULES ===");

    HANDLE process = GetCurrentProcess();
    HMODULE modules[1024];
    DWORD needed;

    if (EnumProcessModules(process, modules, sizeof(modules), &needed)) {
        int moduleCount = needed / sizeof(HMODULE);
        for (int i = 0; i < moduleCount; i++) {
            char moduleName[MAX_PATH];
            if (GetModuleFileNameA(modules[i], moduleName, sizeof(moduleName))) {
                MODULEINFO modInfo;
                if (GetModuleInformation(process, modules[i], &modInfo, sizeof(modInfo))) {
                    std::stringstream ss;
                    ss << "  " << moduleName
                       << " (Base: " << Logger::hex(reinterpret_cast<uint64_t>(modInfo.lpBaseOfDll), 16)
                       << ", Size: " << Logger::hex(modInfo.SizeOfImage, 8) << ")";
                    LOG(LL_ERR, ss.str());
                }
            }
        }
    }
}

void WriteSystemInfo() {
    LOG(LL_ERR, "=== SYSTEM INFORMATION ===");

    SYSTEM_INFO sysInfo;
    GetSystemInfo(&sysInfo);

    std::stringstream ss;
    ss << "  Processor Architecture: " << sysInfo.wProcessorArchitecture;
    LOG(LL_ERR, ss.str());

    ss.str("");
    ss.clear();
    ss << "  Number of Processors: " << sysInfo.dwNumberOfProcessors;
    LOG(LL_ERR, ss.str());

    MEMORYSTATUSEX memStatus;
    memStatus.dwLength = sizeof(memStatus);
    GlobalMemoryStatusEx(&memStatus);

    ss.str("");
    ss.clear();
    ss << "  Physical Memory: " << (memStatus.ullTotalPhys / (1024 * 1024)) << " MB";
    LOG(LL_ERR, ss.str());

    ss.str("");
    ss.clear();
    ss << "  Available Memory: " << (memStatus.ullAvailPhys / (1024 * 1024)) << " MB";
    LOG(LL_ERR, ss.str());

    ss.str("");
    ss.clear();
    ss << "  Memory Load: " << memStatus.dwMemoryLoad << "%";
    LOG(LL_ERR, ss.str());
}

LONG WINAPI CrashExceptionHandler(EXCEPTION_POINTERS* pExceptionInfo) {
    LOG(LL_ERR, "========================================");
    LOG(LL_ERR, "=== CRITICAL CRASH DETECTED IN EVER ===");
    LOG(LL_ERR, "========================================");

    std::stringstream ss;
    ss << "Exception Code: " << Logger::hex(pExceptionInfo->ExceptionRecord->ExceptionCode, 8) << " ("
       << GetExceptionCodeString(pExceptionInfo->ExceptionRecord->ExceptionCode) << ")";
    LOG(LL_ERR, ss.str());

    ss.str("");
    ss.clear();
    ss << "Exception Address: "
       << Logger::hex(reinterpret_cast<uint64_t>(pExceptionInfo->ExceptionRecord->ExceptionAddress), 16);
    LOG(LL_ERR, ss.str());

    if (pExceptionInfo->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION) {
        ss.str("");
        ss.clear();
        if (pExceptionInfo->ExceptionRecord->ExceptionInformation[0] == 0) {
            ss << "Access Violation: Read from "
               << Logger::hex(pExceptionInfo->ExceptionRecord->ExceptionInformation[1], 16);
        } else if (pExceptionInfo->ExceptionRecord->ExceptionInformation[0] == 1) {
            ss << "Access Violation: Write to "
               << Logger::hex(pExceptionInfo->ExceptionRecord->ExceptionInformation[1], 16);
        } else {
            ss << "Access Violation: DEP at "
               << Logger::hex(pExceptionInfo->ExceptionRecord->ExceptionInformation[1], 16);
        }
        LOG(LL_ERR, ss.str());
    }

    LOG(LL_ERR, "");
    LOG(LL_ERR, "=== CPU REGISTERS ===");

#if defined(_M_X64)
    ss.str("");
    ss.clear();
    ss << "RAX=" << Logger::hex(pExceptionInfo->ContextRecord->Rax, 16);
    LOG(LL_ERR, ss.str());
    ss.str("");
    ss.clear();
    ss << "RBX=" << Logger::hex(pExceptionInfo->ContextRecord->Rbx, 16);
    LOG(LL_ERR, ss.str());
    ss.str("");
    ss.clear();
    ss << "RCX=" << Logger::hex(pExceptionInfo->ContextRecord->Rcx, 16);
    LOG(LL_ERR, ss.str());
    ss.str("");
    ss.clear();
    ss << "RDX=" << Logger::hex(pExceptionInfo->ContextRecord->Rdx, 16);
    LOG(LL_ERR, ss.str());
    ss.str("");
    ss.clear();
    ss << "RSI=" << Logger::hex(pExceptionInfo->ContextRecord->Rsi, 16);
    LOG(LL_ERR, ss.str());
    ss.str("");
    ss.clear();
    ss << "RDI=" << Logger::hex(pExceptionInfo->ContextRecord->Rdi, 16);
    LOG(LL_ERR, ss.str());
    ss.str("");
    ss.clear();
    ss << "RSP=" << Logger::hex(pExceptionInfo->ContextRecord->Rsp, 16);
    LOG(LL_ERR, ss.str());
    ss.str("");
    ss.clear();
    ss << "RBP=" << Logger::hex(pExceptionInfo->ContextRecord->Rbp, 16);
    LOG(LL_ERR, ss.str());
    ss.str("");
    ss.clear();
    ss << "RIP=" << Logger::hex(pExceptionInfo->ContextRecord->Rip, 16);
    LOG(LL_ERR, ss.str());
#else
    ss.str("");
    ss.clear();
    ss << "EAX=" << Logger::hex(pExceptionInfo->ContextRecord->Eax, 8);
    LOG(LL_ERR, ss.str());
    ss.str("");
    ss.clear();
    ss << "EBX=" << Logger::hex(pExceptionInfo->ContextRecord->Ebx, 8);
    LOG(LL_ERR, ss.str());
    ss.str("");
    ss.clear();
    ss << "ECX=" << Logger::hex(pExceptionInfo->ContextRecord->Ecx, 8);
    LOG(LL_ERR, ss.str());
    ss.str("");
    ss.clear();
    ss << "EDX=" << Logger::hex(pExceptionInfo->ContextRecord->Edx, 8);
    LOG(LL_ERR, ss.str());
    ss.str("");
    ss.clear();
    ss << "ESI=" << Logger::hex(pExceptionInfo->ContextRecord->Esi, 8);
    LOG(LL_ERR, ss.str());
    ss.str("");
    ss.clear();
    ss << "EDI=" << Logger::hex(pExceptionInfo->ContextRecord->Edi, 8);
    LOG(LL_ERR, ss.str());
    ss.str("");
    ss.clear();
    ss << "ESP=" << Logger::hex(pExceptionInfo->ContextRecord->Esp, 8);
    LOG(LL_ERR, ss.str());
    ss.str("");
    ss.clear();
    ss << "EBP=" << Logger::hex(pExceptionInfo->ContextRecord->Ebp, 8);
    LOG(LL_ERR, ss.str());
    ss.str("");
    ss.clear();
    ss << "EIP=" << Logger::hex(pExceptionInfo->ContextRecord->Eip, 8);
    LOG(LL_ERR, ss.str());
#endif

    LOG(LL_ERR, "");
    WriteSystemInfo();
    LOG(LL_ERR, "");
    WriteStackTrace(pExceptionInfo->ContextRecord);
    LOG(LL_ERR, "");
    WriteModuleList();
    LOG(LL_ERR, "");
    LOG(LL_ERR, "========================================");
    LOG(LL_ERR, "Please report this crash with the log file to the EVER developers");
    LOG(LL_ERR, "========================================");

    if (g_previousExceptionFilter != nullptr) {
        return g_previousExceptionFilter(pExceptionInfo);
    }

    return EXCEPTION_CONTINUE_SEARCH;
}
} // namespace

namespace ever {
namespace crash {

void Initialize() {
    LOG(LL_NFO, "Installing crash exception handler");
    g_previousExceptionFilter = SetUnhandledExceptionFilter(CrashExceptionHandler);
    if (g_previousExceptionFilter != nullptr) {
        LOG(LL_DBG, "Previous exception filter saved: ",
            Logger::hex(reinterpret_cast<uint64_t>(g_previousExceptionFilter), 16));
    }
}

void Cleanup() {
    LOG(LL_NFO, "Removing crash exception handler");
    if (g_previousExceptionFilter != nullptr) {
        SetUnhandledExceptionFilter(g_previousExceptionFilter);
        g_previousExceptionFilter = nullptr;
    }
}

} // namespace crash
} // namespace ever
