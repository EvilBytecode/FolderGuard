#pragma once

#include <fltKernel.h>

namespace NoMoreStealer {
    namespace Process {

        NTSTATUS GetProcessImageName(
            _In_ PEPROCESS Process,
            _Out_ PCHAR Buffer,
            _In_ ULONG BufferSize,
            _Out_ PULONG ReturnLength
        );

        BOOLEAN IsAllowed(_In_ PEPROCESS Process);
        BOOLEAN IsKnownTrustedProcess(_In_ PEPROCESS Process);
        BOOLEAN IsSystemProcess(_In_ PEPROCESS Process);
        BOOLEAN IsProcessProtected(_In_ PEPROCESS Process);
        BOOLEAN IsLegitimateProcess(_In_ PEPROCESS Process);
        BOOLEAN VerifyProcessIntegrity(_In_ PEPROCESS Process);
        BOOLEAN VerifySystemProcessIntegrity(_In_ PEPROCESS Process);
        BOOLEAN VerifyProcessPath(_In_ PEPROCESS Process, _In_ const CHAR* ExpectedPath);

    }
}

