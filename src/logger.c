#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <time.h>
#include "logger.h"

static FILE *log_file = NULL;
static CRITICAL_SECTION log_lock;
static BOOL logger_initialized = FALSE;

BOOL logger_init(const wchar_t *log_path) {
    if (logger_initialized) {
        return TRUE;
    }

    InitializeCriticalSection(&log_lock);

    log_file = _wfopen(log_path, L"w");
    if (!log_file) {
        DeleteCriticalSection(&log_lock);
        return FALSE;
    }

    logger_initialized = TRUE;
    return TRUE;
}

void logger_cleanup(void) {
    if (!logger_initialized) {
        return;
    }

    EnterCriticalSection(&log_lock);
    
    if (log_file) {
        fclose(log_file);
        log_file = NULL;
    }

    LeaveCriticalSection(&log_lock);
    DeleteCriticalSection(&log_lock);
    
    logger_initialized = FALSE;
}

void logger_log(LogLevel level, const wchar_t *format, ...) {
    const wchar_t *level_str;
    switch (level) {
        case LOG_LEVEL_DEBUG: level_str = L"DEBUG"; break;
        case LOG_LEVEL_INFO:  level_str = L"INFO "; break;
        case LOG_LEVEL_WARN:  level_str = L"WARN "; break;
        case LOG_LEVEL_ERROR: level_str = L"ERROR"; break;
        default:              level_str = L"?????"; break;
    }

    SYSTEMTIME st;
    GetLocalTime(&st);

    wchar_t timestamp[32];
    wsprintfW(timestamp, L"%04d-%02d-%02d %02d:%02d:%02d",
              st.wYear, st.wMonth, st.wDay,
              st.wHour, st.wMinute, st.wSecond);

    wchar_t message[2048];
    va_list args;
    va_start(args, format);
    vswprintf(message, 2048, format, args);
    va_end(args);

    wchar_t log_line[2560];
    wsprintfW(log_line, L"[%ls] [%ls] %ls\n", timestamp, level_str, message);

    wprintf(L"%ls", log_line);

    if (logger_initialized && log_file) {
        EnterCriticalSection(&log_lock);
        fwprintf(log_file, L"%ls", log_line);
        fflush(log_file);
        LeaveCriticalSection(&log_lock);
    }
}
