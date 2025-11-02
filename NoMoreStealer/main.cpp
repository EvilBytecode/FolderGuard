#include <fltKernel.h>
#include <ntstrsafe.h>

#include "internal/Paths/paths.h"
#include "internal/Process/process.h"
#include "internal/Callbacks/callbacks.h"
#include "internal/Comm/comm.h"

using namespace NoMoreStealer;  // ← optional, für kürzeren Code

PFLT_FILTER gFilterHandle = nullptr;

namespace NoMoreStealer {
    namespace Driver {

        NTSTATUS Unload(FLT_FILTER_UNLOAD_FLAGS Flags) {
            UNREFERENCED_PARAMETER(Flags);
            DbgPrint("[NoMoreStealer] Unloading minifilter\n");

            Comm::Cleanup();
            Paths::Cleanup();

            if (gFilterHandle) {
                FltUnregisterFilter(gFilterHandle);
                gFilterHandle = nullptr;
            }
            return STATUS_SUCCESS;
        }

        NTSTATUS Entry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath) {
            UNREFERENCED_PARAMETER(RegistryPath);
            DbgPrint("[NoMoreStealer] Loading minifilter\n");

            Paths::Init();
            Paths::DiscoverDefaultPaths();

            NTSTATUS status = Comm::Init();
            if (!NT_SUCCESS(status)) {
                Paths::Cleanup();
                return status;
            }

            FLT_OPERATION_REGISTRATION CallbacksArray[] = {
                { IRP_MJ_CREATE, 0, Callbacks::PreOperation, nullptr },
                { IRP_MJ_OPERATION_END, 0, nullptr, nullptr }
            };

            const FLT_REGISTRATION FilterRegistration = {
                sizeof(FLT_REGISTRATION),
                FLT_REGISTRATION_VERSION,
                0,
                nullptr,
                CallbacksArray,
                Unload,
                nullptr, nullptr, nullptr, nullptr,
                nullptr, nullptr, nullptr, nullptr
            };

            status = FltRegisterFilter(DriverObject, &FilterRegistration, &gFilterHandle);
            if (!NT_SUCCESS(status)) {
                Comm::Cleanup();
                Paths::Cleanup();
                return status;
            }

            status = FltStartFiltering(gFilterHandle);
            if (!NT_SUCCESS(status)) {
                FltUnregisterFilter(gFilterHandle);
                gFilterHandle = nullptr;
                Comm::Cleanup();
                Paths::Cleanup();
                return status;
            }

            return STATUS_SUCCESS;
        }

    } // namespace Driver
} // namespace NoMoreStealer

extern "C" NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath) {
    return NoMoreStealer::Driver::Entry(DriverObject, RegistryPath);
}