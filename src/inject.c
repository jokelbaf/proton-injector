#include <windows.h>
#include <tlhelp32.h>
#include <stddef.h>
#include <string.h>
#include "inject.h"
#include "logger.h"

typedef NTSTATUS (NTAPI *NtCreateThreadEx_t)(
    PHANDLE, ACCESS_MASK, PVOID, HANDLE, PVOID,
    PVOID, ULONG, SIZE_T, SIZE_T, SIZE_T, PVOID
);

typedef NTSTATUS (NTAPI *NtQueryInformationProcess_t)(
    HANDLE, ULONG, PVOID, ULONG, PULONG
);

typedef NTSTATUS (NTAPI *NtQueryVirtualMemory_t)(
    HANDLE, PVOID, ULONG, PVOID, SIZE_T, PSIZE_T
);

typedef struct {
    USHORT Length;
    USHORT MaximumLength;
    WCHAR  Buffer[512];
} MY_MEMORY_SECTION_NAME;

typedef struct {
    USHORT Length;
    USHORT MaximumLength;
    PWSTR  Buffer;
} MY_UNICODE_STRING;

typedef struct {
    ULONG      Length;
    BOOLEAN    Initialized;
    PVOID      SsHandle;
    LIST_ENTRY InLoadOrderModuleList;
    LIST_ENTRY InMemoryOrderModuleList;
    LIST_ENTRY InInitializationOrderModuleList;
} MY_PEB_LDR_DATA;

typedef struct {
    LIST_ENTRY        InLoadOrderLinks;
    LIST_ENTRY        InMemoryOrderLinks;
    LIST_ENTRY        InInitializationOrderLinks;
    PVOID             DllBase;
    PVOID             EntryPoint;
    ULONG             SizeOfImage;
    MY_UNICODE_STRING FullDllName;
    MY_UNICODE_STRING BaseDllName;
} MY_LDR_DATA_TABLE_ENTRY;

typedef struct {
    BYTE  Reserved1[2];
    BYTE  BeingDebugged;
    BYTE  Reserved2[1];
    PVOID Reserved3[2];
    PVOID Ldr;
} MY_PEB;

typedef struct {
    NTSTATUS  ExitStatus;
    PVOID     PebBaseAddress;
    ULONG_PTR AffinityMask;
    LONG      BasePriority;
    ULONG_PTR UniqueProcessId;
    ULONG_PTR InheritedFromUniqueProcessId;
} MY_PROCESS_BASIC_INFORMATION;


static HMODULE find_remote_module_peb(HANDLE process, const wchar_t *module_name) {
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (!ntdll) return NULL;

    NtQueryInformationProcess_t NtQIP =
        (NtQueryInformationProcess_t)GetProcAddress(ntdll, "NtQueryInformationProcess");
    if (!NtQIP) return NULL;

    MY_PROCESS_BASIC_INFORMATION pbi = {0};
    if (NtQIP(process, 0, &pbi, sizeof(pbi), NULL) != 0 || !pbi.PebBaseAddress)
        return NULL;

    PVOID ldr_ptr = NULL;
    if (!ReadProcessMemory(process, (BYTE*)pbi.PebBaseAddress + offsetof(MY_PEB, Ldr),
                           &ldr_ptr, sizeof(ldr_ptr), NULL) || !ldr_ptr)
        return NULL;

    LIST_ENTRY list_head = {0};
    BYTE *list_head_addr = (BYTE*)ldr_ptr + offsetof(MY_PEB_LDR_DATA, InMemoryOrderModuleList);
    if (!ReadProcessMemory(process, list_head_addr, &list_head, sizeof(list_head), NULL))
        return NULL;

    LIST_ENTRY *current = list_head.Flink;

    for (int i = 0; (PVOID)current != (PVOID)list_head_addr && i < 512; i++) {
        BYTE *entry_addr = (BYTE*)current - offsetof(MY_LDR_DATA_TABLE_ENTRY, InMemoryOrderLinks);
        MY_LDR_DATA_TABLE_ENTRY entry = {0};
        if (!ReadProcessMemory(process, entry_addr, &entry, sizeof(entry), NULL))
            break;

        if (entry.BaseDllName.Length > 0 && entry.BaseDllName.Length < 520 && entry.BaseDllName.Buffer) {
            wchar_t name_buf[260] = {0};
            USHORT name_len = entry.BaseDllName.Length;
            if (name_len > sizeof(name_buf) - sizeof(wchar_t))
                name_len = sizeof(name_buf) - sizeof(wchar_t);

            if (ReadProcessMemory(process, entry.BaseDllName.Buffer, name_buf, name_len, NULL)) {
                name_buf[name_len / sizeof(wchar_t)] = L'\0';
                if (_wcsicmp(name_buf, module_name) == 0)
                    return (HMODULE)entry.DllBase;
            }
        }

        current = entry.InMemoryOrderLinks.Flink;
    }

    return NULL;
}

static HMODULE find_remote_module_vmquery(HANDLE process, const wchar_t *module_name) {
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    NtQueryVirtualMemory_t NtQVM = NULL;
    if (ntdll)
        NtQVM = (NtQueryVirtualMemory_t)GetProcAddress(ntdll, "NtQueryVirtualMemory");

    BYTE *addr = NULL;
    MEMORY_BASIC_INFORMATION mbi;

    while (VirtualQueryEx(process, addr, &mbi, sizeof(mbi)) == sizeof(mbi)) {
        if (mbi.Type == MEM_IMAGE && mbi.BaseAddress == mbi.AllocationBase &&
            mbi.State == MEM_COMMIT) {

            HMODULE candidate = (HMODULE)mbi.BaseAddress;
            IMAGE_DOS_HEADER dos_hdr = {0};

            if (ReadProcessMemory(process, (PVOID)candidate, &dos_hdr, sizeof(dos_hdr), NULL) &&
                dos_hdr.e_magic == IMAGE_DOS_SIGNATURE) {

                IMAGE_NT_HEADERS nt_hdr = {0};
                if (ReadProcessMemory(process, (BYTE*)candidate + dos_hdr.e_lfanew,
                                      &nt_hdr, sizeof(nt_hdr), NULL) &&
                    nt_hdr.Signature == IMAGE_NT_SIGNATURE) {

                    IMAGE_DATA_DIRECTORY exp_dir =
                        nt_hdr.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];

                    if (exp_dir.VirtualAddress && exp_dir.Size) {
                        IMAGE_EXPORT_DIRECTORY exp = {0};
                        if (ReadProcessMemory(process, (BYTE*)candidate + exp_dir.VirtualAddress,
                                              &exp, sizeof(exp), NULL) && exp.Name) {
                            char pe_name[260] = {0};
                            if (ReadProcessMemory(process, (BYTE*)candidate + exp.Name,
                                                  pe_name, sizeof(pe_name) - 1, NULL)) {
                                wchar_t pe_name_w[260] = {0};
                                MultiByteToWideChar(CP_ACP, 0, pe_name, -1, pe_name_w, 260);
                                if (_wcsicmp(pe_name_w, module_name) == 0)
                                    return candidate;
                            }
                        }
                    }
                }
            }

            if (NtQVM) {
                MY_MEMORY_SECTION_NAME sec_name = {0};
                if (NtQVM(process, mbi.BaseAddress, 2, &sec_name, sizeof(sec_name), NULL) == 0 &&
                    sec_name.Length > 0) {
                    sec_name.Buffer[sec_name.Length / sizeof(WCHAR)] = L'\0';
                    wchar_t *filename = wcsrchr(sec_name.Buffer, L'\\');
                    filename = filename ? filename + 1 : sec_name.Buffer;
                    if (_wcsicmp(filename, module_name) == 0)
                        return candidate;
                }
            }
        }

        addr = (BYTE*)mbi.BaseAddress + mbi.RegionSize;
        if (addr <= (BYTE*)mbi.BaseAddress) break;
    }

    return NULL;
}

static HMODULE find_remote_module_toolhelp(DWORD pid, const wchar_t *module_name) {
    HANDLE snapshot = INVALID_HANDLE_VALUE;

    for (int i = 0; i < 5; i++) {
        snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
        if (snapshot != INVALID_HANDLE_VALUE) break;
        Sleep(50);
    }

    if (snapshot == INVALID_HANDLE_VALUE)
        return NULL;

    MODULEENTRY32W me = {0};
    me.dwSize = sizeof(me);
    HMODULE result = NULL;

    if (Module32FirstW(snapshot, &me)) {
        do {
            if (_wcsicmp(me.szModule, module_name) == 0) {
                result = (HMODULE)me.modBaseAddr;
                break;
            }
        } while (Module32NextW(snapshot, &me));
    }

    CloseHandle(snapshot);
    return result;
}

static FARPROC find_export_in_remote_pe(HANDLE process, HMODULE remote_base, const char *func_name) {
    IMAGE_DOS_HEADER dos_header = {0};
    if (!ReadProcessMemory(process, (PVOID)remote_base, &dos_header, sizeof(dos_header), NULL) ||
        dos_header.e_magic != IMAGE_DOS_SIGNATURE)
        return NULL;

    IMAGE_NT_HEADERS nt_headers = {0};
    if (!ReadProcessMemory(process, (BYTE*)remote_base + dos_header.e_lfanew,
                           &nt_headers, sizeof(nt_headers), NULL) ||
        nt_headers.Signature != IMAGE_NT_SIGNATURE)
        return NULL;

    IMAGE_DATA_DIRECTORY export_data_dir =
        nt_headers.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    if (!export_data_dir.VirtualAddress || !export_data_dir.Size)
        return NULL;

    IMAGE_EXPORT_DIRECTORY export_dir = {0};
    if (!ReadProcessMemory(process, (BYTE*)remote_base + export_data_dir.VirtualAddress,
                           &export_dir, sizeof(export_dir), NULL))
        return NULL;

    if (export_dir.NumberOfNames == 0 || export_dir.NumberOfNames > 0x10000)
        return NULL;

    DWORD *name_rvas = NULL;
    WORD  *ordinals  = NULL;
    DWORD *func_rvas = NULL;
    FARPROC result = NULL;

    name_rvas = (DWORD*)malloc(export_dir.NumberOfNames * sizeof(DWORD));
    ordinals  = (WORD*)malloc(export_dir.NumberOfNames * sizeof(WORD));
    func_rvas = (DWORD*)malloc(export_dir.NumberOfFunctions * sizeof(DWORD));
    if (!name_rvas || !ordinals || !func_rvas)
        goto done;

    if (!ReadProcessMemory(process, (BYTE*)remote_base + export_dir.AddressOfNames,
                           name_rvas, export_dir.NumberOfNames * sizeof(DWORD), NULL) ||
        !ReadProcessMemory(process, (BYTE*)remote_base + export_dir.AddressOfNameOrdinals,
                           ordinals, export_dir.NumberOfNames * sizeof(WORD), NULL) ||
        !ReadProcessMemory(process, (BYTE*)remote_base + export_dir.AddressOfFunctions,
                           func_rvas, export_dir.NumberOfFunctions * sizeof(DWORD), NULL))
        goto done;

    for (DWORD i = 0; i < export_dir.NumberOfNames; i++) {
        char name_buf[256] = {0};
        SIZE_T to_read = strlen(func_name) + 1;
        if (to_read > sizeof(name_buf) - 1)
            to_read = sizeof(name_buf) - 1;

        if (!ReadProcessMemory(process, (BYTE*)remote_base + name_rvas[i],
                               name_buf, to_read, NULL))
            continue;

        if (strcmp(name_buf, func_name) == 0 && ordinals[i] < export_dir.NumberOfFunctions) {
            DWORD func_rva = func_rvas[ordinals[i]];
            if (func_rva >= export_data_dir.VirtualAddress &&
                func_rva < export_data_dir.VirtualAddress + export_data_dir.Size) {
                LOG_DEBUG(L"PE parse: %hs is a forwarded export", func_name);
            } else {
                result = (FARPROC)((BYTE*)remote_base + func_rva);
            }
            break;
        }
    }

done:
    free(name_rvas);
    free(ordinals);
    free(func_rvas);
    return result;
}

static FARPROC get_remote_proc_address(HANDLE process, const wchar_t *module_name, const char *func_name) {
    DWORD pid = GetProcessId(process);

    LOG_DEBUG(L"Resolving %hs in remote %ls (PID: %lu)", func_name, module_name, pid);

    HMODULE remote_module = find_remote_module_peb(process, module_name);
    if (!remote_module) {
        LOG_DEBUG(L"PEB walk failed for %ls, trying VMQuery", module_name);
        remote_module = find_remote_module_vmquery(process, module_name);
    }
    if (!remote_module) {
        LOG_DEBUG(L"VMQuery failed for %ls, trying toolhelp", module_name);
        remote_module = find_remote_module_toolhelp(pid, module_name);
    }
    if (!remote_module) {
        LOG_ERROR(L"Failed to find %ls in target process (PID: %lu)", module_name, pid);
        return NULL;
    }

    LOG_DEBUG(L"Remote %ls base: 0x%p", module_name, remote_module);

    FARPROC result = find_export_in_remote_pe(process, remote_module, func_name);
    if (result) {
        LOG_DEBUG(L"Resolved %hs via PE exports: 0x%p", func_name, result);
        return result;
    }

    LOG_DEBUG(L"PE export parse failed for %hs, falling back to offset calculation", func_name);

    HMODULE local_module = GetModuleHandleW(module_name);
    if (!local_module) {
        LOG_ERROR(L"Failed to get local %ls handle", module_name);
        return NULL;
    }

    FARPROC local_func = GetProcAddress(local_module, func_name);
    if (!local_func) {
        LOG_ERROR(L"Failed to get local %hs address", func_name);
        return NULL;
    }

    DWORD_PTR offset = (DWORD_PTR)local_func - (DWORD_PTR)local_module;
    result = (FARPROC)((DWORD_PTR)remote_module + offset);
    LOG_DEBUG(L"Resolved %hs via offset: 0x%p (offset: 0x%llx)",
              func_name, result, (unsigned long long)offset);

    return result;
}

BOOL wait_for_process_init(HANDLE process, int timeout_ms) {
    const int interval = 10;

    for (int elapsed = 0; elapsed < timeout_ms; elapsed += interval) {
        if (find_remote_module_peb(process, L"kernel32.dll")) {
            LOG_DEBUG(L"kernel32.dll loaded after %dms", elapsed);
            return TRUE;
        }
        Sleep(interval);
    }

    LOG_WARN(L"Process init timed out after %dms", timeout_ms);
    return FALSE;
}

static BOOL prepare_loadlibrary_injection(HANDLE process, const wchar_t *dll_path,
                                          FARPROC *out_load_lib, LPVOID *out_remote_path) {
    FARPROC load_lib = get_remote_proc_address(process, L"kernel32.dll", "LoadLibraryA");
    if (!load_lib) {
        LOG_ERROR(L"Failed to resolve LoadLibraryA in target process");
        return FALSE;
    }

    char path_ansi[MAX_PATH];
    WideCharToMultiByte(CP_ACP, 0, dll_path, -1, path_ansi, MAX_PATH, NULL, NULL);
    SIZE_T path_size = strlen(path_ansi) + 1;

    LPVOID remote_path = VirtualAllocEx(process, NULL, path_size,
                                        MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remote_path) {
        LOG_ERROR(L"VirtualAllocEx failed: %lu", GetLastError());
        return FALSE;
    }

    if (!WriteProcessMemory(process, remote_path, path_ansi, path_size, NULL)) {
        LOG_ERROR(L"WriteProcessMemory failed: %lu", GetLastError());
        VirtualFreeEx(process, remote_path, 0, MEM_RELEASE);
        return FALSE;
    }

    *out_load_lib  = load_lib;
    *out_remote_path = remote_path;

    LOG_DEBUG(L"LoadLibraryA at 0x%p, path at 0x%p", load_lib, remote_path);
    return TRUE;
}

static BOOL inject_standard(HANDLE process, const wchar_t *dll_path) {
    FARPROC load_lib   = NULL;
    LPVOID  remote_path = NULL;
    if (!prepare_loadlibrary_injection(process, dll_path, &load_lib, &remote_path))
        return FALSE;

    HANDLE thread = CreateRemoteThread(process, NULL, 0,
                                       (LPTHREAD_START_ROUTINE)load_lib,
                                       remote_path, 0, NULL);
    if (!thread) {
        LOG_ERROR(L"CreateRemoteThread failed: %lu", GetLastError());
        VirtualFreeEx(process, remote_path, 0, MEM_RELEASE);
        return FALSE;
    }

    WaitForSingleObject(thread, INFINITE);

    DWORD exit_code;
    GetExitCodeThread(thread, &exit_code);
    CloseHandle(thread);
    VirtualFreeEx(process, remote_path, 0, MEM_RELEASE);

    if (!exit_code) {
        LOG_ERROR(L"LoadLibraryA returned NULL");
        return FALSE;
    }

    LOG_DEBUG(L"DLL loaded via CreateRemoteThread + LoadLibraryA");
    return TRUE;
}

static BOOL inject_apc(HANDLE process, HANDLE thread, const wchar_t *dll_path) {
    FARPROC load_lib   = NULL;
    LPVOID  remote_path = NULL;
    if (!prepare_loadlibrary_injection(process, dll_path, &load_lib, &remote_path))
        return FALSE;

    SuspendThread(thread);

    if (!QueueUserAPC((PAPCFUNC)load_lib, thread, (ULONG_PTR)remote_path)) {
        LOG_ERROR(L"QueueUserAPC failed: %lu", GetLastError());
        VirtualFreeEx(process, remote_path, 0, MEM_RELEASE);
        return FALSE;
    }

    LOG_DEBUG(L"APC queued (LoadLibraryA)");
    return TRUE;
}

static BOOL inject_nt(HANDLE process, const wchar_t *dll_path) {
    FARPROC load_lib   = NULL;
    LPVOID  remote_path = NULL;
    if (!prepare_loadlibrary_injection(process, dll_path, &load_lib, &remote_path))
        return FALSE;

    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (!ntdll) {
        LOG_ERROR(L"Failed to get ntdll.dll handle");
        VirtualFreeEx(process, remote_path, 0, MEM_RELEASE);
        return FALSE;
    }

    NtCreateThreadEx_t NtCreateThreadEx =
        (NtCreateThreadEx_t)GetProcAddress(ntdll, "NtCreateThreadEx");
    if (!NtCreateThreadEx) {
        LOG_ERROR(L"NtCreateThreadEx not available");
        VirtualFreeEx(process, remote_path, 0, MEM_RELEASE);
        return FALSE;
    }

    HANDLE remote_thread = NULL;
    NTSTATUS status = NtCreateThreadEx(&remote_thread, THREAD_ALL_ACCESS, NULL, process,
                                       load_lib, remote_path, 0, 0, 0, 0, NULL);
    if (status != 0 || !remote_thread) {
        LOG_ERROR(L"NtCreateThreadEx failed: 0x%08X", status);
        VirtualFreeEx(process, remote_path, 0, MEM_RELEASE);
        return FALSE;
    }

    WaitForSingleObject(remote_thread, INFINITE);

    DWORD exit_code;
    GetExitCodeThread(remote_thread, &exit_code);
    CloseHandle(remote_thread);
    VirtualFreeEx(process, remote_path, 0, MEM_RELEASE);

    if (!exit_code) {
        LOG_ERROR(L"LoadLibraryA returned NULL");
        return FALSE;
    }

    LOG_DEBUG(L"DLL loaded via NtCreateThreadEx + LoadLibraryA");
    return TRUE;
}

DWORD find_process_main_thread(DWORD pid) {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return 0;

    THREADENTRY32 te = {0};
    te.dwSize = sizeof(te);

    DWORD main_thread_id = 0;
    FILETIME earliest = {0xFFFFFFFF, 0xFFFFFFFF};

    if (Thread32First(snapshot, &te)) {
        do {
            if (te.th32OwnerProcessID == pid) {
                HANDLE t = OpenThread(THREAD_QUERY_INFORMATION, FALSE, te.th32ThreadID);
                if (t) {
                    FILETIME ct, et, kt, ut;
                    if (GetThreadTimes(t, &ct, &et, &kt, &ut) &&
                        CompareFileTime(&ct, &earliest) < 0) {
                        earliest = ct;
                        main_thread_id = te.th32ThreadID;
                    }
                    CloseHandle(t);
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

    DWORD main_thread_id = find_process_main_thread(pid);
    if (!main_thread_id) {
        LOG_ERROR(L"Failed to find main thread");
        FreeLibrary(dll);
        return FALSE;
    }

    HHOOK hook = SetWindowsHookExA(WH_CBT, (HOOKPROC)GetProcAddress(dll, "DllMain"),
                                    dll, main_thread_id);
    if (!hook) {
        LOG_ERROR(L"SetWindowsHookExA failed: %lu", GetLastError());
        FreeLibrary(dll);
        return FALSE;
    }

    PostThreadMessage(main_thread_id, WM_NULL, 0, 0);
    Sleep(100);
    UnhookWindowsHookEx(hook);

    LOG_DEBUG(L"DLL loaded via SetWindowsHookExA");
    return TRUE;
}

BOOL inject_dll(HANDLE process, HANDLE thread, DWORD pid, const wchar_t *dll_path, InjectionMethod method) {
    static const wchar_t *names[] = {L"standard", L"apc", L"nt", L"hook"};
    if (method >= 0 && method <= INJECTION_HOOK)
        LOG_INFO(L"Injection method: %ls", names[method]);

    switch (method) {
        case INJECTION_STANDARD: return inject_standard(process, dll_path);
        case INJECTION_APC:      return inject_apc(process, thread, dll_path);
        case INJECTION_NT:       return inject_nt(process, dll_path);
        case INJECTION_HOOK:     return inject_hook(pid, dll_path);
        default:
            LOG_ERROR(L"Unknown injection method: %d", method);
            return FALSE;
    }
}
