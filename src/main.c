#include <windows.h>
#include <tlhelp32.h>
#include <wctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>
#include "inject.h"
#include "logger.h"

#define VERSION "3.3.0"

typedef struct {
    wchar_t *target_exe;
    wchar_t **dll_paths;
    int dll_count;
    wchar_t *log_file;
    InjectionMethod method;
    DWORD sleep_ms;
    DWORD follow_sleep_ms;
    BOOL follow_process;
    wchar_t *follow_process_name;
    BOOL no_parent;
    wchar_t **target_args;
    int target_argc;
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
    wprintf(L"Usage: %ls <target.exe> <dll.dll> [dll2.dll ...] [options]\n\n", program_name);
    wprintf(L"Arguments:\n");
    wprintf(L"  target.exe              Path to the target executable\n");
    wprintf(L"  dll.dll [dll2.dll ...]  Path(s) to DLL(s) to inject (in order)\n\n");
    wprintf(L"Options:\n");
    wprintf(L"  --method <type>         Injection method (standard, apc, nt, hook, manual_map) [default: standard]\n");
    wprintf(L"  --log-file <path>       Log file path\n");
    wprintf(L"  --sleep <ms>            Delay in milliseconds before injection [default: 0]\n");
    wprintf(L"  --follow-sleep <ms>     Delay in milliseconds before injecting into followed child process [default: 0]\n");
    wprintf(L"  --follow-process        Inject into best child process when parent exits with 0\n");
    wprintf(L"  --follow-process-name   Preferred child process executable name\n");
    wprintf(L"  --no-parent             Skip parent injection; inject into child process instead\n\n");
    wprintf(L"  --                      End of injector options; pass remaining args to target\n\n");
    wprintf(L"Example:\n");
    wprintf(L"  %ls \"Z:\\path\\to\\game.exe\" \"Z:\\path\\to\\mod.dll\" --method apc -- --foo bar\n", program_name);
}

static int parse_arguments(int argc, wchar_t **argv, Arguments *args) {
    if (argc < 3)
        return 0;

    args->target_exe          = argv[1];
    args->dll_paths           = NULL;
    args->dll_count           = 0;
    args->log_file            = NULL;
    args->method              = INJECTION_STANDARD;
    args->sleep_ms            = 0;
    args->follow_sleep_ms     = 0;
    args->follow_process      = FALSE;
    args->follow_process_name = NULL;
    args->no_parent           = FALSE;
    args->target_args          = NULL;
    args->target_argc          = 0;

    int dll_capacity = 8;
    args->dll_paths = (wchar_t **)malloc(dll_capacity * sizeof(wchar_t *));
    if (!args->dll_paths)
        return 0;

    int i = 2;
    for (; i < argc; i++) {
        if (wcscmp(argv[i], L"--") == 0) {
            args->target_args = &argv[i + 1];
            args->target_argc = argc - i - 1;
            break;
        }
        if (wcsncmp(argv[i], L"--", 2) == 0)
            break;

        if (args->dll_count == dll_capacity) {
            dll_capacity *= 2;
            wchar_t **resized = (wchar_t **)realloc(args->dll_paths, dll_capacity * sizeof(wchar_t *));
            if (!resized)
                return 0;
            args->dll_paths = resized;
        }
        args->dll_paths[args->dll_count++] = argv[i];
    }

    if (args->dll_count == 0)
        return 0;

    for (; i < argc; i++) {
        if (wcscmp(argv[i], L"--") == 0) {
            args->target_args = &argv[i + 1];
            args->target_argc = argc - i - 1;
            break;
        }
        if (wcscmp(argv[i], L"--log-file") == 0 && i + 1 < argc) {
            args->log_file = argv[++i];
        } else if (wcscmp(argv[i], L"--sleep") == 0 && i + 1 < argc) {
            args->sleep_ms = (DWORD)wcstoul(argv[++i], NULL, 10);
        } else if (wcscmp(argv[i], L"--follow-sleep") == 0 && i + 1 < argc) {
            args->follow_sleep_ms = (DWORD)wcstoul(argv[++i], NULL, 10);
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
            else if (wcscmp(method, L"manual_map") == 0)
                args->method = INJECTION_MANUAL_MAP;
            else {
                wprintf(L"Error: Invalid injection method '%ls'\n", method);
                return 0;
            }
        }
    }

    return 1;
}

static BOOL arg_needs_quotes(const wchar_t *arg) {
    if (!arg || !*arg)
        return TRUE;
    for (const wchar_t *p = arg; *p; p++) {
        if (*p == L' ' || *p == L'\t' || *p == L'"')
            return TRUE;
    }
    return FALSE;
}

static BOOL append_wchars(wchar_t *buf, size_t buf_size, const wchar_t *src, size_t len) {
    size_t used = wcslen(buf);
    if (used + len + 1 > buf_size)
        return FALSE;
    wmemcpy(buf + used, src, len);
    buf[used + len] = L'\0';
    return TRUE;
}

static BOOL append_char(wchar_t *buf, size_t buf_size, wchar_t ch) {
    wchar_t tmp[2] = { ch, L'\0' };
    return append_wchars(buf, buf_size, tmp, 1);
}

static BOOL append_quoted_arg(wchar_t *buf, size_t buf_size, const wchar_t *arg, BOOL first) {
    if (!first) {
        if (!append_char(buf, buf_size, L' '))
            return FALSE;
    }

    if (!arg_needs_quotes(arg))
        return append_wchars(buf, buf_size, arg, wcslen(arg));

    if (!append_char(buf, buf_size, L'"'))
        return FALSE;

    size_t backslashes = 0;
    for (const wchar_t *p = arg; *p; p++) {
        if (*p == L'\\') {
            backslashes++;
            continue;
        }

        if (*p == L'"') {
            for (size_t i = 0; i < backslashes * 2 + 1; i++) {
                if (!append_char(buf, buf_size, L'\\'))
                    return FALSE;
            }
            backslashes = 0;
            if (!append_char(buf, buf_size, L'"'))
                return FALSE;
            continue;
        }

        while (backslashes > 0) {
            if (!append_char(buf, buf_size, L'\\'))
                return FALSE;
            backslashes--;
        }

        if (!append_char(buf, buf_size, *p))
            return FALSE;
    }

    while (backslashes > 0) {
        if (!append_char(buf, buf_size, L'\\'))
            return FALSE;
        backslashes--;
    }

    return append_char(buf, buf_size, L'"');
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
                                   const FILETIME *ref_time, const wchar_t *follow_name) {
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
        LOG_DEBUG(L"No alive child processes found to follow");
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

        LONGLONG i_delta    = filetime_delta(&candidates[i].start_time,    ref_time);
        LONGLONG best_delta = filetime_delta(&candidates[best].start_time, ref_time);

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

static BOOL is_descendant_of(HANDLE snapshot, DWORD pid, DWORD ancestor_pid) {
    DWORD current = pid;
    for (int depth = 0; depth < 16; depth++) {
        if (current == ancestor_pid)
            return TRUE;
        if (current == 0)
            return FALSE;

        PROCESSENTRY32W pe = {0};
        pe.dwSize = sizeof(pe);
        BOOL found = FALSE;

        if (Process32FirstW(snapshot, &pe)) {
            do {
                if (pe.th32ProcessID == current) {
                    current = pe.th32ParentProcessID;
                    found = TRUE;
                    break;
                }
            } while (Process32NextW(snapshot, &pe));
        }

        if (!found)
            return FALSE;
    }
    return FALSE;
}

static DWORD find_descendant_by_name(DWORD ancestor_pid, const wchar_t *name) {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
        return 0;

    PROCESSENTRY32W pe = {0};
    pe.dwSize = sizeof(pe);
    DWORD result = 0;

    if (Process32FirstW(snapshot, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, name) != 0)
                continue;
            if (pe.th32ProcessID == ancestor_pid)
                continue;
            if (is_descendant_of(snapshot, pe.th32ProcessID, ancestor_pid)) {
                result = pe.th32ProcessID;
                LOG_INFO(L"Found descendant process: [%5lu] %ls", result, pe.szExeFile);
                break;
            }
        } while (Process32NextW(snapshot, &pe));
    }

    CloseHandle(snapshot);
    return result;
}

static DWORD poll_for_child_process(HANDLE parent_proc, DWORD parent_pid,
                                    const wchar_t *parent_exe,
                                    const wchar_t *follow_name) {
    const int interval = 500;

    LOG_INFO(L"Polling for child process '%ls'...",
             follow_name ? follow_name : L"*");

    for (;;) {
        if (WaitForSingleObject(parent_proc, 0) != WAIT_TIMEOUT)
            return 0;

        DWORD pid = 0;

        if (follow_name) {
            pid = find_descendant_by_name(parent_pid, follow_name);
        } else {
            FILETIME now;
            GetSystemTimeAsFileTime(&now);
            pid = find_follow_candidate(parent_pid, parent_exe, &now, NULL);
        }

        if (pid)
            return pid;

        Sleep(interval);
    }
}

static void inject_into_followed_process(DWORD follow_pid, wchar_t **dll_paths, int dll_count,
                                         DWORD follow_sleep_ms, InjectionMethod method) {
    HANDLE follow_proc = OpenProcess(PROCESS_ALL_ACCESS, FALSE, follow_pid);
    if (!follow_proc) {
        LOG_ERROR(L"Failed to open followed process (PID: %lu): error %lu",
                  follow_pid, GetLastError());
        return;
    }

    wait_for_process_init(follow_proc, 10000);

    if (follow_sleep_ms > 0) {
        LOG_INFO(L"Sleeping for %lu ms before injection...", follow_sleep_ms);
        Sleep(follow_sleep_ms);
    }

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

    BOOL all_ok = TRUE;
    for (int i = 0; i < dll_count; i++) {
        if (dll_count > 1)
            LOG_INFO(L"Injecting DLL %d/%d into followed process...", i + 1, dll_count);
        else
            LOG_INFO(L"Injecting DLL into followed process...");

        if (!inject_dll(follow_proc, follow_thread, follow_pid, dll_paths[i], follow_method)) {
            LOG_ERROR(L"DLL injection into followed process failed: %ls", dll_paths[i]);
            all_ok = FALSE;
            break;
        }

        LOG_INFO(L"DLL injected successfully: %ls", dll_paths[i]);
    }

    if (all_ok) {
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
    for (int i = 0; i < args.dll_count; i++)
        LOG_INFO(L"DLL %d/%d: %ls", i + 1, args.dll_count, args.dll_paths[i]);

    wchar_t target_exe_full[MAX_PATH];

    if (!GetFullPathNameW(args.target_exe, MAX_PATH, target_exe_full, NULL)) {
        LOG_ERROR(L"Invalid target executable path");
        return 1;
    }

    if (GetFileAttributesW(target_exe_full) == INVALID_FILE_ATTRIBUTES) {
        LOG_ERROR(L"Target executable not found: %ls", target_exe_full);
        return 1;
    }

    wchar_t **dll_paths_full = (wchar_t **)malloc(args.dll_count * sizeof(wchar_t *));
    if (!dll_paths_full) {
        LOG_ERROR(L"Failed to allocate DLL path array");
        return 1;
    }

    for (int i = 0; i < args.dll_count; i++) {
        dll_paths_full[i] = (wchar_t *)malloc(MAX_PATH * sizeof(wchar_t));
        if (!dll_paths_full[i]) {
            LOG_ERROR(L"Failed to allocate DLL path buffer");
            for (int j = 0; j < i; j++) free(dll_paths_full[j]);
            free(dll_paths_full);
            return 1;
        }

        if (!GetFullPathNameW(args.dll_paths[i], MAX_PATH, dll_paths_full[i], NULL)) {
            LOG_ERROR(L"Invalid DLL path: %ls", args.dll_paths[i]);
            for (int j = 0; j <= i; j++) free(dll_paths_full[j]);
            free(dll_paths_full);
            return 1;
        }

        if (GetFileAttributesW(dll_paths_full[i]) == INVALID_FILE_ATTRIBUTES) {
            LOG_ERROR(L"DLL not found: %ls", dll_paths_full[i]);
            for (int j = 0; j <= i; j++) free(dll_paths_full[j]);
            free(dll_paths_full);
            return 1;
        }
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

    const size_t cmdline_cap = 32767;
    wchar_t *cmdline = (wchar_t *)malloc(cmdline_cap * sizeof(wchar_t));
    if (!cmdline) {
        LOG_ERROR(L"Failed to allocate command line buffer");
        for (int i = 0; i < args.dll_count; i++) free(dll_paths_full[i]);
        free(dll_paths_full);
        return 1;
    }
    cmdline[0] = L'\0';

    if (!append_quoted_arg(cmdline, cmdline_cap, target_exe_full, TRUE)) {
        LOG_ERROR(L"Command line is too long");
        free(cmdline);
        for (int i = 0; i < args.dll_count; i++) free(dll_paths_full[i]);
        free(dll_paths_full);
        return 1;
    }

    for (int i = 0; i < args.target_argc; i++) {
        if (!append_quoted_arg(cmdline, cmdline_cap, args.target_args[i], FALSE)) {
            LOG_ERROR(L"Command line is too long");
            free(cmdline);
            for (int j = 0; j < args.dll_count; j++) free(dll_paths_full[j]);
            free(dll_paths_full);
            return 1;
        }
    }

    if (!CreateProcessW(NULL, cmdline, NULL, NULL, FALSE,
                        CREATE_SUSPENDED, NULL, workdir, &si, &pi)) {
        LOG_ERROR(L"Failed to create process: error %lu", GetLastError());
        free(cmdline);
        for (int i = 0; i < args.dll_count; i++) free(dll_paths_full[i]);
        free(dll_paths_full);
        return 1;
    }

    free(cmdline);

    LOG_INFO(L"Process created (PID: %lu)", pi.dwProcessId);

    ResumeThread(pi.hThread);

    if (!args.no_parent) {
        wait_for_process_init(pi.hProcess, 10000);
        LOG_INFO(L"Process ready");

        if (args.sleep_ms > 0) {
            LOG_INFO(L"Sleeping for %lu ms before injection...", args.sleep_ms);
            Sleep(args.sleep_ms);
        }

        for (int i = 0; i < args.dll_count; i++) {
            if (args.dll_count > 1)
                LOG_INFO(L"Injecting DLL %d/%d...", i + 1, args.dll_count);
            else
                LOG_INFO(L"Injecting DLL...");

            if (!inject_dll(pi.hProcess, pi.hThread, pi.dwProcessId, dll_paths_full[i], args.method)) {
                LOG_ERROR(L"DLL injection failed: %ls", dll_paths_full[i]);
                TerminateProcess(pi.hProcess, 1);
                CloseHandle(pi.hThread);
                CloseHandle(pi.hProcess);
                for (int j = 0; j < args.dll_count; j++) free(dll_paths_full[j]);
                free(dll_paths_full);
                return 1;
            }

            LOG_INFO(L"DLL injected successfully: %ls", dll_paths_full[i]);
        }

        ResumeThread(pi.hThread);
    } else {
        LOG_INFO(L"Skipping parent injection (--no-parent)");

        for (;;) {
            DWORD follow_pid = poll_for_child_process(
                pi.hProcess, pi.dwProcessId, target_exe_full, args.follow_process_name);

            if (!follow_pid) {
                LOG_INFO(L"Parent process exited, stopping");
                break;
            }

            inject_into_followed_process(follow_pid, dll_paths_full, args.dll_count,
                                         args.follow_sleep_ms, args.method);
            LOG_INFO(L"Resuming child process watch...");
        }

        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        for (int i = 0; i < args.dll_count; i++) free(dll_paths_full[i]);
        free(dll_paths_full);
        logger_cleanup();
        return 0;
    }

    LOG_INFO(L"Waiting for process to exit...");

    WaitForSingleObject(pi.hProcess, INFINITE);

    log_process_list();

    DWORD exit_code;
    GetExitCodeProcess(pi.hProcess, &exit_code);
    LOG_INFO(L"Process exited with code %lu", exit_code);

    if (exit_code == 0 && args.follow_process) {
        FILETIME ft_create, ft_exit, ft_kernel, ft_user;
        GetProcessTimes(pi.hProcess, &ft_create, &ft_exit, &ft_kernel, &ft_user);

        LOG_INFO(L"Follow-process enabled, scanning for child processes...");

        DWORD follow_pid = find_follow_candidate(
            pi.dwProcessId, target_exe_full, &ft_exit, args.follow_process_name);

        if (!follow_pid) {
            LOG_WARN(L"No suitable child process found");
        } else {
            inject_into_followed_process(follow_pid, dll_paths_full, args.dll_count,
                                         args.follow_sleep_ms, args.method);
        }
    }

    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    for (int i = 0; i < args.dll_count; i++) free(dll_paths_full[i]);
    free(dll_paths_full);

    logger_cleanup();
    return 0;
}
