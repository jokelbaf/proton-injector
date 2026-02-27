#include <windows.h>
#include <tlhelp32.h>
#include <wctype.h>
#include <stdio.h>
#include <wchar.h>
#include "inject.h"
#include "logger.h"

#define VERSION "2.1.0"

typedef struct {
    wchar_t *target_exe;
    wchar_t *dll_path;
    wchar_t *log_file;
    InjectionMethod method;
    DWORD sleep_ms;
    BOOL follow_process;
    wchar_t *follow_process_name;
    BOOL no_parent;
} Arguments;

typedef struct {
    DWORD    pid;
    FILETIME start_time;
    wchar_t  exe_path[MAX_PATH];
    wchar_t  exe_name[MAX_PATH];
} ChildCandidate;

static void log_process_list(void) {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        LOG_WARN(L"Failed to enumerate processes: %lu", GetLastError());
        return;
    }

    PROCESSENTRY32W pe = {0};
    pe.dwSize = sizeof(pe);

    LOG_INFO(L"Running processes:");
    if (Process32FirstW(snapshot, &pe)) {
        do {
            LOG_INFO(L"  [%5lu] (ppid: %5lu) %ls", pe.th32ProcessID, pe.th32ParentProcessID, pe.szExeFile);
        } while (Process32NextW(snapshot, &pe));
    }

    CloseHandle(snapshot);
}

static void print_usage(const wchar_t *program_name) {
    wprintf(L"Proton DLL Injector v%hs\n", VERSION);
    wprintf(L"Usage: %ls <target.exe> <dll.dll> [options]\n\n", program_name);
    wprintf(L"Arguments:\n");
    wprintf(L"  target.exe              Path to the target executable\n");
    wprintf(L"  dll.dll                 Path to the DLL to inject\n\n");
    wprintf(L"Options:\n");
    wprintf(L"  --method <type>         Injection method (standard, apc, nt, hook) [default: standard]\n");
    wprintf(L"  --log-file <path>       Log file path\n");
    wprintf(L"  --sleep <ms>            Delay in milliseconds before injection [default: 0]\n");
    wprintf(L"  --follow-process        Inject into best child process when parent exits with 0\n");
    wprintf(L"  --follow-process-name   Preferred child process executable name\n");
    wprintf(L"  --no-parent             Skip parent injection; inject into child process instead\n\n");
    wprintf(L"Example:\n");
    wprintf(L"  %ls \"Z:\\path\\to\\game.exe\" \"Z:\\path\\to\\mod.dll\" --method apc --follow-process\n", program_name);
}

static int parse_arguments(int argc, wchar_t **argv, Arguments *args) {
    if (argc < 3)
        return 0;

    args->target_exe          = argv[1];
    args->dll_path            = argv[2];
    args->log_file            = NULL;
    args->method              = INJECTION_STANDARD;
    args->sleep_ms            = 0;
    args->follow_process      = FALSE;
    args->follow_process_name = NULL;
    args->no_parent           = FALSE;

    for (int i = 3; i < argc; i++) {
        if (wcscmp(argv[i], L"--log-file") == 0 && i + 1 < argc) {
            args->log_file = argv[++i];
        } else if (wcscmp(argv[i], L"--sleep") == 0 && i + 1 < argc) {
            args->sleep_ms = (DWORD)wcstoul(argv[++i], NULL, 10);
        } else if (wcscmp(argv[i], L"--follow-process") == 0) {
            args->follow_process = TRUE;
        } else if (wcscmp(argv[i], L"--follow-process-name") == 0 && i + 1 < argc) {
            args->follow_process_name = argv[++i];
        } else if (wcscmp(argv[i], L"--no-parent") == 0) {
            args->no_parent = TRUE;
        } else if (wcscmp(argv[i], L"--method") == 0 && i + 1 < argc) {
            wchar_t *method = argv[++i];
            if (wcscmp(method, L"standard") == 0)
                args->method = INJECTION_STANDARD;
            else if (wcscmp(method, L"apc") == 0)
                args->method = INJECTION_APC;
            else if (wcscmp(method, L"nt") == 0)
                args->method = INJECTION_NT;
            else if (wcscmp(method, L"hook") == 0)
                args->method = INJECTION_HOOK;
            else {
                wprintf(L"Error: Invalid injection method '%ls'\n", method);
                return 0;
            }
        }
    }

    return 1;
}

static LONGLONG filetime_delta(const FILETIME *a, const FILETIME *b) {
    ULARGE_INTEGER ua, ub;
    ua.LowPart  = a->dwLowDateTime;
    ua.HighPart = a->dwHighDateTime;
    ub.LowPart  = b->dwLowDateTime;
    ub.HighPart = b->dwHighDateTime;
    LONGLONG diff = (LONGLONG)ua.QuadPart - (LONGLONG)ub.QuadPart;
    return diff < 0 ? -diff : diff;
}

static int common_path_components(const wchar_t *a, const wchar_t *b) {
    int count = 0;
    while (*a && *b && towlower(*a) == towlower(*b)) {
        if (*a == L'\\')
            count++;
        a++;
        b++;
    }
    return count;
}

static DWORD find_follow_candidate(DWORD parent_pid, const wchar_t *parent_exe,
                                   FILETIME parent_exit_time, const wchar_t *follow_name) {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        LOG_WARN(L"Failed to enumerate processes for follow-process: %lu", GetLastError());
        return 0;
    }

    DWORD capacity = 16;
    DWORD count    = 0;
    ChildCandidate *candidates = (ChildCandidate *)malloc(capacity * sizeof(ChildCandidate));
    if (!candidates) {
        CloseHandle(snapshot);
        return 0;
    }

    PROCESSENTRY32W pe = {0};
    pe.dwSize = sizeof(pe);

    if (Process32FirstW(snapshot, &pe)) {
        do {
            if (pe.th32ParentProcessID != parent_pid)
                continue;

            HANDLE proc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pe.th32ProcessID);
            if (!proc)
                continue;

            FILETIME ft_start = {0}, ft_dummy = {0};
            BOOL times_ok = GetProcessTimes(proc, &ft_start, &ft_dummy, &ft_dummy, &ft_dummy);

            wchar_t exe_path[MAX_PATH] = {0};
            DWORD path_size = MAX_PATH;
            QueryFullProcessImageNameW(proc, 0, exe_path, &path_size);

            CloseHandle(proc);

            if (!times_ok)
                continue;

            if (count == capacity) {
                capacity *= 2;
                ChildCandidate *resized = (ChildCandidate *)realloc(candidates,
                                          capacity * sizeof(ChildCandidate));
                if (!resized)
                    break;
                candidates = resized;
            }

            candidates[count].pid        = pe.th32ProcessID;
            candidates[count].start_time = ft_start;
            wcsncpy(candidates[count].exe_path, exe_path, MAX_PATH - 1);
            candidates[count].exe_path[MAX_PATH - 1] = L'\0';
            wcsncpy(candidates[count].exe_name, pe.szExeFile, MAX_PATH - 1);
            candidates[count].exe_name[MAX_PATH - 1] = L'\0';
            count++;

        } while (Process32NextW(snapshot, &pe));
    }

    CloseHandle(snapshot);

    if (count == 0) {
        LOG_WARN(L"No alive child processes found to follow");
        free(candidates);
        return 0;
    }

    LOG_INFO(L"Child process candidates (%lu):", count);
    for (DWORD i = 0; i < count; i++)
        LOG_INFO(L"  [%5lu] %ls", candidates[i].pid, candidates[i].exe_name);

    int best = 0;
    for (DWORD i = 1; i < count; i++) {
        BOOL i_name    = follow_name && _wcsicmp(candidates[i].exe_name,    follow_name) == 0;
        BOOL best_name = follow_name && _wcsicmp(candidates[best].exe_name, follow_name) == 0;

        if (i_name && !best_name)    { best = (int)i; continue; }
        if (!i_name && best_name)    { continue; }

        LONGLONG i_delta    = filetime_delta(&candidates[i].start_time,    &parent_exit_time);
        LONGLONG best_delta = filetime_delta(&candidates[best].start_time, &parent_exit_time);

        if (i_delta < best_delta)    { best = (int)i; continue; }
        if (i_delta > best_delta)    { continue; }

        int i_path    = common_path_components(candidates[i].exe_path,    parent_exe);
        int best_path = common_path_components(candidates[best].exe_path, parent_exe);

        if (i_path > best_path)      { best = (int)i; }
    }

    DWORD best_pid = candidates[best].pid;
    LOG_INFO(L"Selected follow candidate: [%5lu] %ls", best_pid, candidates[best].exe_name);
    free(candidates);
    return best_pid;
}

static void inject_into_followed_process(DWORD follow_pid, const wchar_t *dll_path,
                                         DWORD sleep_ms, InjectionMethod method) {
    HANDLE follow_proc = OpenProcess(PROCESS_ALL_ACCESS, FALSE, follow_pid);
    if (!follow_proc) {
        LOG_ERROR(L"Failed to open followed process (PID: %lu): error %lu",
                  follow_pid, GetLastError());
        return;
    }

    wait_for_process_init(follow_proc, 10000);

    if (sleep_ms > 0) {
        LOG_INFO(L"Sleeping for %lu ms before injection...", sleep_ms);
        Sleep(sleep_ms);
    }

    LOG_INFO(L"Injecting DLL into followed process...");

    HANDLE follow_thread = NULL;
    InjectionMethod follow_method = method;

    if (method == INJECTION_APC) {
        DWORD tid = find_process_main_thread(follow_pid);
        if (tid) {
            follow_thread = OpenThread(
                THREAD_SUSPEND_RESUME | THREAD_SET_CONTEXT | THREAD_GET_CONTEXT,
                FALSE, tid);
        }
        if (!follow_thread) {
            LOG_WARN(L"Could not open follow process main thread; falling back to standard injection");
            follow_method = INJECTION_STANDARD;
        }
    }

    if (!inject_dll(follow_proc, follow_thread, follow_pid, dll_path, follow_method)) {
        LOG_ERROR(L"DLL injection into followed process failed");
    } else {
        LOG_INFO(L"DLL injected into followed process successfully");

        if (follow_method == INJECTION_APC && follow_thread)
            ResumeThread(follow_thread);

        LOG_INFO(L"Waiting for followed process to exit...");
        WaitForSingleObject(follow_proc, INFINITE);

        DWORD follow_exit;
        GetExitCodeProcess(follow_proc, &follow_exit);
        LOG_INFO(L"Followed process exited with code %lu", follow_exit);
    }

    if (follow_thread)
        CloseHandle(follow_thread);
    CloseHandle(follow_proc);
}

int wmain(int argc, wchar_t **argv) {
    Arguments args;

    if (!parse_arguments(argc, argv, &args)) {
        print_usage(argv[0]);
        return 1;
    }

    if (args.log_file) {
        if (!logger_init(args.log_file))
            wprintf(L"Warning: Failed to initialize logger\n");
    }

    LOG_INFO(L"Proton DLL Injector v%hs", VERSION);
    LOG_INFO(L"Target: %ls", args.target_exe);
    LOG_INFO(L"DLL: %ls", args.dll_path);

    wchar_t target_exe_full[MAX_PATH];
    wchar_t dll_path_full[MAX_PATH];

    if (!GetFullPathNameW(args.target_exe, MAX_PATH, target_exe_full, NULL)) {
        LOG_ERROR(L"Invalid target executable path");
        return 1;
    }

    if (!GetFullPathNameW(args.dll_path, MAX_PATH, dll_path_full, NULL)) {
        LOG_ERROR(L"Invalid DLL path");
        return 1;
    }

    if (GetFileAttributesW(target_exe_full) == INVALID_FILE_ATTRIBUTES) {
        LOG_ERROR(L"Target executable not found: %ls", target_exe_full);
        return 1;
    }

    if (GetFileAttributesW(dll_path_full) == INVALID_FILE_ATTRIBUTES) {
        LOG_ERROR(L"DLL not found: %ls", dll_path_full);
        return 1;
    }

    wchar_t workdir[MAX_PATH];
    wcscpy(workdir, target_exe_full);
    wchar_t *last_slash = wcsrchr(workdir, L'\\');
    if (last_slash)
        *last_slash = L'\0';

    LOG_INFO(L"Working directory: %ls", workdir);
    LOG_INFO(L"Starting target process...");

    STARTUPINFOW si = {0};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi = {0};

    wchar_t cmdline[MAX_PATH];
    wsprintfW(cmdline, L"\"%ls\"", target_exe_full);

    if (!CreateProcessW(NULL, cmdline, NULL, NULL, FALSE,
                        CREATE_SUSPENDED, NULL, workdir, &si, &pi)) {
        LOG_ERROR(L"Failed to create process: error %lu", GetLastError());
        return 1;
    }

    LOG_INFO(L"Process created (PID: %lu)", pi.dwProcessId);

    ResumeThread(pi.hThread);

    if (!args.no_parent) {
        wait_for_process_init(pi.hProcess, 10000);
        LOG_INFO(L"Process ready");

        if (args.sleep_ms > 0) {
            LOG_INFO(L"Sleeping for %lu ms before injection...", args.sleep_ms);
            Sleep(args.sleep_ms);
        }

        LOG_INFO(L"Injecting DLL...");

        if (!inject_dll(pi.hProcess, pi.hThread, pi.dwProcessId, dll_path_full, args.method)) {
            LOG_ERROR(L"DLL injection failed");
            TerminateProcess(pi.hProcess, 1);
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
            return 1;
        }

        LOG_INFO(L"DLL injected successfully");

        ResumeThread(pi.hThread);
    } else {
        LOG_INFO(L"Skipping parent injection (--no-parent)");
    }

    LOG_INFO(L"Waiting for process to exit...");

    WaitForSingleObject(pi.hProcess, INFINITE);

    log_process_list();

    DWORD exit_code;
    GetExitCodeProcess(pi.hProcess, &exit_code);
    LOG_INFO(L"Process exited with code %lu", exit_code);

    BOOL should_follow = args.no_parent || (exit_code == 0 && args.follow_process);
    if (should_follow) {
        FILETIME ft_create, ft_exit, ft_kernel, ft_user;
        GetProcessTimes(pi.hProcess, &ft_create, &ft_exit, &ft_kernel, &ft_user);

        if (args.no_parent)
            LOG_INFO(L"Scanning for child processes (--no-parent)...");
        else
            LOG_INFO(L"Follow-process enabled, scanning for child processes...");

        DWORD follow_pid = find_follow_candidate(
            pi.dwProcessId, target_exe_full, ft_exit, args.follow_process_name);

        if (!follow_pid) {
            LOG_WARN(L"No suitable child process found");
        } else {
            inject_into_followed_process(follow_pid, dll_path_full, args.sleep_ms, args.method);
        }
    }

    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    logger_cleanup();
    return 0;
}
