#include <windows.h>
#include <stdio.h>

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved) {
    if (fdwReason == DLL_PROCESS_ATTACH) {
        MessageBoxW(NULL, L"DLL Injected Successfully!", L"Test DLL", MB_OK | MB_ICONINFORMATION);
    }
    return TRUE;
}
