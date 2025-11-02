#include <fltKernel.h>
#include <ntstrsafe.h>
#include "paths.h"

#define POOL_TAG 'tPMM'

namespace NoMoreStealer {  // ← Singular!
    namespace Paths {

        struct ProtectedPath {
            UNICODE_STRING Path;
            PVOID Buffer;
        };

        constexpr ULONG MAX_PROTECTED_PATHS = 128;
        static ProtectedPath gProtectedPaths[MAX_PROTECTED_PATHS] = { 0 };
        static ULONG gProtectedPathCount = 0;
        static ERESOURCE gPathsLock;

        void Init() {
            // ExInitializeResourceLite → veraltet → ersetze durch:
            NTSTATUS status = ExInitializeResourceLite(&gPathsLock);
            if (!NT_SUCCESS(status)) {
                DbgPrint("[NoMoreStealer] Paths: ExInitializeResourceLite failed 0x%08X\n", status);
            }
        }

        void Cleanup() {
            ExEnterCriticalRegionAndAcquireResourceExclusive(&gPathsLock);
            for (ULONG i = 0; i < gProtectedPathCount; i++) {
                if (gProtectedPaths[i].Buffer) {
                    ExFreePoolWithTag(gProtectedPaths[i].Buffer, POOL_TAG);
                }
                gProtectedPaths[i].Buffer = nullptr;
                RtlZeroMemory(&gProtectedPaths[i].Path, sizeof(UNICODE_STRING));
            }
            gProtectedPathCount = 0;
            ExReleaseResourceAndLeaveCriticalRegion(&gPathsLock);
            ExDeleteResourceLite(&gPathsLock);
        }

        void Add(const WCHAR* path) {
            if (!path || gProtectedPathCount >= MAX_PROTECTED_PATHS) return;

            SIZE_T pathLen = wcslen(path);
            if (pathLen == 0) return;

            // Duplikat prüfen
            ExEnterCriticalRegionAndAcquireResourceShared(&gPathsLock);
            for (ULONG i = 0; i < gProtectedPathCount; i++) {
                if (gProtectedPaths[i].Buffer && _wcsicmp((PWSTR)gProtectedPaths[i].Buffer, path) == 0) {
                    ExReleaseResourceAndLeaveCriticalRegion(&gPathsLock);
                    return;
                }
            }
            ExReleaseResourceAndLeaveCriticalRegion(&gPathsLock);

            PVOID buffer = ExAllocatePoolZero(NonPagedPool, (pathLen + 1) * sizeof(WCHAR), POOL_TAG);
            if (!buffer) return;
            RtlCopyMemory(buffer, path, pathLen * sizeof(WCHAR));
            ((PWCHAR)buffer)[pathLen] = L'\0';

            ExEnterCriticalRegionAndAcquireResourceExclusive(&gPathsLock);
            gProtectedPaths[gProtectedPathCount].Buffer = buffer;
            RtlInitUnicodeString(&gProtectedPaths[gProtectedPathCount].Path, (PCWSTR)buffer);
            gProtectedPathCount++;
            ExReleaseResourceAndLeaveCriticalRegion(&gPathsLock);
        }

        BOOLEAN IsProtected(PUNICODE_STRING filePath) {
            if (!filePath || !filePath->Buffer || filePath->Length == 0) return FALSE;

            BOOLEAN result = FALSE;
            ExEnterCriticalRegionAndAcquireResourceShared(&gPathsLock);

            for (ULONG i = 0; i < gProtectedPathCount; i++) {
                if (gProtectedPaths[i].Path.Length == 0) continue;

                // Prefix-Match mit Case-Insensitive
                if (RtlPrefixUnicodeString(&gProtectedPaths[i].Path, filePath, TRUE)) {
                    result = TRUE;
                    break;
                }
            }

            ExReleaseResourceAndLeaveCriticalRegion(&gPathsLock);
            return result;
        }

        void DiscoverDefaultPaths() {
            // Browser
            Add(L"\\Google\\Chrome\\User Data");
            Add(L"\\Microsoft\\Edge\\User Data");
            Add(L"\\BraveSoftware\\Brave-Browser\\User Data");
            Add(L"\\Opera Software\\Opera Stable");
            Add(L"\\Vivaldi\\User Data");
            Add(L"\\Yandex\\YandexBrowser\\User Data");
            Add(L"\\Mozilla\\Firefox\\Profiles");
            Add(L"\\AppData\\Roaming\\zen\\Profiles");

            // Social
            Add(L"\\AppData\\Roaming\\Discord");
            Add(L"\\AppData\\Roaming\\Discordptb");
            Add(L"\\AppData\\Roaming\\Discordcanary");
            Add(L"\\AppData\\Local\\Discord");
            Add(L"\\AppData\\Local\\Discordptb");
            Add(L"\\AppData\\Local\\Discordcanary");
            Add(L"\\AppData\\Roaming\\Telegram Desktop");
            Add(L"\\AppData\\Roaming\\Signal");

            // Wallets
            Add(L"\\AppData\\Local\\Exodus");
            Add(L"\\AppData\\Roaming\\Armory");
            Add(L"\\AppData\\Roaming\\Atomic\\Local Storage\\leveldb");
            Add(L"\\AppData\\Roaming\\Bitcoin\\wallets");
            Add(L"\\AppData\\Roaming\\bytecoin");
            Add(L"\\AppData\\Local\\Coinomi\\Coinomi\\wallets");
            Add(L"\\AppData\\Roaming\\DashCore\\wallets");
            Add(L"\\AppData\\Roaming\\Electrum\\wallets");
            Add(L"\\AppData\\Roaming\\Ethereum\\keystore");
            Add(L"\\AppData\\Roaming\\Guarda\\Local Storage\\leveldb");
            Add(L"\\AppData\\Roaming\\com.liberty.jaxx\\IndexedDB\\file__0.indexeddb.leveldb");
            Add(L"\\AppData\\Roaming\\Litecoin\\wallets");
            Add(L"\\AppData\\Roaming\\MyMonero");
            Add(L"\\AppData\\Roaming\\Monero");
            Add(L"\\AppData\\Roaming\\Zcash");

            // VPN
            Add(L"\\AppData\\Local\\Mullvad VPN\\Local Storage\\leveldb");

            // Misc
            Add(L"C:\\Windows\\System32\\drivers\\etc");

            // Games
            Add(L"\\AppData\\Roaming\\Battle.net");
        }

    } // namespace Paths
} // namespace NoMoreStealer