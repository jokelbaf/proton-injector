#include <windows.h>
#include <tlhelp32.h>
#include "inject.h"
#include "logger.h"

typedef HMODULE (WINAPI *LoadLibraryW_t)(LPCWSTR);
typedef HMODULE (WINAPI *LoadLibraryA_t)(LPCSTR);
typedef NTSTATUS (NTAPI *NtCreateThreadEx_t)(
    PHANDLE ThreadHandle,
    ACCESS_MASK DesiredAccess,
    PVOID ObjectAttributes,
    HANDLE ProcessHandle,
    PVOID StartRoutine,
    PVOID Argument,
    ULONG CreateFlags,
    SIZE_T ZeroBits,
    SIZE_T StackSize,
    SIZE_T MaximumStackSize,
    PVOID AttributeList
);

static BOOL inject_standard(HANDLE process, const wchar_t *dll_path) {
    SIZE_T dll_path_size = (wcslen(dll_path) + 1) * sizeof(wchar_t);
    
    LPVOID remote_dll_path = VirtualAllocEx(
        process,
        NULL,
        dll_path_size,
        MEM_COMMIT | MEM_RESERVE,
        PAGE_READWRITE
    );

    if (!remote_dll_path) {
        LOG_ERROR(L"VirtualAllocEx failed: %lu", GetLastError());
        return FALSE;
    }

    LOG_DEBUG(L"Allocated memory at 0x%p", remote_dll_path);

    if (!WriteProcessMemory(process, remote_dll_path, dll_path, dll_path_size, NULL)) {
        LOG_ERROR(L"WriteProcessMemory failed: %lu", GetLastError());
        VirtualFreeEx(process, remote_dll_path, 0, MEM_RELEASE);
        return FALSE;
    }

    LOG_DEBUG(L"DLL path written to remote process");

    HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll");
    if (!kernel32) {
        LOG_ERROR(L"Failed to get kernel32.dll handle");
        VirtualFreeEx(process, remote_dll_path, 0, MEM_RELEASE);
        return FALSE;
    }

    LoadLibraryW_t load_library_addr = (LoadLibraryW_t)GetProcAddress(kernel32, "LoadLibraryW");
    if (!load_library_addr) {
        LOG_ERROR(L"Failed to get LoadLibraryW address");
        VirtualFreeEx(process, remote_dll_path, 0, MEM_RELEASE);
        return FALSE;
    }

    LOG_DEBUG(L"LoadLibraryW address: 0x%p", load_library_addr);

    HANDLE remote_thread = CreateRemoteThread(
        process,
        NULL,
        0,
        (LPTHREAD_START_ROUTINE)load_library_addr,
        remote_dll_path,
        0,
        NULL
    );

    if (!remote_thread) {
        LOG_ERROR(L"CreateRemoteThread failed: %lu", GetLastError());
        VirtualFreeEx(process, remote_dll_path, 0, MEM_RELEASE);
        return FALSE;
    }

    LOG_DEBUG(L"Remote thread created");

    WaitForSingleObject(remote_thread, INFINITE);

    DWORD exit_code;
    GetExitCodeThread(remote_thread, &exit_code);
    
    CloseHandle(remote_thread);
    VirtualFreeEx(process, remote_dll_path, 0, MEM_RELEASE);

    if (!exit_code) {
        LOG_ERROR(L"LoadLibraryW returned NULL");
        return FALSE;
    }

    LOG_DEBUG(L"DLL loaded at 0x%08X", exit_code);
    return TRUE;
}

static BOOL inject_apc(HANDLE process, HANDLE thread, const wchar_t *dll_path) {
    char dll_path_ansi[MAX_PATH];
    WideCharToMultiByte(CP_ACP, 0, dll_path, -1, dll_path_ansi, MAX_PATH, NULL, NULL);
    
    SIZE_T dll_path_size = strlen(dll_path_ansi) + 1;
    
    LPVOID remote_dll_path = VirtualAllocEx(
        process,
        NULL,
        dll_path_size,
        MEM_COMMIT | MEM_RESERVE,
        PAGE_READWRITE
    );

    if (!remote_dll_path) {
        LOG_ERROR(L"VirtualAllocEx failed: %lu", GetLastError());
        return FALSE;
    }

    LOG_DEBUG(L"Allocated memory at 0x%p", remote_dll_path);

    if (!WriteProcessMemory(process, remote_dll_path, dll_path_ansi, dll_path_size, NULL)) {
        LOG_ERROR(L"WriteProcessMemory failed: %lu", GetLastError());
        VirtualFreeEx(process, remote_dll_path, 0, MEM_RELEASE);
        return FALSE;
    }

    LOG_DEBUG(L"DLL path written to remote process");

    HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll");
    if (!kernel32) {
        LOG_ERROR(L"Failed to get kernel32.dll handle");
        VirtualFreeEx(process, remote_dll_path, 0, MEM_RELEASE);
        return FALSE;
    }

    LoadLibraryA_t load_library_addr = (LoadLibraryA_t)GetProcAddress(kernel32, "LoadLibraryA");
    if (!load_library_addr) {
        LOG_ERROR(L"Failed to get LoadLibraryA address");
        VirtualFreeEx(process, remote_dll_path, 0, MEM_RELEASE);
        return FALSE;
    }

    LOG_DEBUG(L"LoadLibraryA address: 0x%p", load_library_addr);

    if (!QueueUserAPC((PAPCFUNC)load_library_addr, thread, (ULONG_PTR)remote_dll_path)) {
        LOG_ERROR(L"QueueUserAPC failed: %lu", GetLastError());
        VirtualFreeEx(process, remote_dll_path, 0, MEM_RELEASE);
        return FALSE;
    }

    LOG_DEBUG(L"APC queued successfully");
    
    return TRUE;
}

static BOOL inject_nt(HANDLE process, const wchar_t *dll_path) {
    char dll_path_ansi[MAX_PATH];
    WideCharToMultiByte(CP_ACP, 0, dll_path, -1, dll_path_ansi, MAX_PATH, NULL, NULL);
    
    SIZE_T dll_path_size = strlen(dll_path_ansi) + 1;
    
    LPVOID remote_dll_path = VirtualAllocEx(
        process,
        NULL,
        dll_path_size,
        MEM_COMMIT | MEM_RESERVE,
        PAGE_READWRITE
    );

    if (!remote_dll_path) {
        LOG_ERROR(L"VirtualAllocEx failed: %lu", GetLastError());
        return FALSE;
    }

    LOG_DEBUG(L"Allocated memory at 0x%p", remote_dll_path);

    if (!WriteProcessMemory(process, remote_dll_path, dll_path_ansi, dll_path_size, NULL)) {
        LOG_ERROR(L"WriteProcessMemory failed: %lu", GetLastError());
        VirtualFreeEx(process, remote_dll_path, 0, MEM_RELEASE);
        return FALSE;
    }

    LOG_DEBUG(L"DLL path written to remote process");

    HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll");
    if (!kernel32) {
        LOG_ERROR(L"Failed to get kernel32.dll handle");
        VirtualFreeEx(process, remote_dll_path, 0, MEM_RELEASE);
        return FALSE;
    }

    LoadLibraryA_t load_library_addr = (LoadLibraryA_t)GetProcAddress(kernel32, "LoadLibraryA");
    if (!load_library_addr) {
        LOG_ERROR(L"Failed to get LoadLibraryA address");
        VirtualFreeEx(process, remote_dll_path, 0, MEM_RELEASE);
        return FALSE;
    }

    LOG_DEBUG(L"LoadLibraryA address: 0x%p", load_library_addr);

    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (!ntdll) {
        LOG_ERROR(L"Failed to get ntdll.dll handle");
        VirtualFreeEx(process, remote_dll_path, 0, MEM_RELEASE);
        return FALSE;
    }

    NtCreateThreadEx_t NtCreateThreadEx = (NtCreateThreadEx_t)GetProcAddress(ntdll, "NtCreateThreadEx");
    if (!NtCreateThreadEx) {
        LOG_ERROR(L"Failed to get NtCreateThreadEx address");
        VirtualFreeEx(process, remote_dll_path, 0, MEM_RELEASE);
        return FALSE;
    }

    LOG_DEBUG(L"NtCreateThreadEx address: 0x%p", NtCreateThreadEx);

    HANDLE remote_thread = NULL;
    NTSTATUS status = NtCreateThreadEx(
        &remote_thread,
        THREAD_ALL_ACCESS,
        NULL,
        process,
        (PVOID)load_library_addr,
        remote_dll_path,
        0,
        0,
        0,
        0,
        NULL
    );

    if (status != 0 || !remote_thread) {
        LOG_ERROR(L"NtCreateThreadEx failed: 0x%08X", status);
        VirtualFreeEx(process, remote_dll_path, 0, MEM_RELEASE);
        return FALSE;
    }

    LOG_DEBUG(L"Remote thread created via NtCreateThreadEx");

    WaitForSingleObject(remote_thread, INFINITE);

    DWORD exit_code;
    GetExitCodeThread(remote_thread, &exit_code);
    
    CloseHandle(remote_thread);
    VirtualFreeEx(process, remote_dll_path, 0, MEM_RELEASE);

    if (!exit_code) {
        LOG_ERROR(L"LoadLibraryA returned NULL");
        return FALSE;
    }

    LOG_DEBUG(L"DLL loaded at 0x%08X", exit_code);
    return TRUE;
}

static DWORD find_main_thread(DWORD pid) {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return 0;
    }

    THREADENTRY32 te = {0};
    te.dwSize = sizeof(te);

    DWORD main_thread_id = 0;
    FILETIME earliest_time = {0xFFFFFFFF, 0xFFFFFFFF};

    if (Thread32First(snapshot, &te)) {
        do {
            if (te.th32OwnerProcessID == pid) {
                HANDLE thread = OpenThread(THREAD_QUERY_INFORMATION, FALSE, te.th32ThreadID);
                if (thread) {
                    FILETIME creation_time, exit_time, kernel_time, user_time;
                    if (GetThreadTimes(thread, &creation_time, &exit_time, &kernel_time, &user_time)) {
                        if (CompareFileTime(&creation_time, &earliest_time) < 0) {
                            earliest_time = creation_time;
                            main_thread_id = te.th32ThreadID;
                        }
                    }
                    CloseHandle(thread);
                }
            }
        } while (Thread32Next(snapshot, &te));
    }

    CloseHandle(snapshot);
    return main_thread_id;
}

static BOOL inject_hook(DWORD pid, const wchar_t *dll_path) {
    char dll_path_ansi[MAX_PATH];
    WideCharToMultiByte(CP_ACP, 0, dll_path, -1, dll_path_ansi, MAX_PATH, NULL, NULL);

    HMODULE dll = LoadLibraryA(dll_path_ansi);
    if (!dll) {
        LOG_ERROR(L"Failed to load DLL locally: %lu", GetLastError());
        return FALSE;
    }

    LOG_DEBUG(L"DLL loaded locally at 0x%p", dll);

    DWORD main_thread_id = find_main_thread(pid);
    if (!main_thread_id) {
        LOG_ERROR(L"Failed to find main thread");
        FreeLibrary(dll);
        return FALSE;
    }

    LOG_DEBUG(L"Main thread ID: %lu", main_thread_id);

    HHOOK hook = SetWindowsHookExA(WH_CBT, (HOOKPROC)GetProcAddress(dll, "DllMain"), dll, main_thread_id);
    if (!hook) {
        LOG_ERROR(L"SetWindowsHookExA failed: %lu", GetLastError());
        FreeLibrary(dll);
        return FALSE;
    }

    LOG_DEBUG(L"Hook installed successfully");

    PostThreadMessage(main_thread_id, WM_NULL, 0, 0);

    Sleep(100);

    UnhookWindowsHookEx(hook);
    
    LOG_DEBUG(L"Hook removed");
    
    return TRUE;
}

BOOL inject_dll(HANDLE process, HANDLE thread, DWORD pid, const wchar_t *dll_path, InjectionMethod method) {
    LOG_INFO(L"Using injection method: %d", method);
    
    switch (method) {
        case INJECTION_STANDARD:
            return inject_standard(process, dll_path);
        
        case INJECTION_APC:
            return inject_apc(process, thread, dll_path);
        
        case INJECTION_NT:
            return inject_nt(process, dll_path);
        
        case INJECTION_HOOK:
            return inject_hook(pid, dll_path);
        
        default:
            LOG_ERROR(L"Unknown injection method: %d", method);
            return FALSE;
    }
}
