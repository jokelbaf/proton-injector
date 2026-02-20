#pragma once

#include <windows.h>

typedef enum {
    INJECTION_STANDARD,
    INJECTION_APC,
    INJECTION_NT,
    INJECTION_HOOK
} InjectionMethod;

BOOL wait_for_process_init(HANDLE process, int timeout_ms);
BOOL inject_dll(HANDLE process, HANDLE thread, DWORD pid, const wchar_t *dll_path, InjectionMethod method);
