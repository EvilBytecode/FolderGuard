#pragma once

#include <fltKernel.h>

#define NMS_SECTION_NAME L"\\BaseNamedObjects\\NoMoreStealerNotify"

namespace NoMoreStealer {

#pragma pack(push, 1)
    struct NoMoreStealerNotifyData {
        ULONG pid;
        USHORT pathLen;      // bytes
        CHAR procName[64];   // null-terminated
        ULONG ready;         // 0 = empty, 1 = ready
    };
#pragma pack(pop)

    namespace Comm {
        NTSTATUS Init();
        VOID Cleanup();
        VOID NotifyBlock(_In_ PUNICODE_STRING path, _In_opt_ const CHAR* procNameAnsi, _In_ ULONG pid);
    }
}