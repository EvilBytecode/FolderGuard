#include "comm.h"
#include "comm_internal.h"

// Helper fce to manually construct wellknown SIDs
// SYSTEM SID: S-1-5-18 (SECURITY_NT_AUTHORITY, 1, SECURITY_LOCAL_SYSTEM_RID)
// Administrators SID: S-1-5-32-544 (SECURITY_NT_AUTHORITY, 2, SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS)

static VOID InitializeSystemSid(PSID sid) {
    SID_IDENTIFIER_AUTHORITY ntAuthority = SECURITY_NT_AUTHORITY;
    RtlInitializeSid(sid, &ntAuthority, 1);
    *(RtlSubAuthoritySid(sid, 0)) = SECURITY_LOCAL_SYSTEM_RID;
}

static VOID InitializeAdminSid(PSID sid) {
    SID_IDENTIFIER_AUTHORITY ntAuthority = SECURITY_NT_AUTHORITY;
    RtlInitializeSid(sid, &ntAuthority, 2);
    *(RtlSubAuthoritySid(sid, 0)) = SECURITY_BUILTIN_DOMAIN_RID;
    *(RtlSubAuthoritySid(sid, 1)) = DOMAIN_ALIAS_RID_ADMINS;
}

namespace NoMoreStealer {

    namespace Comm {

        static HANDLE g_sectionHandle = nullptr;
        static PVOID g_sectionBase = nullptr;
        static NoMoreStealerNotifyData* g_notifyData = nullptr;
        static KSPIN_LOCK g_commLock;
        static volatile LONG g_activeWorkItems = 0;
        static volatile BOOLEAN g_shutdownRequested = FALSE;
        static PFLT_FILTER g_filterHandle = nullptr;
        static PDEVICE_OBJECT g_workItemDeviceObject = nullptr;

        VOID NotifyWorkRoutine(_In_ PDEVICE_OBJECT DeviceObject, _In_opt_ PVOID Context) {
            UNREFERENCED_PARAMETER(DeviceObject);
            
            PNOTIFY_WORK_ITEM workItem = (PNOTIFY_WORK_ITEM)Context;
            if (!workItem) {
                return;
            }

            KIRQL oldIrql;

            KeAcquireSpinLock(&g_commLock, &oldIrql);

            if (g_notifyData && !g_shutdownRequested) {
                g_notifyData->pid = workItem->Pid;
                ULONG maxPathBytes = PAGE_SIZE - sizeof(NoMoreStealerNotifyData);
                ULONG pathBytes = workItem->Path.Length;
                if (pathBytes > maxPathBytes) {
                    pathBytes = maxPathBytes;
                    pathBytes = (pathBytes / sizeof(WCHAR)) * sizeof(WCHAR);
                }

                PWCHAR pathDst = (PWCHAR)((PUCHAR)g_notifyData + sizeof(NoMoreStealerNotifyData));
                RtlZeroMemory(pathDst, maxPathBytes);
                if (pathBytes > 0 && workItem->Path.Buffer) {
                    RtlCopyMemory(pathDst, workItem->Path.Buffer, pathBytes);
                }

                g_notifyData->pathLen = pathBytes;
                RtlZeroMemory(g_notifyData->procName, sizeof(g_notifyData->procName));
                SIZE_T procNameLen = (sizeof(workItem->ProcName) < (sizeof(g_notifyData->procName) - 1)) 
                    ? sizeof(workItem->ProcName) 
                    : (sizeof(g_notifyData->procName) - 1);
                if (procNameLen > 0) {
                    ULONG copyLen = 0;
                    while (copyLen < procNameLen - 1 && workItem->ProcName[copyLen] != '\0') {
                        g_notifyData->procName[copyLen] = workItem->ProcName[copyLen];
                        copyLen++;
                    }
                    g_notifyData->procName[copyLen] = '\0';
                }

                KeMemoryBarrier();
                *(volatile ULONG*)&g_notifyData->ready = 1;
            }

            KeReleaseSpinLock(&g_commLock, oldIrql);

            InterlockedDecrement(&g_activeWorkItems);
            if (workItem->Path.Buffer) {
                ExFreePoolWithTag(workItem->Path.Buffer, POOL_TAG_COMM);
            }
            if (workItem->WorkItem) {
                IoFreeWorkItem(workItem->WorkItem);
            }
            ExFreePoolWithTag(workItem, POOL_TAG_COMM);
        }

        NTSTATUS Init(_In_ PFLT_FILTER Filter, _In_opt_ PDRIVER_OBJECT DriverObject) {
            if (!Filter) {
                return STATUS_INVALID_PARAMETER;
            }

            g_filterHandle = Filter;
            KeInitializeSpinLock(&g_commLock);
            g_activeWorkItems = 0;
            g_shutdownRequested = FALSE;

            OBJECT_ATTRIBUTES oa;
            UNICODE_STRING sectionName;
            RtlInitUnicodeString(&sectionName, NMS_SECTION_NAME);

            UCHAR systemSidBuffer[SECURITY_MAX_SID_SIZE];
            UCHAR adminSidBuffer[SECURITY_MAX_SID_SIZE];
            PSID systemSid = (PSID)systemSidBuffer;
            PSID adminSid = (PSID)adminSidBuffer;
            PACL dacl = nullptr;
            SECURITY_DESCRIPTOR* sd = nullptr;
            NTSTATUS status;

            InitializeSystemSid(systemSid);
            InitializeAdminSid(adminSid);

            // Calculate DACL size: ACL header + 2 ACEs (SYSTEM + Administrators)
            // Each ACE = ACE_HEADER + ACCESS_MASK + SID
            ULONG systemSidLen = RtlLengthSid(systemSid);
            ULONG adminSidLen = RtlLengthSid(adminSid);
            ULONG daclSize = sizeof(ACL) +
                (sizeof(ACE_HEADER) + sizeof(ACCESS_MASK) + systemSidLen) +
                (sizeof(ACE_HEADER) + sizeof(ACCESS_MASK) + adminSidLen);

            dacl = (PACL)ExAllocatePool2(POOL_FLAG_NON_PAGED, daclSize, POOL_TAG_COMM);
            if (!dacl) {
                return STATUS_INSUFFICIENT_RESOURCES;
            }

            status = RtlCreateAcl(dacl, daclSize, ACL_REVISION);
            if (!NT_SUCCESS(status)) {
                ExFreePoolWithTag(dacl, POOL_TAG_COMM);
                return status;
            }

            status = RtlAddAccessAllowedAce(dacl, ACL_REVISION,
                SECTION_MAP_READ | SECTION_MAP_WRITE | SECTION_QUERY, systemSid);
            if (!NT_SUCCESS(status)) {
                ExFreePoolWithTag(dacl, POOL_TAG_COMM);
                return status;
            }

            status = RtlAddAccessAllowedAce(dacl, ACL_REVISION,
                SECTION_MAP_READ | SECTION_MAP_WRITE | SECTION_QUERY, adminSid);
            if (!NT_SUCCESS(status)) {
                ExFreePoolWithTag(dacl, POOL_TAG_COMM);
                return status;
            }

            sd = (SECURITY_DESCRIPTOR*)ExAllocatePool2(POOL_FLAG_NON_PAGED,
                SECURITY_DESCRIPTOR_MIN_LENGTH, POOL_TAG_COMM);
            if (!sd) {
                ExFreePoolWithTag(dacl, POOL_TAG_COMM);
                return STATUS_INSUFFICIENT_RESOURCES;
            }

            status = RtlCreateSecurityDescriptor(sd, SECURITY_DESCRIPTOR_REVISION);
            if (!NT_SUCCESS(status)) {
                ExFreePoolWithTag(sd, POOL_TAG_COMM);
                ExFreePoolWithTag(dacl, POOL_TAG_COMM);
                return status;
            }

            status = RtlSetDaclSecurityDescriptor(sd, TRUE, dacl, FALSE);
            if (!NT_SUCCESS(status)) {
                ExFreePoolWithTag(sd, POOL_TAG_COMM);
                ExFreePoolWithTag(dacl, POOL_TAG_COMM);
                return status;
            }

            InitializeObjectAttributes(&oa, &sectionName, OBJ_CASE_INSENSITIVE, nullptr,
                sd);

            LARGE_INTEGER maxSize;
            maxSize.QuadPart = PAGE_SIZE;

            status = ZwCreateSection(
                &g_sectionHandle, SECTION_MAP_READ | SECTION_MAP_WRITE | SECTION_QUERY,
                &oa, &maxSize, PAGE_READWRITE, SEC_COMMIT, nullptr);

            ExFreePoolWithTag(sd, POOL_TAG_COMM);
            ExFreePoolWithTag(dacl, POOL_TAG_COMM);
            sd = nullptr;
            dacl = nullptr;

            if (!NT_SUCCESS(status)) {
                DbgPrint("[NoMoreStealer] Comm: ZwCreateSection failed 0x%08X\n", status);
                return status;
            }

            SIZE_T viewSize = PAGE_SIZE;
            status =
                ZwMapViewOfSection(g_sectionHandle, ZwCurrentProcess(), &g_sectionBase, 0,
                    0, nullptr, &viewSize, ViewUnmap, 0, PAGE_READWRITE);

            if (!NT_SUCCESS(status)) {
                ZwClose(g_sectionHandle);
                g_sectionHandle = nullptr;
                DbgPrint("[NoMoreStealer] Comm: ZwMapViewOfSection failed 0x%08X\n", status);
                return status;
            }

            g_notifyData = (NoMoreStealerNotifyData*)g_sectionBase;
            RtlZeroMemory(g_notifyData, sizeof(NoMoreStealerNotifyData));
            DbgPrint("[NoMoreStealer] Comm: Section shared at %p\n", g_sectionBase);

            UNICODE_STRING deviceName;
            RtlInitUnicodeString(&deviceName, L"\\Device\\NoMoreStealerWorkItem");
            
            if (!DriverObject) {
                DbgPrint("[NoMoreStealer] Comm: DriverObject is required for work item device\n");
                status = STATUS_INVALID_PARAMETER;
            } else {
                status = IoCreateDevice(
                    DriverObject,
                    0,
                    &deviceName,
                    FILE_DEVICE_UNKNOWN,
                    FILE_DEVICE_SECURE_OPEN,
                    FALSE,
                    &g_workItemDeviceObject);
                
                if (!NT_SUCCESS(status)) {
                    DbgPrint("[NoMoreStealer] Comm: Failed to create work item device 0x%08X\n", status);
                    g_workItemDeviceObject = nullptr;
                } else {
                    g_workItemDeviceObject->Flags |= DO_DIRECT_IO;
                    g_workItemDeviceObject->Flags &= ~DO_DEVICE_INITIALIZING;
                }
            }
            
            return STATUS_SUCCESS;
        }

        VOID Cleanup() {
            KIRQL oldIrql;
            LARGE_INTEGER interval;

            g_shutdownRequested = TRUE;

            interval.QuadPart = -10000000LL;
            ULONG maxWaitIterations = 100;
            ULONG waitIteration = 0;
            while (g_activeWorkItems > 0 && waitIteration < maxWaitIterations) {
                KeDelayExecutionThread(KernelMode, FALSE, &interval);
                waitIteration++;
                if (waitIteration % 10 == 0) {
                    DbgPrint("[NoMoreStealer] Comm: Waiting for %ld work items to complete...\n",
                        g_activeWorkItems);
                }
            }

            KeAcquireSpinLock(&g_commLock, &oldIrql);

            if (g_sectionBase) {
                ZwUnmapViewOfSection(ZwCurrentProcess(), g_sectionBase);
                g_sectionBase = nullptr;
                g_notifyData = nullptr;
            }

            KeReleaseSpinLock(&g_commLock, oldIrql);

            if (g_sectionHandle) {
                ZwClose(g_sectionHandle);
                g_sectionHandle = nullptr;
            }

            if (g_workItemDeviceObject) {
                IoDeleteDevice(g_workItemDeviceObject);
                g_workItemDeviceObject = nullptr;
            }

            if (g_activeWorkItems > 0) {
                DbgPrint("[NoMoreStealer] Comm: Warning - %ld work items still active after "
                    "cleanup\n",
                    g_activeWorkItems);
            }
        }

        VOID NotifyBlock(_In_ PUNICODE_STRING path, _In_opt_ const CHAR* procNameAnsi,
            _In_ ULONG pid) {
            if (!path || !path->Buffer || path->Length == 0)
                return;

            if (g_shutdownRequested)
                return;

            if (path->Length == 0 || path->Length > (MAXUSHORT * sizeof(WCHAR))) {
                return;
            }

            PNOTIFY_WORK_ITEM workItem = (PNOTIFY_WORK_ITEM)ExAllocatePool2(
                POOL_FLAG_NON_PAGED, sizeof(NOTIFY_WORK_ITEM), POOL_TAG_COMM);
            if (!workItem) {
                DbgPrint("[NoMoreStealer] Comm: Failed to allocate work item (pool exhausted)\n");
                return;
            }

            RtlZeroMemory(workItem, sizeof(NOTIFY_WORK_ITEM));
            workItem->Pid = pid;

            if (procNameAnsi) {
                ULONG copyLen = 0;
                while (copyLen < (sizeof(workItem->ProcName) - 1) &&
                    procNameAnsi[copyLen] != '\0') {
                    workItem->ProcName[copyLen] = procNameAnsi[copyLen];
                    copyLen++;
                }
                workItem->ProcName[copyLen] = '\0';
            }
            else {
                workItem->ProcName[0] = '\0';
            }

            workItem->Path.Length = path->Length;
            workItem->Path.MaximumLength = path->Length;
            workItem->Path.Buffer =
                (PWCHAR)ExAllocatePool2(POOL_FLAG_NON_PAGED, path->Length, POOL_TAG_COMM);

            if (!workItem->Path.Buffer) {
                DbgPrint("[NoMoreStealer] Comm: Failed to allocate path buffer (size=%u, pool exhausted)\n", path->Length);
                ExFreePoolWithTag(workItem, POOL_TAG_COMM);
                return;
            }

            RtlCopyMemory(workItem->Path.Buffer, path->Buffer, path->Length);

            if (!g_workItemDeviceObject) {
                ExFreePoolWithTag(workItem->Path.Buffer, POOL_TAG_COMM);
                ExFreePoolWithTag(workItem, POOL_TAG_COMM);
                return;
            }

            workItem->WorkItem = IoAllocateWorkItem(g_workItemDeviceObject);
            if (!workItem->WorkItem) {
                ExFreePoolWithTag(workItem->Path.Buffer, POOL_TAG_COMM);
                ExFreePoolWithTag(workItem, POOL_TAG_COMM);
                return;
            }

            InterlockedIncrement(&g_activeWorkItems);

            IoQueueWorkItem(workItem->WorkItem, NotifyWorkRoutine, 
                DelayedWorkQueue, workItem);
        }

    } // namespace Comm

} // namespace NoMoreStealer
