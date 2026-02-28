#include "CrashHandler.h"
#include "logger.h"
#include "util.h"   // AsiPath()

#include <DbgHelp.h>
#include <Psapi.h>
#include <exception>
#include <stdarg.h>
#include <atomic>

#pragma comment(lib, "dbghelp.lib")
#pragma comment(lib, "psapi.lib")

#ifndef DBG_PRINTEXCEPTION_C
#   define DBG_PRINTEXCEPTION_C      0x40010006UL
#endif
#ifndef DBG_PRINTEXCEPTION_WIDE_C
#   define DBG_PRINTEXCEPTION_WIDE_C 0x4001000AUL
#endif

namespace {

static constexpr int   MAX_STACK_FRAMES       = 64;
static constexpr DWORD EXCEPTION_CPP          = 0xE06D7363UL; // MSVC C++ throw
static constexpr DWORD EXCEPTION_SET_THREAD_NAME = 0x406D1388UL;

static LPTOP_LEVEL_EXCEPTION_FILTER g_previousExceptionFilter = nullptr;
static PVOID                        g_vehHandle                = nullptr;
static std::terminate_handler       g_prevTerminateHandler     = nullptr;

static volatile LONG g_crashInProgress = 0;

static volatile LONG g_symbolsInitialized = 0;

static char g_crashDumpDir[MAX_PATH] = {};

static char g_mainLogPath[MAX_PATH] = {};

static char          g_lastRageError[2048] = {};
static volatile LONG g_hasRageError        = 0;

static void RawWrite(HANDLE h, const char* s) {
    if (h == INVALID_HANDLE_VALUE || !s || !*s) return;
    DWORD n = 0;
    WriteFile(h, s, static_cast<DWORD>(strlen(s)), &n, nullptr);
}

static void RawWriteLine(HANDLE h, const char* s) {
    RawWrite(h, s);
    RawWrite(h, "\r\n");
}

static void RawWritef(HANDLE h, _Printf_format_string_ const char* fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    RawWriteLine(h, buf);
}


static bool IsNonFatalException(DWORD code) {
    switch (code) {
    case EXCEPTION_CPP:              // C++ throw (SEH wrapper)
    case EXCEPTION_SET_THREAD_NAME: // VS thread-naming convention
    case EXCEPTION_BREAKPOINT:      // INT 3
    case EXCEPTION_SINGLE_STEP:     // Single-step / hardware bp
    case DBG_PRINTEXCEPTION_C:      // OutputDebugStringA
    case DBG_PRINTEXCEPTION_WIDE_C: // OutputDebugStringW
        return true;
    default:
        return false;
    }
}

static bool IsFatalException(DWORD code) {
    switch (code) {
    case EXCEPTION_ACCESS_VIOLATION:         // 0xC0000005
    case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:    // 0xC000008C
    case EXCEPTION_DATATYPE_MISALIGNMENT:    // 0x80000002
    case EXCEPTION_FLT_DENORMAL_OPERAND:     // 0xC000008D
    case EXCEPTION_FLT_DIVIDE_BY_ZERO:       // 0xC000008E
    case EXCEPTION_FLT_INEXACT_RESULT:       // 0xC000008F
    case EXCEPTION_FLT_INVALID_OPERATION:    // 0xC0000090
    case EXCEPTION_FLT_OVERFLOW:             // 0xC0000091
    case EXCEPTION_FLT_STACK_CHECK:          // 0xC0000092
    case EXCEPTION_FLT_UNDERFLOW:            // 0xC0000093
    case EXCEPTION_ILLEGAL_INSTRUCTION:      // 0xC000001D
    case EXCEPTION_IN_PAGE_ERROR:            // 0xC0000006
    case EXCEPTION_INT_DIVIDE_BY_ZERO:       // 0xC0000094
    case EXCEPTION_INT_OVERFLOW:             // 0xC0000095
    case EXCEPTION_INVALID_DISPOSITION:      // 0xC0000026
    case EXCEPTION_NONCONTINUABLE_EXCEPTION: // 0xC0000025
    case EXCEPTION_PRIV_INSTRUCTION:         // 0xC0000096
    case EXCEPTION_STACK_OVERFLOW:           // 0xC00000FD
        return true;
    default:
        return false;
    }
}

static bool IsGtaIntentionalTerminate(const EXCEPTION_POINTERS* ep) {
    if (ep->ExceptionRecord->ExceptionCode != EXCEPTION_ACCESS_VIOLATION)
        return false;
    if (ep->ExceptionRecord->NumberParameters < 2)
        return false;
    return (ep->ExceptionRecord->ExceptionInformation[0] == 1) &&
           (ep->ExceptionRecord->ExceptionInformation[1] == 0);
}

static const char* ExceptionCodeStr(DWORD code) {
    switch (code) {
    case EXCEPTION_ACCESS_VIOLATION:         return "ACCESS_VIOLATION";
    case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:    return "ARRAY_BOUNDS_EXCEEDED";
    case EXCEPTION_BREAKPOINT:               return "BREAKPOINT";
    case EXCEPTION_DATATYPE_MISALIGNMENT:    return "DATATYPE_MISALIGNMENT";
    case EXCEPTION_FLT_DENORMAL_OPERAND:     return "FLT_DENORMAL_OPERAND";
    case EXCEPTION_FLT_DIVIDE_BY_ZERO:       return "FLT_DIVIDE_BY_ZERO";
    case EXCEPTION_FLT_INEXACT_RESULT:       return "FLT_INEXACT_RESULT";
    case EXCEPTION_FLT_INVALID_OPERATION:    return "FLT_INVALID_OPERATION";
    case EXCEPTION_FLT_OVERFLOW:             return "FLT_OVERFLOW";
    case EXCEPTION_FLT_STACK_CHECK:          return "FLT_STACK_CHECK";
    case EXCEPTION_FLT_UNDERFLOW:            return "FLT_UNDERFLOW";
    case EXCEPTION_ILLEGAL_INSTRUCTION:      return "ILLEGAL_INSTRUCTION";
    case EXCEPTION_IN_PAGE_ERROR:            return "IN_PAGE_ERROR";
    case EXCEPTION_INT_DIVIDE_BY_ZERO:       return "INT_DIVIDE_BY_ZERO";
    case EXCEPTION_INT_OVERFLOW:             return "INT_OVERFLOW";
    case EXCEPTION_INVALID_DISPOSITION:      return "INVALID_DISPOSITION";
    case EXCEPTION_NONCONTINUABLE_EXCEPTION: return "NONCONTINUABLE_EXCEPTION";
    case EXCEPTION_PRIV_INSTRUCTION:         return "PRIV_INSTRUCTION";
    case EXCEPTION_SINGLE_STEP:              return "SINGLE_STEP";
    case EXCEPTION_STACK_OVERFLOW:           return "STACK_OVERFLOW";
    default:                                 return "UNKNOWN";
    }
}

static BOOL WriteMiniDump(EXCEPTION_POINTERS* ep, const char* dmpPath) {
    HANDLE hFile = CreateFileA(dmpPath, GENERIC_WRITE, 0, nullptr,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE)
        return FALSE;

    MINIDUMP_EXCEPTION_INFORMATION ei;
    ei.ThreadId          = GetCurrentThreadId();
    ei.ExceptionPointers = ep;
    ei.ClientPointers    = FALSE;

    const MINIDUMP_TYPE dumpType = static_cast<MINIDUMP_TYPE>(
          MiniDumpWithDataSegs
        | MiniDumpWithProcessThreadData
        | MiniDumpWithHandleData
        | MiniDumpWithModuleHeaders
        | MiniDumpWithUnloadedModules
        | MiniDumpWithThreadInfo);

    BOOL ok = MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(),
                                hFile, dumpType, &ei, nullptr, nullptr);
    if (!ok) {
        ok = MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(),
                               hFile, MiniDumpNormal, &ei, nullptr, nullptr);
    }
    CloseHandle(hFile);
    return ok;
}

static void WriteRegisters(HANDLE h, const CONTEXT* ctx) {
    RawWriteLine(h, "=== CPU REGISTERS ===");
    RawWritef(h, "  RAX=0x%016llX  RBX=0x%016llX  RCX=0x%016llX  RDX=0x%016llX",
              ctx->Rax, ctx->Rbx, ctx->Rcx, ctx->Rdx);
    RawWritef(h, "  RSI=0x%016llX  RDI=0x%016llX  RBP=0x%016llX  RSP=0x%016llX",
              ctx->Rsi, ctx->Rdi, ctx->Rbp, ctx->Rsp);
    RawWritef(h, "  R8 =0x%016llX  R9 =0x%016llX  R10=0x%016llX  R11=0x%016llX",
              ctx->R8,  ctx->R9,  ctx->R10, ctx->R11);
    RawWritef(h, "  R12=0x%016llX  R13=0x%016llX  R14=0x%016llX  R15=0x%016llX",
              ctx->R12, ctx->R13, ctx->R14, ctx->R15);
    RawWritef(h, "  RIP=0x%016llX  EFLAGS=0x%08lX", ctx->Rip, ctx->EFlags);
}

static void WriteStackTrace(HANDLE h, CONTEXT ctxCopy, bool safeToWalk) {
    RawWriteLine(h, "=== STACK TRACE ===");

    if (!safeToWalk) {
        RawWriteLine(h, "  [Stack walk skipped - unsafe for EXCEPTION_STACK_OVERFLOW]");
        return;
    }
    if (!InterlockedOr(&g_symbolsInitialized, 0)) {
        RawWriteLine(h, "  [Stack walk skipped - SymInitialize failed during startup]");
        return;
    }

    HANDLE process = GetCurrentProcess();
    HANDLE thread  = GetCurrentThread();

    STACKFRAME64 sf = {};
    sf.AddrPC.Offset    = ctxCopy.Rip;  sf.AddrPC.Mode    = AddrModeFlat;
    sf.AddrFrame.Offset = ctxCopy.Rbp;  sf.AddrFrame.Mode = AddrModeFlat;
    sf.AddrStack.Offset = ctxCopy.Rsp;  sf.AddrStack.Mode = AddrModeFlat;

    for (int i = 0; i < MAX_STACK_FRAMES; ++i) {
        if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, process, thread,
                         &sf, &ctxCopy,
                         nullptr, SymFunctionTableAccess64, SymGetModuleBase64, nullptr))
            break;

        if (sf.AddrPC.Offset == 0) break;

        char modName[MAX_PATH] = "<unknown>";
        DWORD64 modBase = SymGetModuleBase64(process, sf.AddrPC.Offset);
        if (modBase) {
            char full[MAX_PATH] = {};
            if (GetModuleFileNameA(reinterpret_cast<HMODULE>(modBase), full, MAX_PATH)) {
                const char* slash = strrchr(full, '\\');
                strncpy_s(modName, slash ? slash + 1 : full, _TRUNCATE);
            }
        }

        char symBuf[sizeof(SYMBOL_INFO) + MAX_SYM_NAME * sizeof(char)] = {};
        PSYMBOL_INFO sym = reinterpret_cast<PSYMBOL_INFO>(symBuf);
        sym->SizeOfStruct = sizeof(SYMBOL_INFO);
        sym->MaxNameLen   = MAX_SYM_NAME;

        DWORD64 displacement = 0;
        if (SymFromAddr(process, sf.AddrPC.Offset, &displacement, sym)) {
            // Source line (best effort)
            IMAGEHLP_LINE64 lineInfo = {};
            lineInfo.SizeOfStruct   = sizeof(lineInfo);
            DWORD lineDisp          = 0;
            if (SymGetLineFromAddr64(process, sf.AddrPC.Offset, &lineDisp, &lineInfo)) {
                RawWritef(h, "  [%02d] 0x%016llX  %s!%s+0x%llX  [%s:%lu]",
                          i, sf.AddrPC.Offset,
                          modName, sym->Name, displacement,
                          lineInfo.FileName, lineInfo.LineNumber);
            } else {
                RawWritef(h, "  [%02d] 0x%016llX  %s!%s+0x%llX",
                          i, sf.AddrPC.Offset, modName, sym->Name, displacement);
            }
        } else {
            RawWritef(h, "  [%02d] 0x%016llX  %s",
                      i, sf.AddrPC.Offset, modName);
        }
    }
}

static void WriteModuleList(HANDLE h) {
    RawWriteLine(h, "=== LOADED MODULES ===");

    HMODULE mods[1024];
    DWORD   needed;
    if (!EnumProcessModules(GetCurrentProcess(), mods, sizeof(mods), &needed))
        return;

    const int count = std::min(static_cast<int>(needed / sizeof(HMODULE)), 1024);
    for (int i = 0; i < count; ++i) {
        char path[MAX_PATH] = {};
        if (!GetModuleFileNameA(mods[i], path, sizeof(path))) continue;

        MODULEINFO mi;
        if (!GetModuleInformation(GetCurrentProcess(), mods[i], &mi, sizeof(mi))) continue;

        RawWritef(h, "  0x%016llX  size=0x%08X  %s",
                  reinterpret_cast<uint64_t>(mi.lpBaseOfDll), mi.SizeOfImage, path);
    }
}

static void WriteCrashReport(EXCEPTION_POINTERS* ep, const char* handlerName) {
    // Build timestamped filenames
    char timestamp[32];
    {
        SYSTEMTIME st;
        GetLocalTime(&st);
        snprintf(timestamp, sizeof(timestamp), "%04d%02d%02d_%02d%02d%02d",
                 st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    }

    char txtPath[MAX_PATH], dmpPath[MAX_PATH];
    snprintf(txtPath, sizeof(txtPath), "%s\\EVER_crash_%s.txt", g_crashDumpDir, timestamp);
    snprintf(dmpPath, sizeof(dmpPath), "%s\\EVER_crash_%s.dmp", g_crashDumpDir, timestamp);

    HANDLE h = CreateFileA(txtPath, GENERIC_WRITE, FILE_SHARE_READ,
                           nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);

    RawWriteLine(h, "========================================");
    RawWriteLine(h, "=== EVER CRASH REPORT                ===");
    RawWriteLine(h, "========================================");
    RawWritef(h, "Handler    : %s", handlerName);
    RawWritef(h, "Timestamp  : %s", timestamp);

    const DWORD code   = ep->ExceptionRecord->ExceptionCode;
    const bool  isSOF  = (code == EXCEPTION_STACK_OVERFLOW);
    const bool  isGTA  = IsGtaIntentionalTerminate(ep);

    RawWritef(h, "Exception  : 0x%08lX  %s", code, ExceptionCodeStr(code));
    RawWritef(h, "Flags      : 0x%08lX  %s", ep->ExceptionRecord->ExceptionFlags,
              (ep->ExceptionRecord->ExceptionFlags & EXCEPTION_NONCONTINUABLE)
              ? "NONCONTINUABLE" : "continuable");
    RawWritef(h, "At address : 0x%016llX",
              reinterpret_cast<uint64_t>(ep->ExceptionRecord->ExceptionAddress));

    if (code == EXCEPTION_ACCESS_VIOLATION && ep->ExceptionRecord->NumberParameters >= 2) {
        const char* kind = (ep->ExceptionRecord->ExceptionInformation[0] == 0) ? "Read" :
                           (ep->ExceptionRecord->ExceptionInformation[0] == 1) ? "Write" : "DEP";
        RawWritef(h, "AV detail  : %s of address 0x%016llX",
                  kind, ep->ExceptionRecord->ExceptionInformation[1]);
    }

    if (isGTA) {
        RawWriteLine(h, "");
        RawWriteLine(h, "*** GTA V diagTerminate() PATTERN DETECTED ***");
        RawWriteLine(h, "    This is GTA V's RAGE engine intentionally crashing the process.");
        RawWriteLine(h, "    It calls diagTerminate() which writes to address 0x0 to force a dump.");
        RawWriteLine(h, "    Root cause: likely a Quitf() / fatal RAGE assertion triggered");
        RawWriteLine(h, "    BEFORE this crash.  Check the RAGE error message below and the");
        RawWriteLine(h, "    main EVER log for the last recorded error.");
    }

    if (InterlockedOr(&g_hasRageError, 0)) {
        RawWriteLine(h, "");
        RawWriteLine(h, "=== LAST CAPTURED GTA V / RAGE ERROR MESSAGE ===");
        RawWriteLine(h, g_lastRageError);
        RawWriteLine(h, "(Captured by MessageBoxW hook before the crash.)");
    } else {
        RawWriteLine(h, "");
        RawWriteLine(h, "[No prior GTA V error message was captured]");
        RawWriteLine(h,  "  If this is a diagTerminate() crash, the MessageBoxW hook may have");
        RawWriteLine(h,  "  missed it.  Check the main EVER log for clues.");
    }

    {
        SYSTEM_INFO si;     GetSystemInfo(&si);
        MEMORYSTATUSEX ms;  ms.dwLength = sizeof(ms);  GlobalMemoryStatusEx(&ms);
        RawWriteLine(h, "");
        RawWriteLine(h, "=== SYSTEM INFO ===");
        RawWritef(h, "  Processors   : %lu", si.dwNumberOfProcessors);
        RawWritef(h, "  Physical RAM : %llu MB", ms.ullTotalPhys  / (1024*1024));
        RawWritef(h, "  Available RAM: %llu MB", ms.ullAvailPhys  / (1024*1024));
        RawWritef(h, "  Memory load  : %lu%%",   ms.dwMemoryLoad);
    }

    RawWriteLine(h, "");
    WriteRegisters(h, ep->ContextRecord);

    RawWriteLine(h, "");
    WriteStackTrace(h, *ep->ContextRecord, !isSOF);

    RawWriteLine(h, "");
    WriteModuleList(h);

    RawWriteLine(h, "");
    RawWriteLine(h, "========================================");
    RawWritef(h, "Crash text : %s", txtPath);
    RawWritef(h, "Crash dump : %s", dmpPath);
    RawWriteLine(h, "Please attach BOTH files when reporting this crash.");
    RawWriteLine(h, "========================================");

    if (!isSOF) {
        BOOL dmpOk = WriteMiniDump(ep, dmpPath);
        if (dmpOk) {
            RawWritef(h, "Minidump  : %s  (load in WinDbg/VS for full analysis)", dmpPath);
        } else {
            RawWritef(h, "[!] MiniDumpWriteDump failed – no .dmp file written");
        }
    } else {
        RawWriteLine(h, "[Minidump skipped for EXCEPTION_STACK_OVERFLOW]");
    }

    if (h != INVALID_HANDLE_VALUE) {
        FlushFileBuffers(h);
        CloseHandle(h);
        h = INVALID_HANDLE_VALUE;
    }

    if (g_mainLogPath[0]) {
        HANDLE hMain = CreateFileA(g_mainLogPath,
                                   GENERIC_WRITE,
                                   FILE_SHARE_READ | FILE_SHARE_WRITE,
                                   nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (hMain != INVALID_HANDLE_VALUE) {
            SetFilePointer(hMain, 0, nullptr, FILE_END);
            RawWriteLine(hMain, "");
            RawWriteLine(hMain, "========================================");
            RawWriteLine(hMain, "=== EVER CRASH HANDLER FIRED         ===");
            RawWriteLine(hMain, "========================================");
            RawWritef(hMain, "Handler   : %s", handlerName);
            RawWritef(hMain, "Exception : 0x%08lX  %s", code, ExceptionCodeStr(code));
            RawWritef(hMain, "At address: 0x%016llX",
                      reinterpret_cast<uint64_t>(ep->ExceptionRecord->ExceptionAddress));
            if (isGTA)
                RawWriteLine(hMain, "*** GTA V diagTerminate() pattern detected ***");
            if (InterlockedOr(&g_hasRageError, 0))
                RawWritef(hMain, "RAGE error: %s", g_lastRageError);
            RawWritef(hMain, "Full crash report written to: %s", txtPath);
            RawWriteLine(hMain, "========================================");
            FlushFileBuffers(hMain);
            CloseHandle(hMain);
        }
    }
}

static bool IsExceptionFromBlacklistedModule(void* exceptionAddress) {
    HMODULE hModule = nullptr;
    if (!GetModuleHandleExA(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
            GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCSTR>(exceptionAddress),
            &hModule)) {
        return false;
    }

    char modulePath[MAX_PATH] = {};
    if (!GetModuleFileNameA(hModule, modulePath, MAX_PATH))
        return false;

    for (char* p = modulePath; *p; ++p)
        *p = static_cast<char>(::tolower(static_cast<unsigned char>(*p)));

    // Modules known to use intentional SEH exceptions (false-positive source):
    //   adhesive.dll     – FiveM anti-cheat / DRM integrity checks
    //   socialclub.dll   – Rockstar Social Club (RPC / COM exception idiom)
    //   citizen-core.dll – FiveM core runtime
    static const char* const kBlacklist[] = {
        "adhesive.dll",
        "socialclub.dll",
        "citizen-core.dll",
        nullptr
    };

    for (const char* const* entry = kBlacklist; *entry; ++entry) {
        if (::strstr(modulePath, *entry))
            return true;
    }
    return false;
}

static LONG WINAPI VectoredCrashHandler(EXCEPTION_POINTERS* ep) {
    if (!IsFatalException(ep->ExceptionRecord->ExceptionCode))
        return EXCEPTION_CONTINUE_SEARCH;

    if (IsExceptionFromBlacklistedModule(ep->ExceptionRecord->ExceptionAddress))
        return EXCEPTION_CONTINUE_SEARCH;

    if (InterlockedCompareExchange(&g_crashInProgress, 1L, 0L) != 0L)
        return EXCEPTION_CONTINUE_SEARCH;

    WriteCrashReport(ep, "VEH (Vectored Exception Handler - primary, cannot be bypassed by GTA V)");

    InterlockedExchange(&g_crashInProgress, 0L);
    return EXCEPTION_CONTINUE_SEARCH;  // Let GTA's handler also run
}

static LONG WINAPI UnhandledCrashHandler(EXCEPTION_POINTERS* ep) {
    if (IsNonFatalException(ep->ExceptionRecord->ExceptionCode)) {
        return g_previousExceptionFilter
               ? g_previousExceptionFilter(ep) : EXCEPTION_CONTINUE_SEARCH;
    }

    if (InterlockedCompareExchange(&g_crashInProgress, 1L, 0L) == 0L) {
        WriteCrashReport(ep, "UEF (Unhandled Exception Filter - secondary fallback)");
        InterlockedExchange(&g_crashInProgress, 0L);
    }

    return g_previousExceptionFilter
           ? g_previousExceptionFilter(ep) : EXCEPTION_CONTINUE_SEARCH;
}

static void EverTerminateHandler() {
    CONTEXT ctx = {};
    ctx.ContextFlags = CONTEXT_ALL;
    RtlCaptureContext(&ctx);

    EXCEPTION_RECORD er = {};
    er.ExceptionCode    = 0xC0000029UL; // STATUS_INVALID_UNWIND_TARGET
    er.ExceptionAddress = reinterpret_cast<void*>(ctx.Rip);
    er.ExceptionFlags   = EXCEPTION_NONCONTINUABLE;

    EXCEPTION_POINTERS ep;
    ep.ExceptionRecord = &er;
    ep.ContextRecord   = &ctx;

    // Try to extract the exception message
    char cppMsg[512] = "[unhandled C++ exception – no message available]";
    try {
        std::rethrow_exception(std::current_exception());
    } catch (const std::exception& ex) {
        snprintf(cppMsg, sizeof(cppMsg), "std::exception: %s", ex.what());
    } catch (...) {
        strncpy_s(cppMsg, "[non-std::exception type]", _TRUNCATE);
    }

    ever::crash::RecordRageErrorMessage(cppMsg);

    if (InterlockedCompareExchange(&g_crashInProgress, 1L, 0L) == 0L) {
        WriteCrashReport(&ep, "std::terminate (unhandled C++ exception)");
        InterlockedExchange(&g_crashInProgress, 0L);
    }

    if (g_prevTerminateHandler) g_prevTerminateHandler();
    else std::abort();
}

}

namespace ever {
namespace crash {

void RecordRageErrorMessage(const char* msg) {
    if (!msg || !*msg) return;
    strncpy_s(g_lastRageError, sizeof(g_lastRageError), msg, _TRUNCATE);
    InterlockedExchange(&g_hasRageError, 1L);
    // Best-effort log via the normal Logger (might deadlock very rarely, but
    // this is called from the MessageBoxW hook, not from a crash handler).
    LOG(LL_ERR, "[RAGE/GTA V error message captured before crash]: ", g_lastRageError);
}

void Initialize() {
    LOG(LL_NFO, "=== Installing EVER crash handlers ===");

    const std::string base = AsiPath() + "\\EVER";
    snprintf(g_crashDumpDir, sizeof(g_crashDumpDir), "%s\\crashes", base.c_str());
    snprintf(g_mainLogPath,  sizeof(g_mainLogPath),  "%s\\" TARGET_NAME ".log", base.c_str());
    CreateDirectoryA(base.c_str(), nullptr);
    CreateDirectoryA(g_crashDumpDir, nullptr);
    LOG(LL_NFO, "Crash reports will be written to: ", g_crashDumpDir);

    SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
    if (SymInitialize(GetCurrentProcess(), nullptr, TRUE)) {
        InterlockedExchange(&g_symbolsInitialized, 1L);
        LOG(LL_NFO, "DbgHelp symbol engine initialised (symbols load on demand)");
    } else {
        LOG(LL_WRN, "SymInitialize failed (error ", GetLastError(), ") – "
            "stack traces will show only addresses");
    }

    g_vehHandle = AddVectoredExceptionHandler(1, VectoredCrashHandler);
    if (g_vehHandle) {
        LOG(LL_NFO, "Vectored Exception Handler (VEH) installed - "
            "fires before GTA V's handler and cannot be overridden by it");
    } else {
        LOG(LL_ERR, "AddVectoredExceptionHandler FAILED (error ", GetLastError(), ") - "
            "crash handler will rely on UEF fallback only!");
    }

    g_previousExceptionFilter = SetUnhandledExceptionFilter(UnhandledCrashHandler);
    if (g_previousExceptionFilter) {
        LOG(LL_DBG, "Previous UEF saved: 0x",
            Logger::hex(reinterpret_cast<uint64_t>(g_previousExceptionFilter), 16),
            " (GTA V will overwrite this UEF slot later – VEH is the real guard)");
    }
    LOG(LL_NFO, "Unhandled Exception Filter installed as secondary fallback");

    g_prevTerminateHandler = std::set_terminate(EverTerminateHandler);
    LOG(LL_NFO, "std::terminate handler installed (catches unhandled C++ exceptions)");

    LOG(LL_NFO, "=== EVER crash handlers installation complete ===");
}

void Cleanup() {
    LOG(LL_NFO, "Removing EVER crash handlers");

    if (g_vehHandle) {
        RemoveVectoredExceptionHandler(g_vehHandle);
        g_vehHandle = nullptr;
        LOG(LL_DBG, "Vectored Exception Handler removed");
    }

    SetUnhandledExceptionFilter(g_previousExceptionFilter);
    g_previousExceptionFilter = nullptr;

    if (g_prevTerminateHandler) {
        std::set_terminate(g_prevTerminateHandler);
        g_prevTerminateHandler = nullptr;
    }

    if (InterlockedExchange(&g_symbolsInitialized, 0L)) {
        SymCleanup(GetCurrentProcess());
        LOG(LL_DBG, "DbgHelp symbols cleaned up");
    }

    LOG(LL_NFO, "EVER crash handlers removed");
}

}
}
