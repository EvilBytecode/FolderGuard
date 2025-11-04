#include "process.h"
#include "trusted_processes.h"
#include <fltKernel.h>
#include <ntimage.h>
#include <ntifs.h>
#include <ntstrsafe.h>

#pragma warning(push)
#pragma warning(disable: 4100) 
#pragma warning(disable: 4201)

static CHAR* FindSubstring(CHAR* str, const CHAR* substr) {
    if (!str || !substr) return nullptr;
    ULONG len = 0;
    while (substr[len] != '\0' && len < 512) {
        len++;
    }
    if (len == 0) return str;
    
    for (CHAR* p = str; *p && (p - str) < 512; p++) {
        if (RtlCompareMemory(p, substr, len) == len) {
            return p;
        }
    }
    return nullptr;
}

static CHAR* FindChar(CHAR* str, CHAR ch) {
    if (!str) return nullptr;
    for (CHAR* p = str; *p; p++) {
        if (*p == ch) return p;
    }
    return nullptr;
}

#ifndef PROCESS_QUERY_INFORMATION
#define PROCESS_QUERY_INFORMATION 0x0400
#endif
#ifndef PROCESS_QUERY_LIMITED_INFORMATION
#define PROCESS_QUERY_LIMITED_INFORMATION 0x1000
#endif

#define ProcessImageFileName ((PROCESSINFOCLASS)27)
#define ProcessBasicInformation ((PROCESSINFOCLASS)0)
#define ProcessHandleInformation ((PROCESSINFOCLASS)51)
#define ProcessSignatureInformation ((PROCESSINFOCLASS)75)

#ifndef PROCESS_BASIC_INFORMATION_DEFINED
#define PROCESS_BASIC_INFORMATION_DEFINED
typedef struct _PROCESS_BASIC_INFORMATION_LOCAL {
    NTSTATUS ExitStatus;
    PVOID PebBaseAddress;
    KAFFINITY AffinityMask;
    KPRIORITY BasePriority;
    HANDLE UniqueProcessId;
    HANDLE InheritedFromUniqueProcessId;
} PROCESS_BASIC_INFORMATION_LOCAL, *PPROCESS_BASIC_INFORMATION_LOCAL;
#endif

typedef struct _PEB_LDR_DATA {
    ULONG Length;
    BOOLEAN Initialized;
    PVOID SsHandle;
    LIST_ENTRY InLoadOrderModuleList;
    LIST_ENTRY InMemoryOrderModuleList;
    LIST_ENTRY InInitializationOrderModuleList;
    PVOID EntryInProgress;
} PEB_LDR_DATA, *PPEB_LDR_DATA;

typedef struct _PEB_LOCAL {
    BOOLEAN InheritedAddressSpace;
    BOOLEAN ReadImageFileExecOptions;
    BOOLEAN BeingDebugged;
    union {
        BOOLEAN BitField;
    };
    PVOID Mutant;
    PVOID ImageBaseAddress;
    PPEB_LDR_DATA Ldr;
    PVOID ProcessParameters;
    PVOID SubSystemData;
    PVOID ProcessHeap;
} PEB_LOCAL, *PPEB_LOCAL;

typedef struct _PROCESS_HANDLE_INFORMATION {
    ULONG HandleCount;
} PROCESS_HANDLE_INFORMATION, *PPROCESS_HANDLE_INFORMATION;

typedef enum _SIGNATURE_LEVEL {
    SIG_LEVEL_UNCHECKED = 0,
    SIG_LEVEL_UNSIGNED = 1,
    SIG_LEVEL_ENTERPRISE = 2,
    SIG_LEVEL_CUSTOM_1 = 3,
    SIG_LEVEL_AUTHENTICODE = 4,
    SIG_LEVEL_CUSTOM_2 = 5,
    SIG_LEVEL_STORE = 6,
    SIG_LEVEL_CUSTOM_3 = 7,
    SIG_LEVEL_ANTIMALWARE = 8,
    SIG_LEVEL_MICROSOFT = 9,
    SIG_LEVEL_CUSTOM_4 = 10,
    SIG_LEVEL_CUSTOM_5 = 11,
    SIG_LEVEL_DYNAMIC_CODEGEN = 12,
    SIG_LEVEL_WINDOWS = 13,
    SIG_LEVEL_CUSTOM_7 = 14,
    SIG_LEVEL_WINDOWS_TCB = 15,
    SIG_LEVEL_CUSTOM_6 = 16
} SIGNATURE_LEVEL, *PSIGNATURE_LEVEL;

typedef struct _PS_PROTECTION {
    union {
        UCHAR Level;
        struct {
            UCHAR Type : 3;
            UCHAR Audit : 1;
            UCHAR Signer : 4;
        } s;
    } u;
} PS_PROTECTION, *PPS_PROTECTION;

typedef struct _PROCESS_SIGNATURE_INFORMATION {
    PS_PROTECTION Protection;
    UCHAR SignatureLevel;
    UCHAR SectionSignatureLevel;
    UCHAR Reserved;
} PROCESS_SIGNATURE_INFORMATION, *PPROCESS_SIGNATURE_INFORMATION;

typedef struct _PROCESS_IMAGE_NAME_INFORMATION {
    UNICODE_STRING ImageName;
} PROCESS_IMAGE_NAME_INFORMATION, *PPROCESS_IMAGE_NAME_INFORMATION;

extern "C" {
    NTKERNELAPI PSTR PsGetProcessImageFileName(_In_ PEPROCESS Process);
    NTKERNELAPI PPEB PsGetProcessPeb(_In_ PEPROCESS Process);
    NTKERNELAPI PVOID PsGetProcessSectionBaseAddress(_In_ PEPROCESS Process);
    NTKERNELAPI LONGLONG PsGetProcessCreateTimeQuadPart(_In_ PEPROCESS Process);
    NTKERNELAPI NTSTATUS NTAPI ZwQueryInformationProcess(
        _In_ HANDLE ProcessHandle,
        _In_ PROCESSINFOCLASS ProcessInformationClass,
        _Out_ PVOID ProcessInformation,
        _In_ ULONG ProcessInformationLength,
        _Out_opt_ PULONG ReturnLength
    );
    NTKERNELAPI NTSTATUS PsLookupProcessByProcessId(
        _In_ HANDLE ProcessId,
        _Out_ PEPROCESS* Process
    );
}

static CHAR CharToLower(CHAR c) {
    return (c >= 'A' && c <= 'Z') ? (c + 32) : c;
}

namespace NoMoreStealer {
    namespace Process {

        NTSTATUS GetProcessImageName(
            PEPROCESS Process,
            PCHAR Buffer,
            ULONG BufferSize,
            PULONG ReturnLength
        ) {
            if (!Process || !Buffer || BufferSize == 0 || !ReturnLength) {
                return STATUS_INVALID_PARAMETER;
            }

            *ReturnLength = 0;

            HANDLE processHandle = nullptr;
            NTSTATUS status = ObOpenObjectByPointer(
                Process,
                OBJ_KERNEL_HANDLE,
                nullptr,
                PROCESS_QUERY_LIMITED_INFORMATION,
                *PsProcessType,
                KernelMode,
                &processHandle
            );

            if (!NT_SUCCESS(status)) {
                return status;
            }

            PROCESS_IMAGE_NAME_INFORMATION imageInfo = { 0 };
            ULONG returnLength = 0;

            __try {
                status = ZwQueryInformationProcess(
                    processHandle,
                    ProcessImageFileName,
                    &imageInfo,
                    sizeof(imageInfo),
                    &returnLength
                );

                if (NT_SUCCESS(status) && imageInfo.ImageName.Buffer && imageInfo.ImageName.Length > 0) {
                    ANSI_STRING ansiBuffer = { 0 };
                    ansiBuffer.Buffer = Buffer;
                    ansiBuffer.MaximumLength = (USHORT)BufferSize;
                    
                    status = RtlUnicodeStringToAnsiString(
                        &ansiBuffer,
                        &imageInfo.ImageName,
                        FALSE
                    );

                    if (NT_SUCCESS(status)) {
                        *ReturnLength = (ULONG)strlen(Buffer);
                    }
                }
            } __except(EXCEPTION_EXECUTE_HANDLER) {
                status = STATUS_UNSUCCESSFUL;
            }

            ZwClose(processHandle);
            return status;
        }

        BOOLEAN VerifyTrustedProcessPath(PEPROCESS process, const CHAR* fileName) {
            if (!process || !fileName) return FALSE;

            CHAR procPath[512] = { 0 };
            ULONG pathLen = 0;
            NTSTATUS status = STATUS_UNSUCCESSFUL;
            
            __try {
                status = GetProcessImageName(process, procPath, sizeof(procPath) - 1, &pathLen);
            } __except(EXCEPTION_EXECUTE_HANDLER) {
                status = STATUS_UNSUCCESSFUL;
            }
            
            if (!NT_SUCCESS(status) || pathLen == 0) {
                for (ULONG i = 0; i < Trusted::PROCESS_PATH_MAPPINGS_COUNT; i++) {
                    if (!_stricmp(fileName, Trusted::PROCESS_PATH_MAPPINGS[i].processName)) {
                        return TRUE;
                    }
                }
                return FALSE;
            }

            CHAR pathLower[512] = { 0 };
            ULONG copyLen = 0;
            while (procPath[copyLen] != '\0' && copyLen < sizeof(pathLower) - 1) {
                CHAR c = procPath[copyLen];
                if (c >= 'A' && c <= 'Z') {
                    pathLower[copyLen] = (CHAR)(c + 32);
                } else {
                    pathLower[copyLen] = c;
                }
                copyLen++;
            }
            pathLower[copyLen] = '\0';

            for (ULONG i = 0; i < Trusted::PROCESS_PATH_MAPPINGS_COUNT; i++) {
                if (!_stricmp(fileName, Trusted::PROCESS_PATH_MAPPINGS[i].processName)) {
                    BOOLEAN foundMatch = FALSE;
                    for (ULONG j = 0; j < Trusted::PROCESS_PATH_MAPPINGS[i].pathCount; j++) {
                        if (Trusted::PROCESS_PATH_MAPPINGS[i].paths[j] != nullptr) {
                            CHAR trustedPathLower[256] = { 0 };
                            ULONG trustedPathLen = 0;
                            const CHAR* trustedPath = Trusted::PROCESS_PATH_MAPPINGS[i].paths[j];
                            ULONG trustedPathOffset = 0;
                            if (trustedPath[0] == '\\' && trustedPath[1] == '\\') {
                                trustedPathOffset = 1;
                            }
                            while (trustedPath[trustedPathOffset + trustedPathLen] != '\0' && trustedPathLen < sizeof(trustedPathLower) - 1) {
                                CHAR tc = trustedPath[trustedPathOffset + trustedPathLen];
                                if (tc >= 'A' && tc <= 'Z') {
                                    trustedPathLower[trustedPathLen] = (CHAR)(tc + 32);
                                } else {
                                    trustedPathLower[trustedPathLen] = tc;
                                }
                                trustedPathLen++;
                            }
                            trustedPathLower[trustedPathLen] = '\0';
                            
                            CHAR* found = FindSubstring(pathLower, trustedPathLower);
                            if (found != nullptr) {
                                foundMatch = TRUE;
                                break;
                            }
                        }
                    }
                    return foundMatch;
                }
            }

            return FALSE;
        }

        BOOLEAN IsKnownTrustedProcess(PEPROCESS process) {
            if (!process) return FALSE;

            CHAR procNameBuffer[512] = { 0 };
            const CHAR* fileName = nullptr;
            BOOLEAN isTruncated = FALSE;
            
            ULONG nameLength = 0;
            NTSTATUS nameStatus = STATUS_UNSUCCESSFUL;
            
            __try {
                nameStatus = GetProcessImageName(process, procNameBuffer, sizeof(procNameBuffer) - 1, &nameLength);
            } __except(EXCEPTION_EXECUTE_HANDLER) {
                nameStatus = STATUS_UNSUCCESSFUL;
            }
            
            if (NT_SUCCESS(nameStatus) && nameLength > 0) {
                ANSI_STRING ansiImage = { 0 };
                RtlInitAnsiString(&ansiImage, procNameBuffer);
                
                CHAR* lastSlash = nullptr;
                for (ULONG i = 0; i < ansiImage.Length; i++) {
                    if (ansiImage.Buffer[i] == '\\') {
                        lastSlash = &ansiImage.Buffer[i];
                    }
                }
                fileName = lastSlash ? (lastSlash + 1) : procNameBuffer;
            } else {
                __try {
                    const CHAR* imageFileName = PsGetProcessImageFileName(process);
                    if (imageFileName && imageFileName[0] != '\0') {
                        fileName = imageFileName;
                        ULONG truncatedLen = 0;
                        while (imageFileName[truncatedLen] != '\0' && truncatedLen < 16) {
                            truncatedLen++;
                        }
                        if (truncatedLen == 15 && imageFileName[14] != '\0') {
                            isTruncated = TRUE;
                        }
                    } else {
                        return FALSE;
                    }
                } __except(EXCEPTION_EXECUTE_HANDLER) {
                    return FALSE;
                }
            }
            
            if (!fileName || fileName[0] == '\0') return FALSE;

            for (ULONG i = 0; i < Trusted::TRUSTED_PROCESSES_COUNT; i++) {
                const CHAR* trustedName = Trusted::TRUSTED_PROCESSES[i];
                ULONG trustedNameLen = 0;
                while (trustedName[trustedNameLen] != '\0' && trustedNameLen < 256) {
                    trustedNameLen++;
                }
                
                if (isTruncated && trustedNameLen > 15) {
                    if (!_strnicmp(fileName, trustedName, 15)) {
                        if (!VerifyTrustedProcessPath(process, trustedName)) {
                            return FALSE;
                        }
                        return TRUE;
                    }
                } else {
                    if (!_stricmp(fileName, trustedName)) {
                        if (!VerifyTrustedProcessPath(process, fileName)) {
                            return FALSE;
                        }
                        return TRUE;
                    }
                }
            }

            if (fileName) {
                ULONG fileNameLen = 0;
                while (fileName[fileNameLen] != '\0' && fileNameLen < 256) {
                    fileNameLen++;
                }
                if (fileNameLen >= 14) {
                    if (!_strnicmp(fileName, "monero-wallet-", 14)) {
                        return TRUE;
                    }
                }
            }

            return FALSE;
        }

        BOOLEAN IsSystemProcess(PEPROCESS process) {
            if (!process) return FALSE;

            if (PsGetProcessId(process) == (HANDLE)4) return TRUE;

            CHAR procNameBuffer[512] = { 0 };
            const CHAR* fileName = nullptr;
            
            ULONG nameLength = 0;
            NTSTATUS nameStatus = STATUS_UNSUCCESSFUL;
            
            __try {
                nameStatus = GetProcessImageName(process, procNameBuffer, sizeof(procNameBuffer) - 1, &nameLength);
            } __except(EXCEPTION_EXECUTE_HANDLER) {
                nameStatus = STATUS_UNSUCCESSFUL;
            }
            
            if (NT_SUCCESS(nameStatus) && nameLength > 0) {
                ANSI_STRING ansiImage = { 0 };
                RtlInitAnsiString(&ansiImage, procNameBuffer);
                
                CHAR* lastSlash = nullptr;
                for (ULONG i = 0; i < ansiImage.Length; i++) {
                    if (ansiImage.Buffer[i] == '\\') {
                        lastSlash = &ansiImage.Buffer[i];
                    }
                }
                fileName = lastSlash ? (lastSlash + 1) : procNameBuffer;
            } else {
                __try {
                    const CHAR* imageFileName = PsGetProcessImageFileName(process);
                    if (imageFileName && imageFileName[0] != '\0') {
                        fileName = imageFileName;
                    } else {
                        return FALSE;
                    }
                } __except(EXCEPTION_EXECUTE_HANDLER) {
                    return FALSE;
                }
            }

            if (!fileName || fileName[0] == '\0') {
                return FALSE;
            }

            for (ULONG i = 0; i < Trusted::SYSTEM_PROCESSES_COUNT; i++) {
                if (!_stricmp(fileName, Trusted::SYSTEM_PROCESSES[i])) {
                    return VerifyProcessPath(process, "c:\\windows\\system32\\") ||
                           VerifyProcessPath(process, "c:\\windows\\systemapps\\") ||
                           VerifyProcessPath(process, "c:\\windows\\");
                }
            }

            return FALSE;
        }

        BOOLEAN IsProcessProtected(PEPROCESS process) {
            if (!process) return FALSE;

            HANDLE hProcess = nullptr;
            NTSTATUS status = ObOpenObjectByPointer(
                process,
                OBJ_KERNEL_HANDLE,
                nullptr,
                PROCESS_QUERY_LIMITED_INFORMATION,
                *PsProcessType,
                KernelMode,
                &hProcess
            );

            if (!NT_SUCCESS(status)) return FALSE;

            PROCESS_SIGNATURE_INFORMATION sigInfo = { 0 };
            ULONG returnLength = 0;

            status = ZwQueryInformationProcess(
                hProcess,
                ProcessSignatureInformation,
                &sigInfo,
                sizeof(sigInfo),
                &returnLength
            );

            ZwClose(hProcess);

            if (!NT_SUCCESS(status)) return FALSE;

            return (sigInfo.SignatureLevel >= SIG_LEVEL_MICROSOFT);
        }

        BOOLEAN IsLegitimateProcess(PEPROCESS process) {
            if (!process) return FALSE;

            __try {
                PVOID imageBase = PsGetProcessSectionBaseAddress(process);
                if (!imageBase || imageBase == (PVOID)0xFFFFFFFF || imageBase == (PVOID)0x10000) {
                    return FALSE;
                }

                PPEB peb = PsGetProcessPeb(process);
                if (!peb) {
                    return FALSE;
                }
            } __except(EXCEPTION_EXECUTE_HANDLER) {
                return FALSE;
            }

            return TRUE;
        }

        BOOLEAN IsProcessFromTempDirectory(PEPROCESS process) {
            if (!process) return FALSE;

            CHAR procPath[512] = { 0 };
            ULONG pathLen = 0;
            NTSTATUS status = STATUS_UNSUCCESSFUL;
            
            __try {
                status = GetProcessImageName(process, procPath, sizeof(procPath) - 1, &pathLen);
            } __except(EXCEPTION_EXECUTE_HANDLER) {
                status = STATUS_UNSUCCESSFUL;
            }
            
            if (!NT_SUCCESS(status) || pathLen == 0) {
                return FALSE;
            }

            if (FindSubstring(procPath, "\\Temp\\") ||
                FindSubstring(procPath, "\\AppData\\Local\\Temp\\") ||
                FindSubstring(procPath, "\\Windows\\Temp\\")) {
                return TRUE;
            }

            return FALSE;
        }

        BOOLEAN IsProcessFromAppDataLocal(PEPROCESS process) {
            if (!process) return FALSE;

            CHAR procPath[512] = { 0 };
            ULONG pathLen = 0;
            NTSTATUS status = STATUS_UNSUCCESSFUL;
            
            __try {
                status = GetProcessImageName(process, procPath, sizeof(procPath) - 1, &pathLen);
            } __except(EXCEPTION_EXECUTE_HANDLER) {
                status = STATUS_UNSUCCESSFUL;
            }
            
            if (!NT_SUCCESS(status) || pathLen == 0) {
                return FALSE;
            }

            CHAR* appDataLocal = FindSubstring(procPath, "\\AppData\\Local\\");
            if (appDataLocal) {
                CHAR* afterLocal = appDataLocal + 16;
                if (*afterLocal != '\0') {
                    CHAR* nextSlash = FindChar(afterLocal, '\\');
                    if (!nextSlash) {
                        return TRUE;
                    }
                    
                    ULONG dirLen = (ULONG)(nextSlash - afterLocal);
                    if (dirLen >= 3) {
                        return TRUE;
                    }
                }
            }

            return FALSE;
        }

        BOOLEAN VerifyProcessIntegrity(PEPROCESS process) {
            if (!process) return FALSE;

            HANDLE processHandle = nullptr;
            NTSTATUS status = ObOpenObjectByPointer(
                process,
                OBJ_KERNEL_HANDLE,
                nullptr,
                PROCESS_QUERY_LIMITED_INFORMATION,
                *PsProcessType,
                KernelMode,
                &processHandle
            );

            if (!NT_SUCCESS(status)) {
                return TRUE;
            }

            PROCESS_BASIC_INFORMATION_LOCAL basicInfo = { 0 };
            ULONG returnLength = 0;
            status = ZwQueryInformationProcess(
                processHandle,
                ProcessBasicInformation,
                &basicInfo,
                sizeof(basicInfo),
                &returnLength
            );

            BOOLEAN hasValidParent = FALSE;
            BOOLEAN hasValidAge = FALSE;

            if (NT_SUCCESS(status) && basicInfo.InheritedFromUniqueProcessId) {
                HANDLE parentPid = basicInfo.InheritedFromUniqueProcessId;
                
                if (parentPid == (HANDLE)4) {
                    ZwClose(processHandle);
                    return FALSE;
                }

                PEPROCESS parentProcess = nullptr;
                if (NT_SUCCESS(PsLookupProcessByProcessId(parentPid, &parentProcess))) {
                    CHAR parentNameBuffer[256] = { 0 };
                    ULONG parentNameLength = 0;
                    NTSTATUS parentNameStatus = STATUS_UNSUCCESSFUL;
                    
                    __try {
                        parentNameStatus = GetProcessImageName(parentProcess, parentNameBuffer, sizeof(parentNameBuffer) - 1, &parentNameLength);
                    } __except(EXCEPTION_EXECUTE_HANDLER) {
                        parentNameStatus = STATUS_UNSUCCESSFUL;
                    }
                    
                    if (NT_SUCCESS(parentNameStatus) && parentNameLength > 0) {
                        ANSI_STRING ansiParentName = { 0 };
                        RtlInitAnsiString(&ansiParentName, parentNameBuffer);
                        
                        CHAR* parentLastSlash = nullptr;
                        for (ULONG i = 0; i < ansiParentName.Length; i++) {
                            if (ansiParentName.Buffer[i] == '\\') {
                                parentLastSlash = &ansiParentName.Buffer[i];
                            }
                        }
                        const CHAR* parentFileName = parentLastSlash ? (parentLastSlash + 1) : parentNameBuffer;

                        for (ULONG i = 0; i < Trusted::TRUSTED_PARENTS_COUNT; i++) {
                            if (!_stricmp(parentFileName, Trusted::TRUSTED_PARENTS[i])) {
                                hasValidParent = TRUE;
                                break;
                            }
                        }
                        
                        if (!hasValidParent) {
                            CHAR parentCheckBuffer[512] = { 0 };
                            ULONG parentCheckLength = 0;
                            NTSTATUS parentCheckStatus = STATUS_UNSUCCESSFUL;
                            
                            __try {
                                parentCheckStatus = GetProcessImageName(parentProcess, parentCheckBuffer, sizeof(parentCheckBuffer) - 1, &parentCheckLength);
                            } __except(EXCEPTION_EXECUTE_HANDLER) {
                                parentCheckStatus = STATUS_UNSUCCESSFUL;
                            }
                            
                            if (NT_SUCCESS(parentCheckStatus) && parentCheckLength > 0) {
                                ANSI_STRING ansiParentCheck = { 0 };
                                RtlInitAnsiString(&ansiParentCheck, parentCheckBuffer);
                                CHAR* parentCheckLastSlash = nullptr;
                                for (ULONG j = 0; j < ansiParentCheck.Length; j++) {
                                    if (ansiParentCheck.Buffer[j] == '\\') {
                                        parentCheckLastSlash = &ansiParentCheck.Buffer[j];
                                    }
                                }
                                const CHAR* parentCheckFileName = parentCheckLastSlash ? (parentCheckLastSlash + 1) : parentCheckBuffer;
                                for (ULONG j = 0; j < Trusted::TRUSTED_PROCESSES_COUNT; j++) {
                                    if (!_stricmp(parentCheckFileName, Trusted::TRUSTED_PROCESSES[j])) {
                                        hasValidParent = TRUE;
                                        break;
                                    }
                                }
                            }
                        }
                    }
                    
                    ObDereferenceObject(parentProcess);
                }
            }

            LARGE_INTEGER createTime = { 0 };
            LARGE_INTEGER currentTime = { 0 };
            KeQuerySystemTime(&currentTime);

            LONGLONG createTimeQuadPart = PsGetProcessCreateTimeQuadPart(process);
            if (createTimeQuadPart > 0) {
                createTime.QuadPart = createTimeQuadPart;
                LARGE_INTEGER timeDiff = { 0 };
                timeDiff.QuadPart = currentTime.QuadPart - createTime.QuadPart;
                
                LARGE_INTEGER minAge = { 0 };
                minAge.QuadPart = 10000000LL;
                
                if (timeDiff.QuadPart >= minAge.QuadPart) {
                    hasValidAge = TRUE;
                }
            } else {
                hasValidAge = TRUE;
            }

            ULONG handleCount = 0;
            ULONG moduleCount = 0;

            __try {
                PPEB pebPtr = PsGetProcessPeb(process);
                if (!pebPtr) {
                    ZwClose(processHandle);
                    return TRUE;
                }

                PPEB_LOCAL peb = (PPEB_LOCAL)pebPtr;
                if (!peb || !peb->Ldr) {
                    ZwClose(processHandle);
                    return TRUE;
                }

                PPEB_LDR_DATA ldr = peb->Ldr;
                if (ldr && ldr->InLoadOrderModuleList.Flink) {
                    PLIST_ENTRY moduleList = &ldr->InLoadOrderModuleList;
                    PLIST_ENTRY entry = moduleList->Flink;
                    ULONG maxModules = 1000;
                    
                    while (entry != moduleList && moduleCount < maxModules) {
                        moduleCount++;
                        entry = entry->Flink;
                    }
                }
            } __except(EXCEPTION_EXECUTE_HANDLER) {
                ZwClose(processHandle);
                return TRUE;
            }

            PROCESS_HANDLE_INFORMATION handleInfo = { 0 };
            ULONG returnLen = 0;
            if (NT_SUCCESS(ZwQueryInformationProcess(processHandle, ProcessHandleInformation, &handleInfo, sizeof(handleInfo), &returnLen))) {
                handleCount = handleInfo.HandleCount;
            }

            if (moduleCount == 0 && handleCount == 0) {
                ZwClose(processHandle);
                return FALSE;
            }

            BOOLEAN hasValidPE = FALSE;
            PVOID imageBase = PsGetProcessSectionBaseAddress(process);
            if (imageBase) {
                __try {
                    PIMAGE_DOS_HEADER dosHeader = (PIMAGE_DOS_HEADER)imageBase;
                    if (dosHeader->e_magic == IMAGE_DOS_SIGNATURE) {
                        LONG e_lfanew = dosHeader->e_lfanew;
                        if (e_lfanew > 0 && e_lfanew < 0x100000 && 
                            (ULONG_PTR)e_lfanew < 0x100000 - sizeof(IMAGE_NT_HEADERS)) {
                            PIMAGE_NT_HEADERS ntHeaders = (PIMAGE_NT_HEADERS)((PUCHAR)imageBase + e_lfanew);
                            if (ntHeaders->Signature == IMAGE_NT_SIGNATURE) {
                                USHORT characteristics = ntHeaders->FileHeader.Characteristics;
                                if (!(characteristics & IMAGE_FILE_DLL) && 
                                    (characteristics & IMAGE_FILE_EXECUTABLE_IMAGE)) {
                                    if (ntHeaders->OptionalHeader.DllCharacteristics & IMAGE_DLLCHARACTERISTICS_NX_COMPAT) {
                                        hasValidPE = TRUE;
                                    }
                                }
                            }
                        }
                    }
                } __except(EXCEPTION_EXECUTE_HANDLER) {
                }
            }

            if (hasValidPE || hasValidParent || moduleCount >= 3 || handleCount >= 5) {
                ZwClose(processHandle);
                return TRUE;
            }

            PACCESS_TOKEN token = PsReferencePrimaryToken(process);
            if (token) {
                PVOID tokenInfo = nullptr;
                if (NT_SUCCESS(SeQueryInformationToken(token, TokenStatistics, &tokenInfo))) {
                    if (tokenInfo) {
                        PTOKEN_STATISTICS tokenStats = (PTOKEN_STATISTICS)tokenInfo;
                        if (tokenStats->TokenType == TokenImpersonation && tokenStats->ImpersonationLevel == SecurityAnonymous) {
                            ExFreePool(tokenInfo);
                            PsDereferencePrimaryToken(token);
                            ZwClose(processHandle);
                            return FALSE;
                        }
                        ExFreePool(tokenInfo);
                    }
                }
                PsDereferencePrimaryToken(token);
            }

            ZwClose(processHandle);
            return TRUE;
        }

        BOOLEAN VerifySystemProcessIntegrity(PEPROCESS process) {
            if (!process) return FALSE;

            __try {
                PVOID imageBase = PsGetProcessSectionBaseAddress(process);
                if (!imageBase || imageBase == (PVOID)0xFFFFFFFF || imageBase == (PVOID)0x10000) {
                    return FALSE;
                }

                PPEB peb = PsGetProcessPeb(process);
                if (!peb) {
                    return FALSE;
                }
            } __except(EXCEPTION_EXECUTE_HANDLER) {
                return FALSE;
            }

            return TRUE;
        }

        BOOLEAN VerifyProcessPath(PEPROCESS process, const CHAR* expectedPath) {
            if (!process || !expectedPath) return FALSE;

            CHAR procPath[512] = { 0 };
            ULONG pathLen = 0;
            NTSTATUS status = STATUS_UNSUCCESSFUL;
            
            __try {
                status = GetProcessImageName(process, procPath, sizeof(procPath) - 1, &pathLen);
            } __except(EXCEPTION_EXECUTE_HANDLER) {
                status = STATUS_UNSUCCESSFUL;
            }
            
            if (!NT_SUCCESS(status) || pathLen == 0) {
                return FALSE;
            }

            ANSI_STRING ansiProcPath = { 0 };
            RtlInitAnsiString(&ansiProcPath, procPath);
            
            ANSI_STRING ansiExpectedPath = { 0 };
            RtlInitAnsiString(&ansiExpectedPath, expectedPath);
            
            if (ansiProcPath.Length < ansiExpectedPath.Length) {
                return FALSE;
            }

            for (ULONG i = 0; i < ansiExpectedPath.Length; i++) {
                CHAR procChar = (CHAR)CharToLower(ansiProcPath.Buffer[i]);
                CHAR expectedChar = (CHAR)CharToLower(ansiExpectedPath.Buffer[i]);
                if (procChar != expectedChar) {
                    return FALSE;
                }
            }

            return TRUE;
        }

        BOOLEAN IsAllowed(PEPROCESS process) {
            if (!process) return FALSE;

            if (IsSystemProcess(process)) {
                if (VerifySystemProcessIntegrity(process)) {
                    return TRUE;
                }
                return FALSE;
            }

            if (IsKnownTrustedProcess(process)) {
                BOOLEAN isProtected = IsProcessProtected(process);
                if (isProtected) {
                    return TRUE;
                }
                
                CHAR procPath[512] = { 0 };
                ULONG pathLen = 0;
                NTSTATUS pathStatus = STATUS_UNSUCCESSFUL;
                
                __try {
                    pathStatus = GetProcessImageName(process, procPath, sizeof(procPath) - 1, &pathLen);
                } __except(EXCEPTION_EXECUTE_HANDLER) {
                    pathStatus = STATUS_UNSUCCESSFUL;
                }
                
                if (NT_SUCCESS(pathStatus) && pathLen > 0) {
                    CHAR pathLower[512] = { 0 };
                    ULONG copyLen = 0;
                    while (procPath[copyLen] != '\0' && copyLen < sizeof(pathLower) - 1) {
                        CHAR c = procPath[copyLen];
                        if (c >= 'A' && c <= 'Z') {
                            pathLower[copyLen] = (CHAR)(c + 32);
                        } else {
                            pathLower[copyLen] = c;
                        }
                        copyLen++;
                    }
                    pathLower[copyLen] = '\0';
                    
                    if (FindSubstring(pathLower, "\\temp\\") ||
                        FindSubstring(pathLower, "\\appdata\\local\\temp\\") ||
                        FindSubstring(pathLower, "\\windows\\temp\\")) {
                        return FALSE;
                    }
                }
                
                __try {
                    PVOID imageBase = PsGetProcessSectionBaseAddress(process);
                    if (!imageBase || imageBase == (PVOID)0xFFFFFFFF || imageBase == (PVOID)0x10000) {
                        return FALSE;
                    }
                } __except(EXCEPTION_EXECUTE_HANDLER) {
                    return FALSE;
                }
                
                if (!VerifyProcessIntegrity(process)) {
                    return FALSE;
                }
                
                return TRUE;
            }

            if (IsProcessProtected(process))
                return TRUE;

            if (IsProcessFromTempDirectory(process)) {
                return FALSE;
            }

            if (IsProcessFromAppDataLocal(process)) {
                return FALSE;
            }
            
            if (!IsLegitimateProcess(process)) {
                return FALSE;
            }

            return FALSE;
        }

    }
}

#pragma warning(pop)
