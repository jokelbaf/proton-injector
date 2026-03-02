#include <windows.h>
#include "manual_map.h"

#ifdef _WIN64
#define RELOC_FLAG(x) (((x) >> 12) == IMAGE_REL_BASED_DIR64)
#else
#define RELOC_FLAG(x) (((x) >> 12) == IMAGE_REL_BASED_HIGHLOW)
#endif

__attribute__((optimize("O0"), noinline))
void __stdcall mm_shellcode(MappingData *pData) {
    if (!pData)
        return;

    BYTE *pBase = pData->pbase;
    IMAGE_NT_HEADERS *pNt =
        (IMAGE_NT_HEADERS *)(pBase + ((IMAGE_DOS_HEADER *)pBase)->e_lfanew);
    IMAGE_OPTIONAL_HEADER *pOpt = &pNt->OptionalHeader;

    f_LoadLibraryA_t    _LoadLibraryA    = pData->pLoadLibraryA;
    f_GetProcAddress_t  _GetProcAddress  = pData->pGetProcAddress;
    f_GetModuleHandleA_t _GetModuleHandleA = pData->pGetModuleHandleA;
#ifdef _WIN64
    f_RtlAddFunctionTable_t _RtlAddFunctionTable = pData->pRtlAddFunctionTable;
#endif
    f_DllMain_t _DllMain = (f_DllMain_t)(pBase + pOpt->AddressOfEntryPoint);

    ULONG_PTR delta = (ULONG_PTR)pBase - (ULONG_PTR)pOpt->ImageBase;
    if (delta) {
        IMAGE_DATA_DIRECTORY *reloc_dir =
            &pOpt->DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];
        if (reloc_dir->Size) {
            IMAGE_BASE_RELOCATION *pReloc =
                (IMAGE_BASE_RELOCATION *)(pBase + reloc_dir->VirtualAddress);
            IMAGE_BASE_RELOCATION *pRelocEnd =
                (IMAGE_BASE_RELOCATION *)((BYTE *)pReloc + reloc_dir->Size);

            while (pReloc < pRelocEnd && pReloc->SizeOfBlock) {
                DWORD  count = (pReloc->SizeOfBlock - sizeof(IMAGE_BASE_RELOCATION)) / sizeof(WORD);
                WORD  *pInfo = (WORD *)(pReloc + 1);

                for (DWORD i = 0; i < count; i++, pInfo++) {
                    if (RELOC_FLAG(*pInfo)) {
                        ULONG_PTR *pPatch =
                            (ULONG_PTR *)(pBase + pReloc->VirtualAddress + (*pInfo & 0xFFF));
                        *pPatch += delta;
                    }
                }

                pReloc = (IMAGE_BASE_RELOCATION *)((BYTE *)pReloc + pReloc->SizeOfBlock);
            }
        }
    }

    IMAGE_DATA_DIRECTORY *import_dir =
        &pOpt->DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (import_dir->Size) {
        IMAGE_IMPORT_DESCRIPTOR *pImport =
            (IMAGE_IMPORT_DESCRIPTOR *)(pBase + import_dir->VirtualAddress);

        while (pImport->Name) {
            char       *modName  = (char *)(pBase + pImport->Name);
            HINSTANCE   hDll     = _GetModuleHandleA ? (HINSTANCE)_GetModuleHandleA(modName) : NULL;
            if (!hDll)
                hDll = _LoadLibraryA(modName);
            ULONG_PTR  *pThunkRef = (ULONG_PTR *)(pBase + pImport->OriginalFirstThunk);
            ULONG_PTR  *pFuncRef  = (ULONG_PTR *)(pBase + pImport->FirstThunk);

            if (!pImport->OriginalFirstThunk)
                pThunkRef = pFuncRef;

            for (; *pThunkRef; pThunkRef++, pFuncRef++) {
                if (IMAGE_SNAP_BY_ORDINAL(*pThunkRef)) {
                    *pFuncRef = (ULONG_PTR)_GetProcAddress(
                        hDll, (char *)(*pThunkRef & 0xFFFF));
                } else {
                    IMAGE_IMPORT_BY_NAME *pByName =
                        (IMAGE_IMPORT_BY_NAME *)(pBase + *pThunkRef);
                    *pFuncRef = (ULONG_PTR)_GetProcAddress(hDll, pByName->Name);
                }
            }

            pImport++;
        }
    }

    IMAGE_DATA_DIRECTORY *tls_dir =
        &pOpt->DataDirectory[IMAGE_DIRECTORY_ENTRY_TLS];
    if (tls_dir->Size) {
        IMAGE_TLS_DIRECTORY    *pTLS =
            (IMAGE_TLS_DIRECTORY *)(pBase + tls_dir->VirtualAddress);
        PIMAGE_TLS_CALLBACK *pCallback =
            (PIMAGE_TLS_CALLBACK *)pTLS->AddressOfCallBacks;

        for (; pCallback && *pCallback; pCallback++)
            (*pCallback)(pBase, DLL_PROCESS_ATTACH, NULL);
    }

    BOOL sehFailed = FALSE;

#ifdef _WIN64
    if (pData->SEHSupport) {
        IMAGE_DATA_DIRECTORY *excep_dir =
            &pOpt->DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION];
        if (excep_dir->Size) {
            if (!_RtlAddFunctionTable(
                    (RUNTIME_FUNCTION *)(pBase + excep_dir->VirtualAddress),
                    excep_dir->Size / sizeof(RUNTIME_FUNCTION),
                    (DWORD64)pBase))
                sehFailed = TRUE;
        }
    }
#endif

    _DllMain(pBase, pData->fdwReasonParam, pData->reservedParam);

    pData->hMod = sehFailed ? MM_SENTINEL_SEH_FAILED : (HINSTANCE)pBase;
}
