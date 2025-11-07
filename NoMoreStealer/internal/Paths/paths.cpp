#include "paths.h"
#include "paths_internal.h"

namespace NoMoreStealer {

	namespace Paths {

		static PROTECTED_PATH gProtectedPaths[MAX_PROTECTED_PATHS] = { 0 };
		static ULONG gProtectedPathCount = 0;
		static ERESOURCE gPathsLock;

		void Init() {
			ExInitializeResourceLite(&gPathsLock);
		}

		void Cleanup() {
			ExEnterCriticalRegionAndAcquireResourceExclusive(&gPathsLock);
			for (ULONG i = 0; i < gProtectedPathCount; i++) {
				if (gProtectedPaths[i].Buffer)
					ExFreePoolWithTag(gProtectedPaths[i].Buffer, POOL_TAG_PATHS);
				gProtectedPaths[i].Buffer = nullptr;
				RtlZeroMemory(&gProtectedPaths[i].Path, sizeof(UNICODE_STRING));
			}
			gProtectedPathCount = 0;
			ExReleaseResourceAndLeaveCriticalRegion(&gPathsLock);
			ExDeleteResourceLite(&gPathsLock);
		}

	void Add(const WCHAR* path) {
		if (!path || gProtectedPathCount >= MAX_PROTECTED_PATHS) return;

		SIZE_T pathLen = 0;
		while (path[pathLen] != L'\0' && pathLen < MAXUSHORT) {
			pathLen++;
		}
		if (pathLen == 0 || pathLen >= MAXUSHORT) return;

		ExEnterCriticalRegionAndAcquireResourceShared(&gPathsLock);
		for (ULONG i = 0; i < gProtectedPathCount; i++) {
			if (gProtectedPaths[i].Buffer && _wcsicmp((PWSTR)gProtectedPaths[i].Buffer, path) == 0) {
				ExReleaseResourceAndLeaveCriticalRegion(&gPathsLock);
				return;
			}
		}
		ExReleaseResourceAndLeaveCriticalRegion(&gPathsLock);

		if ((pathLen + 1) > (MAXUSHORT / sizeof(WCHAR)) || 
		    (pathLen + 1) * sizeof(WCHAR) > (MAXUSHORT * sizeof(WCHAR))) {
			return;
		}

		PVOID buffer = ExAllocatePoolZero(NonPagedPool, (pathLen + 1) * sizeof(WCHAR), POOL_TAG_PATHS);
		if (!buffer) {
			DbgPrint("[NoMoreStealer] Paths: Failed to allocate path buffer (length=%Iu, pool exhausted)\n", pathLen);
			return;
		}
		RtlCopyMemory(buffer, path, pathLen * sizeof(WCHAR));

		ExEnterCriticalRegionAndAcquireResourceExclusive(&gPathsLock);
		
		for (ULONG i = 0; i < gProtectedPathCount; i++) {
			if (gProtectedPaths[i].Buffer && _wcsicmp((PWSTR)gProtectedPaths[i].Buffer, path) == 0) {
				ExReleaseResourceAndLeaveCriticalRegion(&gPathsLock);
				ExFreePoolWithTag(buffer, POOL_TAG_PATHS);
				return;
			}
		}
		
		gProtectedPaths[gProtectedPathCount].Buffer = buffer;
		RtlInitUnicodeString(&gProtectedPaths[gProtectedPathCount].Path, (PCWSTR)buffer);
		gProtectedPathCount++;
		ExReleaseResourceAndLeaveCriticalRegion(&gPathsLock);
	}

		BOOLEAN IsProtected(PUNICODE_STRING filePath) {
			if (!filePath || !filePath->Buffer || filePath->Length == 0) return FALSE;

			if (filePath->Length > MAXUSHORT * sizeof(WCHAR)) {
				return FALSE;
			}

			BOOLEAN result = FALSE;
			ExEnterCriticalRegionAndAcquireResourceShared(&gPathsLock);
			for (ULONG i = 0; i < gProtectedPathCount; i++) {
				if (gProtectedPaths[i].Path.Length == 0) continue;

				if (gProtectedPaths[i].Path.Length > MAXUSHORT * sizeof(WCHAR)) {
					continue;
				}

				UNICODE_STRING upperFile = { 0 };
				if (NT_SUCCESS(RtlUpcaseUnicodeString(&upperFile, filePath, TRUE))) {
					UNICODE_STRING upperProtected = { 0 };
					if (NT_SUCCESS(RtlUpcaseUnicodeString(&upperProtected, &gProtectedPaths[i].Path, TRUE))) {
						ULONG fileLen = upperFile.Length / sizeof(WCHAR);
						ULONG protLen = upperProtected.Length / sizeof(WCHAR);
						
						if (protLen > 0 && fileLen > 0 && protLen <= fileLen && fileLen <= (MAXUSHORT / sizeof(WCHAR))) {
							for (ULONG j = 0; j <= fileLen - protLen; j++) {
								if (RtlCompareMemory(&upperFile.Buffer[j], upperProtected.Buffer, upperProtected.Length) == upperProtected.Length) {
									result = TRUE;
									break;
								}
							}
						}
						RtlFreeUnicodeString(&upperProtected);
					}
					RtlFreeUnicodeString(&upperFile);
				}
				if (result) break;
			}
			ExReleaseResourceAndLeaveCriticalRegion(&gPathsLock);
			return result;
		}

		void DiscoverDefaultPaths() {
			Add(L"\\Google\\Chrome\\User Data");
			Add(L"\\Microsoft\\Edge\\User Data");
			Add(L"\\BraveSoftware\\Brave-Browser\\User Data");
			Add(L"\\Opera Software\\Opera Stable");
			Add(L"\\Vivaldi\\User Data");
			Add(L"\\Yandex\\YandexBrowser\\User Data");
			Add(L"\\Mozilla\\Firefox\\Profiles");
			Add(L"\\AppData\\Roaming\\zen\\Profiles");

			Add(L"\\AppData\\Roaming\\Discord");
			Add(L"\\AppData\\Roaming\\Discordptb");
			Add(L"\\AppData\\Roaming\\Discordcanary");
			Add(L"\\AppData\\Local\\Discord");
			Add(L"\\AppData\\Local\\Discordptb");
			Add(L"\\AppData\\Local\\Discordcanary");
			Add(L"\\AppData\\Roaming\\Telegram Desktop");
			Add(L"\\AppData\\Roaming\\Signal");

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

			Add(L"\\AppData\\Local\\Mullvad VPN\\Local Storage\\leveldb");

			Add(L"C:\\Windows\\System32\\drivers\\etc");

			Add(L"\\AppData\\Roaming\\Battle.net");
			Add(L"\\AppData\\Roaming\\.feather");
			Add(L"\\.lunarclient\\settings\\game");
			Add(L"\\AppData\\Roaming\\.minecraft");
			Add(L"\\AppData\\Roaming\\FileZilla");
		}

	}

}
