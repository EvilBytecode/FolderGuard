#pragma once

#include <fltKernel.h>

#define NMS_SECTION_NAME L"\\BaseNamedObjects\\NoMoreStealerNotify"

#pragma pack(push, 1)
struct NoMoreStealerNotifyData {
	ULONG pid;
	ULONG pathLen;
	CHAR procName[64]; 
	// WCHAR path[pathLen/2] follows
	ULONG ready; 
};
#pragma pack(pop)

namespace NoMoreStealer {

	namespace Comm {

		NTSTATUS Init(_In_ PFLT_FILTER Filter, _In_opt_ PDRIVER_OBJECT DriverObject);
		VOID Cleanup();
		VOID NotifyBlock(_In_ PUNICODE_STRING path, _In_opt_ const CHAR* procNameAnsi, _In_ ULONG pid);

	}

}

