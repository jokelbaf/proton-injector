#include <windows.h>
#include <stdio.h>
#include <wchar.h>
#include "inject.h"
#include "logger.h"

#define VERSION "1.0.0"

typedef struct {
    wchar_t *target_exe;
    wchar_t *dll_path;
    wchar_t *log_file;
    InjectionMethod method;
} Arguments;

static void print_usage(const wchar_t *program_name) {
    wprintf(L"Proton DLL Injector v%hs\n", VERSION);
    wprintf(L"Usage: %ls <target.exe> <dll.dll> [options]\n\n", program_name);
    wprintf(L"Arguments:\n");
    wprintf(L"  target.exe       Path to the target executable\n");
    wprintf(L"  dll.dll          Path to the DLL to inject\n\n");
    wprintf(L"Options:\n");
    wprintf(L"  --method <type>  Injection method (standard, apc, nt, hook) [default: standard]\n");
    wprintf(L"  --log-file <path> Log file path\n\n");
    wprintf(L"Example:\n");
    wprintf(L"  %ls \"Z:\\path\\to\\game.exe\" \"Z:\\path\\to\\mod.dll\" --method apc --log-file \"injector.log\"\n", program_name);
}

static int parse_arguments(int argc, wchar_t **argv, Arguments *args) {
    if (argc < 3) {
        return 0;
    }

    args->target_exe = argv[1];
    args->dll_path = argv[2];
    args->log_file = NULL;
    args->method = INJECTION_STANDARD;

    for (int i = 3; i < argc; i++) {
        if (wcscmp(argv[i], L"--log-file") == 0 && i + 1 < argc) {
            args->log_file = argv[++i];
        } else if (wcscmp(argv[i], L"--method") == 0 && i + 1 < argc) {
            wchar_t *method = argv[++i];
            if (wcscmp(method, L"standard") == 0) {
                args->method = INJECTION_STANDARD;
            } else if (wcscmp(method, L"apc") == 0) {
                args->method = INJECTION_APC;
            } else if (wcscmp(method, L"nt") == 0) {
                args->method = INJECTION_NT;
            } else if (wcscmp(method, L"hook") == 0) {
                args->method = INJECTION_HOOK;
            } else {
                wprintf(L"Error: Invalid injection method '%ls'\n", method);
                return 0;
            }
        }
    }

    return 1;
}

int wmain(int argc, wchar_t **argv) {
    Arguments args;

    if (!parse_arguments(argc, argv, &args)) {
        print_usage(argv[0]);
        return 1;
    }

    if (args.log_file) {
        if (!logger_init(args.log_file)) {
            wprintf(L"Warning: Failed to initialize logger\n");
        }
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
    if (last_slash) {
        *last_slash = L'\0';
    }

    LOG_INFO(L"Working directory: %ls", workdir);
    LOG_INFO(L"Starting target process...");

    STARTUPINFOW si = {0};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi = {0};

    wchar_t cmdline[MAX_PATH];
    wsprintfW(cmdline, L"\"%ls\"", target_exe_full);

    if (!CreateProcessW(
        NULL,
        cmdline,
        NULL,
        NULL,
        FALSE,
        CREATE_SUSPENDED,
        NULL,
        workdir,
        &si,
        &pi
    )) {
        LOG_ERROR(L"Failed to create process: error %lu", GetLastError());
        return 1;
    }

    LOG_INFO(L"Process created (PID: %lu)", pi.dwProcessId);
    LOG_INFO(L"Injecting DLL...");

    if (!inject_dll(pi.hProcess, pi.hThread, pi.dwProcessId, dll_path_full, args.method)) {
        LOG_ERROR(L"DLL injection failed");
        TerminateProcess(pi.hProcess, 1);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        return 1;
    }

    LOG_INFO(L"DLL injected successfully");
    LOG_INFO(L"Resuming process...");

    ResumeThread(pi.hThread);

    LOG_INFO(L"Process resumed, waiting for exit...");

    WaitForSingleObject(pi.hProcess, INFINITE);

    DWORD exit_code;
    GetExitCodeProcess(pi.hProcess, &exit_code);
    LOG_INFO(L"Process exited with code %lu", exit_code);

    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    logger_cleanup();
    return 0;
}
