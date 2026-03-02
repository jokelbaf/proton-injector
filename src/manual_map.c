#include <windows.h>
#include <string.h>
#include "manual_map.h"
#include "logger.h"

#ifdef _WIN64
#define CURRENT_ARCH IMAGE_FILE_MACHINE_AMD64
#else
#define CURRENT_ARCH IMAGE_FILE_MACHINE_I386
#endif

static BYTE *read_dll_file(const wchar_t *path, SIZE_T *out_size) {
    HANDLE hFile = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL,
                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        LOG_ERROR(L"Failed to open DLL: %lu", GetLastError());
        return NULL;
    }

    LARGE_INTEGER size;
    if (!GetFileSizeEx(hFile, &size) || size.QuadPart < 0x1000) {
        LOG_ERROR(L"Invalid DLL file size");
        CloseHandle(hFile);
        return NULL;
    }

    BYTE *buf = (BYTE *)malloc((SIZE_T)size.QuadPart);
    if (!buf) {
        LOG_ERROR(L"Failed to allocate DLL read buffer");
        CloseHandle(hFile);
        return NULL;
    }

    DWORD read;
    if (!ReadFile(hFile, buf, (DWORD)size.QuadPart, &read, NULL) ||
        read != (DWORD)size.QuadPart) {
        LOG_ERROR(L"Failed to read DLL file: %lu", GetLastError());
        free(buf);
        CloseHandle(hFile);
        return NULL;
    }

    CloseHandle(hFile);
    *out_size = (SIZE_T)size.QuadPart;
    return buf;
}

static void cleanup_remote(HANDLE process, BYTE *image, BYTE *data, BYTE *shellcode) {
    if (shellcode) VirtualFreeEx(process, shellcode, 0, MEM_RELEASE);
    if (data)      VirtualFreeEx(process, data,      0, MEM_RELEASE);
    if (image)     VirtualFreeEx(process, image,     0, MEM_RELEASE);
}

BOOL inject_manual_map(HANDLE process, const wchar_t *dll_path) {
    SIZE_T file_size = 0;
    BYTE  *src = read_dll_file(dll_path, &file_size);
    if (!src)
        return FALSE;

    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)src;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) {
        LOG_ERROR(L"Invalid PE: bad DOS signature");
        free(src);
        return FALSE;
    }

    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(src + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) {
        LOG_ERROR(L"Invalid PE: bad NT signature");
        free(src);
        return FALSE;
    }

    if (nt->FileHeader.Machine != CURRENT_ARCH) {
        LOG_ERROR(L"Invalid PE: architecture mismatch");
        free(src);
        return FALSE;
    }

    IMAGE_OPTIONAL_HEADER *opt = &nt->OptionalHeader;

    BYTE *remote_image = (BYTE *)VirtualAllocEx(process, NULL, opt->SizeOfImage,
                                                MEM_COMMIT | MEM_RESERVE,
                                                PAGE_READWRITE);
    if (!remote_image) {
        LOG_ERROR(L"VirtualAllocEx failed for image: %lu", GetLastError());
        free(src);
        return FALSE;
    }

    DWORD old_prot;
    VirtualProtectEx(process, remote_image, opt->SizeOfImage,
                     PAGE_EXECUTE_READWRITE, &old_prot);

    if (!WriteProcessMemory(process, remote_image, src, 0x1000, NULL)) {
        LOG_ERROR(L"Failed to write PE header: %lu", GetLastError());
        cleanup_remote(process, remote_image, NULL, NULL);
        free(src);
        return FALSE;
    }

    IMAGE_SECTION_HEADER *section = IMAGE_FIRST_SECTION(nt);
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; i++, section++) {
        if (!section->SizeOfRawData)
            continue;
        if (!WriteProcessMemory(process,
                                remote_image + section->VirtualAddress,
                                src + section->PointerToRawData,
                                section->SizeOfRawData, NULL)) {
            LOG_ERROR(L"Failed to write section %.8hs: %lu",
                      section->Name, GetLastError());
            cleanup_remote(process, remote_image, NULL, NULL);
            free(src);
            return FALSE;
        }
    }

    MappingData mapping = {0};
    HMODULE k32 = GetModuleHandleW(L"kernel32.dll");
    mapping.pLoadLibraryA    = (f_LoadLibraryA_t)GetProcAddress(k32, "LoadLibraryA");
    mapping.pGetProcAddress  = (f_GetProcAddress_t)GetProcAddress(k32, "GetProcAddress");
    mapping.pGetModuleHandleA = (f_GetModuleHandleA_t)GetProcAddress(k32, "GetModuleHandleA");
#ifdef _WIN64
    mapping.pRtlAddFunctionTable = (f_RtlAddFunctionTable_t)GetProcAddress(
                                        GetModuleHandleW(L"ntdll.dll"),
                                        "RtlAddFunctionTable");
    mapping.SEHSupport = mapping.pRtlAddFunctionTable != NULL;
#else
    mapping.SEHSupport = FALSE;
#endif
    mapping.pbase         = remote_image;
    mapping.fdwReasonParam = DLL_PROCESS_ATTACH;
    mapping.reservedParam  = NULL;

    BYTE *remote_data = (BYTE *)VirtualAllocEx(process, NULL, sizeof(MappingData),
                                               MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remote_data) {
        LOG_ERROR(L"VirtualAllocEx failed for mapping data: %lu", GetLastError());
        cleanup_remote(process, remote_image, NULL, NULL);
        free(src);
        return FALSE;
    }

    if (!WriteProcessMemory(process, remote_data, &mapping, sizeof(MappingData), NULL)) {
        LOG_ERROR(L"Failed to write mapping data: %lu", GetLastError());
        cleanup_remote(process, remote_image, remote_data, NULL);
        free(src);
        return FALSE;
    }

    BYTE *remote_shellcode = (BYTE *)VirtualAllocEx(process, NULL, 0x1000,
                                                     MEM_COMMIT | MEM_RESERVE,
                                                     PAGE_EXECUTE_READWRITE);
    if (!remote_shellcode) {
        LOG_ERROR(L"VirtualAllocEx failed for shellcode: %lu", GetLastError());
        cleanup_remote(process, remote_image, remote_data, NULL);
        free(src);
        return FALSE;
    }

    if (!WriteProcessMemory(process, remote_shellcode, (PVOID)mm_shellcode, 0x1000, NULL)) {
        LOG_ERROR(L"Failed to write shellcode: %lu", GetLastError());
        cleanup_remote(process, remote_image, remote_data, remote_shellcode);
        free(src);
        return FALSE;
    }

    LOG_DEBUG(L"Image at %p, mapping data at %p, shellcode at %p",
              remote_image, remote_data, remote_shellcode);

    HANDLE hThread = CreateRemoteThread(process, NULL, 0,
                                        (LPTHREAD_START_ROUTINE)remote_shellcode,
                                        remote_data, 0, NULL);
    if (!hThread) {
        LOG_ERROR(L"CreateRemoteThread failed: %lu", GetLastError());
        cleanup_remote(process, remote_image, remote_data, remote_shellcode);
        free(src);
        return FALSE;
    }

    CloseHandle(hThread);

    HINSTANCE hCheck = NULL;
    while (!hCheck) {
        DWORD exit_code = 0;
        GetExitCodeProcess(process, &exit_code);
        if (exit_code != STILL_ACTIVE) {
            LOG_ERROR(L"Target process exited during shellcode (code: %lu)", exit_code);
            cleanup_remote(process, remote_image, remote_data, remote_shellcode);
            free(src);
            return FALSE;
        }

        MappingData checked = {0};
        ReadProcessMemory(process, remote_data, &checked, sizeof(checked), NULL);
        hCheck = checked.hMod;

        if (hCheck == MM_SENTINEL_BAD_DATA) {
            LOG_ERROR(L"Shellcode: invalid mapping data pointer");
            cleanup_remote(process, remote_image, remote_data, remote_shellcode);
            free(src);
            return FALSE;
        }

        if (hCheck == MM_SENTINEL_SEH_FAILED) {
            LOG_WARN(L"Shellcode: RtlAddFunctionTable failed (SEH unavailable)");
            break;
        }

        if (!hCheck)
            Sleep(10);
    }

    BYTE *zeroes = (BYTE *)calloc(opt->SizeOfImage, 1);
    if (zeroes) {
        WriteProcessMemory(process, remote_image, zeroes, 0x1000, NULL);

        section = IMAGE_FIRST_SECTION(nt);
        for (WORD i = 0; i < nt->FileHeader.NumberOfSections; i++, section++) {
            if (!section->Misc.VirtualSize)
                continue;
            if (strcmp((char *)section->Name, ".rsrc") == 0 ||
                strcmp((char *)section->Name, ".reloc") == 0) {
                WriteProcessMemory(process,
                                   remote_image + section->VirtualAddress,
                                   zeroes, section->Misc.VirtualSize, NULL);
            }
        }

        free(zeroes);
    }

    section = IMAGE_FIRST_SECTION(nt);
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; i++, section++) {
        if (!section->Misc.VirtualSize)
            continue;
        DWORD prot = PAGE_READONLY;
        if (section->Characteristics & IMAGE_SCN_MEM_WRITE)
            prot = PAGE_READWRITE;
        else if (section->Characteristics & IMAGE_SCN_MEM_EXECUTE)
            prot = PAGE_EXECUTE_READ;
        VirtualProtectEx(process, remote_image + section->VirtualAddress,
                         section->Misc.VirtualSize, prot, &old_prot);
    }

    VirtualProtectEx(process, remote_image,
                     IMAGE_FIRST_SECTION(nt)->VirtualAddress,
                     PAGE_READONLY, &old_prot);

    BYTE shellcode_zero[0x1000];
    memset(shellcode_zero, 0, sizeof(shellcode_zero));
    WriteProcessMemory(process, remote_shellcode, shellcode_zero, 0x1000, NULL);
    VirtualFreeEx(process, remote_shellcode, 0, MEM_RELEASE);
    VirtualFreeEx(process, remote_data,      0, MEM_RELEASE);

    free(src);

    LOG_DEBUG(L"Manual map complete, DLL base: %p", remote_image);
    return TRUE;
}
