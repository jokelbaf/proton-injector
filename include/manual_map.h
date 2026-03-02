#pragma once

#include <windows.h>

typedef HINSTANCE (WINAPI *f_LoadLibraryA_t)(const char *lpLibFilename);
typedef FARPROC   (WINAPI *f_GetProcAddress_t)(HMODULE hModule, LPCSTR lpProcName);
typedef HMODULE   (WINAPI *f_GetModuleHandleA_t)(const char *lpModuleName);
typedef BOOL      (WINAPI *f_DllMain_t)(void *hDll, DWORD dwReason, void *pReserved);

#ifdef _WIN64
typedef BOOL (WINAPIV *f_RtlAddFunctionTable_t)(
    RUNTIME_FUNCTION *FunctionTable,
    DWORD             EntryCount,
    DWORD64           BaseAddress
);
#endif

typedef struct {
    f_LoadLibraryA_t    pLoadLibraryA;
    f_GetProcAddress_t  pGetProcAddress;
    f_GetModuleHandleA_t pGetModuleHandleA;
#ifdef _WIN64
    f_RtlAddFunctionTable_t pRtlAddFunctionTable;
#endif
    BYTE     *pbase;
    HINSTANCE hMod;
    DWORD     fdwReasonParam;
    LPVOID    reservedParam;
    BOOL      SEHSupport;
} MappingData;

#define MM_SENTINEL_BAD_DATA    ((HINSTANCE)0x404040)
#define MM_SENTINEL_SEH_FAILED  ((HINSTANCE)0x505050)

BOOL inject_manual_map(HANDLE process, const wchar_t *dll_path);
void __stdcall mm_shellcode(MappingData *pData);
