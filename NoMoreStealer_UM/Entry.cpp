#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <strsafe.h>
#include <wintrust.h>
#include <softpub.h>
#include <winternl.h>
#include <wincrypt.h>
#include <psapi.h>

#pragma comment(lib, "ntdll.lib")
#pragma comment(lib, "wintrust.lib")
#pragma comment(lib, "crypt32.lib")
#pragma comment(lib, "psapi.lib")

#define FG_SECTION_NAME L"\\BaseNamedObjects\\NoMoreStealerNotify"
#define APP_ICON_ID 1001

#ifndef NT_SUCCESS
#define NT_SUCCESS(Status) (((NTSTATUS)(Status)) >= 0)
#endif

#pragma pack(push, 1)
struct NoMoreStealerNotifyData {
    ULONG pid;
    ULONG pathLen;
    CHAR procName[64];
    ULONG ready;
};
#pragma pack(pop)

extern "C" {
    NTSTATUS NTAPI NtOpenSection(PHANDLE, ACCESS_MASK, POBJECT_ATTRIBUTES);
    NTSTATUS NTAPI NtMapViewOfSection(HANDLE, HANDLE, PVOID*, ULONG_PTR, SIZE_T, PLARGE_INTEGER, PSIZE_T, ULONG, ULONG, ULONG);
    NTSTATUS NTAPI NtUnmapViewOfSection(HANDLE, PVOID);
    NTSTATUS NTAPI NtClose(HANDLE);
}

// Convert device path to DOS path using hardcoded mappings
BOOL DevicePathToDosPath(const wchar_t* devicePath, wchar_t* dosPath, size_t dosPathSize) {
    // Hardcode common mappings
    if (wcsncmp(devicePath, L"\\Device\\HarddiskVolume3", 22) == 0) {
        StringCchPrintfW(dosPath, dosPathSize, L"C:%s", devicePath + 22);
        return TRUE;
    }
    else if (wcsncmp(devicePath, L"\\Device\\HarddiskVolume1", 22) == 0) {
        StringCchPrintfW(dosPath, dosPathSize, L"C:%s", devicePath + 22);
        return TRUE;
    }
    else if (wcsncmp(devicePath, L"\\Device\\HarddiskVolume2", 22) == 0) {
        StringCchPrintfW(dosPath, dosPathSize, L"D:%s", devicePath + 22);
        return TRUE;
    }
    else if (wcsncmp(devicePath, L"\\Device\\HarddiskVolume4", 22) == 0) {
        StringCchPrintfW(dosPath, dosPathSize, L"E:%s", devicePath + 22);
        return TRUE;
    }

    return FALSE;
}

// Alternative: Get file path from process ID with SeDebug privilege
BOOL GetFilePathFromPid(DWORD pid, wchar_t* filePath, size_t filePathSize) {
    HANDLE hToken = NULL;
    BOOL bResult = FALSE;

    // Try to enable SeDebugPrivilege
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken)) {
        TOKEN_PRIVILEGES tp;
        LUID luid;

        if (LookupPrivilegeValue(NULL, SE_DEBUG_NAME, &luid)) {
            tp.PrivilegeCount = 1;
            tp.Privileges[0].Luid = luid;
            tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

            AdjustTokenPrivileges(hToken, FALSE, &tp, sizeof(TOKEN_PRIVILEGES), NULL, NULL);
        }
        CloseHandle(hToken);
    }

    HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    if (hProcess == NULL) {
        hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (hProcess == NULL) {
            return FALSE;
        }
    }

    DWORD result = GetModuleFileNameExW(hProcess, NULL, filePath, (DWORD)filePathSize);
    CloseHandle(hProcess);

    return (result > 0);
}

// Simple signature check that just looks for any digital signature
BOOL IsFileSigned(LPCWSTR filePath) {
    wchar_t dosPath[MAX_PATH * 2] = { 0 };
    const wchar_t* actualPath = filePath;

    // Check if it's a device path and convert to DOS path
    if (wcsncmp(filePath, L"\\Device\\", 8) == 0) {
        if (DevicePathToDosPath(filePath, dosPath, ARRAYSIZE(dosPath))) {
            actualPath = dosPath;
        }
        else {
            return FALSE;
        }
    }

    // Check if file exists
    DWORD attrs = GetFileAttributesW(actualPath);
    if (attrs == INVALID_FILE_ATTRIBUTES) {
        return FALSE;
    }

    // Method 1: Simple WinVerifyTrust with relaxed settings
    WINTRUST_FILE_INFO fileData = { 0 };
    fileData.cbStruct = sizeof(WINTRUST_FILE_INFO);
    fileData.pcwszFilePath = actualPath;

    WINTRUST_DATA winTrustData = { 0 };
    winTrustData.cbStruct = sizeof(winTrustData);
    winTrustData.dwUIChoice = WTD_UI_NONE;
    winTrustData.fdwRevocationChecks = WTD_REVOKE_NONE;
    winTrustData.dwUnionChoice = WTD_CHOICE_FILE;
    winTrustData.pFile = &fileData;
    winTrustData.dwStateAction = WTD_STATEACTION_VERIFY;
    winTrustData.dwProvFlags = WTD_REVOCATION_CHECK_NONE;

    GUID policyGUID = WINTRUST_ACTION_GENERIC_VERIFY_V2;
    LONG status = WinVerifyTrust(NULL, &policyGUID, &winTrustData);

    // Clean up
    winTrustData.dwStateAction = WTD_STATEACTION_CLOSE;
    WinVerifyTrust(NULL, &policyGUID, &winTrustData);

    if (status == ERROR_SUCCESS) {
        return TRUE;
    }

    // Method 2: Try catalog verification for Windows files
    WINTRUST_CATALOG_INFO catalogInfo = { 0 };
    catalogInfo.cbStruct = sizeof(catalogInfo);
    catalogInfo.pcwszMemberTag = actualPath;

    winTrustData = { 0 };
    winTrustData.cbStruct = sizeof(winTrustData);
    winTrustData.dwUIChoice = WTD_UI_NONE;
    winTrustData.fdwRevocationChecks = WTD_REVOKE_NONE;
    winTrustData.dwUnionChoice = WTD_CHOICE_CATALOG;
    winTrustData.pCatalog = &catalogInfo;
    winTrustData.dwStateAction = WTD_STATEACTION_VERIFY;

    status = WinVerifyTrust(NULL, &policyGUID, &winTrustData);

    winTrustData.dwStateAction = WTD_STATEACTION_CLOSE;
    WinVerifyTrust(NULL, &policyGUID, &winTrustData);

    if (status == ERROR_SUCCESS) {
        return TRUE;
    }

    return FALSE;
}

void ShowBalloonNotification(HWND hwnd, const wchar_t* title, const wchar_t* text) {
    NOTIFYICONDATA nid = { sizeof(nid) };
    nid.hWnd = hwnd;
    nid.uID = APP_ICON_ID;
    nid.uFlags = NIF_INFO | NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.hIcon = LoadIcon(NULL, IDI_WARNING);
    StringCchCopy(nid.szTip, ARRAYSIZE(nid.szTip), L"NoMoreStealer");
    StringCchCopy(nid.szInfoTitle, ARRAYSIZE(nid.szInfoTitle), title);
    StringCchCopy(nid.szInfo, ARRAYSIZE(nid.szInfo), text);
    nid.dwInfoFlags = NIIF_WARNING;

    Shell_NotifyIcon(NIM_ADD, &nid);
    Shell_NotifyIcon(NIM_MODIFY, &nid);
    Sleep(3000);
    Shell_NotifyIcon(NIM_DELETE, &nid);
}

int wmain() {
    wprintf(L"[NotifyHelper] Starting...\n");

    UNICODE_STRING sectionName;
    OBJECT_ATTRIBUTES oa;
    RtlInitUnicodeString(&sectionName, FG_SECTION_NAME);
    InitializeObjectAttributes(&oa, &sectionName, OBJ_CASE_INSENSITIVE, nullptr, nullptr);

    HANDLE hSection = nullptr;
    NTSTATUS status = NtOpenSection(&hSection, SECTION_MAP_READ | SECTION_MAP_WRITE, &oa);
    if (!NT_SUCCESS(status)) {
        wprintf(L"[NotifyHelper] Failed to open section: 0x%08X\n", status);
        return 1;
    }

    PVOID baseAddr = nullptr;
    SIZE_T viewSize = 4096;
    status = NtMapViewOfSection(hSection, GetCurrentProcess(), &baseAddr, 0, 0, nullptr, &viewSize, 2, 0, PAGE_READWRITE);
    if (!NT_SUCCESS(status)) {
        NtClose(hSection);
        wprintf(L"[NotifyHelper] Failed to map section: 0x%08X\n", status);
        return 1;
    }

    NoMoreStealerNotifyData* notify = (NoMoreStealerNotifyData*)baseAddr;
    ULONG lastReady = 0;

    HWND hwnd = GetConsoleWindow();

    for (;;) {
        Sleep(100);
        ULONG currentReady = *(volatile ULONG*)&notify->ready;
        if (currentReady == 1 && lastReady == 0) {
            const WCHAR* pathW = (const WCHAR*)((PUCHAR)notify + sizeof(NoMoreStealerNotifyData));
            wchar_t pathLocal[MAX_PATH * 2] = { 0 };
            size_t pathChars = notify->pathLen / 2;
            if (pathChars >= ARRAYSIZE(pathLocal))
                pathChars = ARRAYSIZE(pathLocal) - 1;
            memcpy(pathLocal, pathW, pathChars * sizeof(WCHAR));
            pathLocal[pathChars] = 0;

            wchar_t wproc[64] = { 0 };
            if (notify->procName[0])
                MultiByteToWideChar(CP_ACP, 0, notify->procName, -1, wproc, ARRAYSIZE(wproc));

            // Try alternative method: Get file path from PID if device path conversion fails
            BOOL isSigned = FALSE;
            wchar_t altPath[MAX_PATH] = { 0 };

            if (GetFilePathFromPid(notify->pid, altPath, ARRAYSIZE(altPath))) {
                isSigned = IsFileSigned(altPath);
            }
            else {
                // Fall back to original device path conversion
                isSigned = IsFileSigned(pathLocal);
            }

            // Only show notification for unsigned processes
            if (!isSigned) {
                wchar_t balloonText[512];
                StringCchPrintf(balloonText, ARRAYSIZE(balloonText),
                    L"Process: %s (PID=%u)\nPath: %s\nStatus: UNSIGNED PROCESS BLOCKED",
                    wproc[0] ? wproc : L"(unknown)", notify->pid, pathLocal);

                ShowBalloonNotification(hwnd, L"NoMoreStealer Blocked Unsigned Process", balloonText);
                wprintf(L"[NotifyHelper] BLOCKED unsigned process: %s (PID: %u)\n", wproc[0] ? wproc : L"(unknown)", notify->pid);
            }
            else {
                wprintf(L"[NotifyHelper] ALLOWED signed process: %s (PID: %u)\n", wproc[0] ? wproc : L"(unknown)", notify->pid);
            }

            // Reset the ready flag regardless of signature status
            *(volatile ULONG*)&notify->ready = 0;
        }
        lastReady = currentReady;
    }

    NtUnmapViewOfSection(GetCurrentProcess(), baseAddr);
    NtClose(hSection);
    return 0;
}