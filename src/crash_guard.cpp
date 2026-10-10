

#include "mod.hpp"
#include "print.hpp"

const char* volatile g_coopStage = "";

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <csignal>
#include <cstdio>
#include <cstring>
#include <exception>
#include <filesystem>
#include <string>

namespace {

HANDLE s_file = INVALID_HANDLE_VALUE;
PVOID s_veh = nullptr;
std::terminate_handler s_prevTerminate = nullptr;
void (*s_prevAbort)(int) = nullptr;
volatile LONG s_written = 0;

void put(const char* text) {
    if (s_file == INVALID_HANDLE_VALUE) return;
    DWORD n = 0;
    WriteFile(s_file, text, static_cast<DWORD>(std::strlen(text)), &n, nullptr);
}

void put_frame(int i, void* pc) {
    char line[320];
    HMODULE mod = nullptr;
    char path[MAX_PATH] = {};
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            static_cast<LPCSTR>(pc), &mod) &&
        GetModuleFileNameA(mod, path, MAX_PATH) != 0) {
        const char* name = std::strrchr(path, '\\');
        name = name != nullptr ? name + 1 : path;

        HMODULE ours = nullptr;
        GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCSTR>(&put_frame), &ours);
        std::snprintf(line, sizeof(line), "  #%02d %s+0x%llx\n", i, mod == ours ? "coop_mod" : name,
            static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(pc) - reinterpret_cast<uintptr_t>(mod)));
    } else {
        std::snprintf(line, sizeof(line), "  #%02d 0x%p\n", i, pc);
    }
    put(line);
}

void report(const char* what, void* pc, unsigned long code) {
    if (InterlockedExchange(&s_written, 1) != 0) return;
    char line[200];
    const char* stage = g_coopStage;
    std::snprintf(line, sizeof(line), "CRASH %s code=0x%08lx stage=%s\n", what, code,
        stage != nullptr ? stage : "");
    put(line);
    if (pc != nullptr) put_frame(-1, pc);
    void* frames[40];
    const USHORT n = RtlCaptureStackBackTrace(1, 40, frames, nullptr);
    for (USHORT i = 0; i < n; ++i) put_frame(i, frames[i]);
    FlushFileBuffers(s_file);
}

bool pc_is_ours(void* pc) {
    HMODULE mod = nullptr;
    HMODULE ours = nullptr;
    return GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
               static_cast<LPCSTR>(pc), &mod) &&
           GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
               reinterpret_cast<LPCSTR>(&put_frame), &ours) &&
           mod == ours;
}

LONG CALLBACK on_exception(EXCEPTION_POINTERS* info) {
    const DWORD code = info->ExceptionRecord->ExceptionCode;

    if (code == EXCEPTION_STACK_OVERFLOW) {
        report("stack overflow", info->ExceptionRecord->ExceptionAddress, code);
    } else if (code == 0xC0000374 || code == 0xC0000409) {
        report(code == 0xC0000374 ? "heap corrupted" : "fast fail", info->ExceptionRecord->ExceptionAddress, code);
    } else if (code == EXCEPTION_ACCESS_VIOLATION && pc_is_ours(info->ExceptionRecord->ExceptionAddress)) {

        char line[96];
        std::snprintf(line, sizeof(line), "fault at 0x%llx", static_cast<unsigned long long>(
            info->ExceptionRecord->NumberParameters >= 2 ? info->ExceptionRecord->ExceptionInformation[1] : 0));
        report(line, info->ExceptionRecord->ExceptionAddress, code);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

void on_abort(int sig) {
    report("abort", nullptr, static_cast<unsigned long>(sig));
    std::signal(SIGABRT, s_prevAbort != nullptr && s_prevAbort != SIG_ERR ? s_prevAbort : SIG_DFL);
    std::raise(SIGABRT);
}

void on_terminate() {
    report("terminate (uncaught C++ exception)", nullptr, 0);
    if (s_prevTerminate != nullptr) s_prevTerminate();
    std::abort();
}

}

void crash_guard_init() {
    if (s_file != INVALID_HANDLE_VALUE) return;
    const std::wstring path = std::filesystem::u8path(coop_crash_trail_path()).wstring();
    s_file = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);

    ULONG guarantee = 64 * 1024;
    SetThreadStackGuarantee(&guarantee);
    s_veh = AddVectoredExceptionHandler(1, on_exception);
    s_prevAbort = std::signal(SIGABRT, on_abort);
    s_prevTerminate = std::set_terminate(on_terminate);
    coop_log::info("coop_mod: [CRASH] last-words handler {}", s_file != INVALID_HANDLE_VALUE ? "ready" : "has no file");
}

#else

void crash_guard_init() {}

#endif
