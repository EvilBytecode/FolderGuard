#include <fltKernel.h>
#include <ntifs.h>
#include "comm.h"

#define POOL_TAG_COMM 'CNMS'

// Struktur ist jetzt in NoMoreStealer → korrekt einbinden
using NoMoreStealer::NoMoreStealerNotifyData;

namespace NoMoreStealer {
    namespace Comm {

        static HANDLE g_sectionHandle = nullptr;
        static PVOID g_sectionBase = nullptr;
        static NoMoreStealerNotifyData* g_notifyData = nullptr;
        static KSPIN_LOCK g_commLock;
        static volatile LONG g_activeWorkItems = 0;
        static volatile BOOLEAN g_shutdownRequested = FALSE;

        typedef struct _NOTIFY_WORK_ITEM {
            WORK_QUEUE_ITEM WorkItem;
            UNICODE_STRING Path;
            CHAR ProcName[64];
            ULONG Pid;
        } NOTIFY_WORK_ITEM, * PNOTIFY_WORK_ITEM;

        VOID NotifyWorkRoutine(_In_ PVOID Context) {
            PNOTIFY_WORK_ITEM workItem = (PNOTIFY_WORK_ITEM)Context;
            KIRQL oldIrql;

            KeAcquireSpinLock(&g_commLock, &oldIrql);

            if (g_notifyData && !g_shutdownRequested) {
                const ULONG maxPathBytes = PAGE_SIZE - sizeof(NoMoreStealerNotifyData) - sizeof(WCHAR);
                const ULONG pathBytes = min(workItem->Path.Length, maxPathBytes);

                g_notifyData->pid = workItem->Pid;
                g_notifyData->pathLen = (USHORT)pathBytes;

                RtlZeroMemory(g_notifyData->procName, sizeof(g_notifyData->procName));
                size_t copyLen = min(sizeof(g_notifyData->procName) - 1, sizeof(workItem->ProcName) - 1);
                RtlCopyMemory(g_notifyData->procName, workItem->ProcName, copyLen);
                g_notifyData->procName[copyLen] = '\0';

                PWCHAR pathDst = (PWCHAR)((PUCHAR)g_notifyData + sizeof(NoMoreStealerNotifyData));
                RtlZeroMemory(pathDst, maxPathBytes + sizeof(WCHAR));
                RtlCopyMemory(pathDst, workItem->Path.Buffer, pathBytes);
                pathDst[pathBytes / sizeof(WCHAR)] = L'\0';

                InterlockedExchange((volatile LONG*)&g_notifyData->ready, 1);
            }

            KeReleaseSpinLock(&g_commLock, oldIrql);

            if (workItem->Path.Buffer) {
                ExFreePoolWithTag(workItem->Path.Buffer, POOL_TAG_COMM);
            }
            ExFreePoolWithTag(workItem, POOL_TAG_COMM);
            InterlockedDecrement(&g_activeWorkItems);
        }

        NTSTATUS Init() {
            KeInitializeSpinLock(&g_commLock);
            g_activeWorkItems = 0;
            g_shutdownRequested = FALSE;

            UNICODE_STRING sectionName;
            RtlInitUnicodeString(&sectionName, NMS_SECTION_NAME);

            OBJECT_ATTRIBUTES oa;
            InitializeObjectAttributes(&oa, &sectionName, OBJ_CASE_INSENSITIVE, nullptr, nullptr);

            LARGE_INTEGER maxSize;
            maxSize.QuadPart = PAGE_SIZE;  // ← KEIN Designated Initializer!

            NTSTATUS status = ZwCreateSection(&g_sectionHandle,
                SECTION_MAP_READ | SECTION_MAP_WRITE | SECTION_QUERY,
                &oa,
                &maxSize,
                PAGE_READWRITE,
                SEC_COMMIT,
                nullptr);

            if (!NT_SUCCESS(status)) {
                DbgPrint("[NoMoreStealer] Comm: ZwCreateSection failed 0x%08X\n", status);
                return status;
            }

            SIZE_T viewSize = PAGE_SIZE;
            status = ZwMapViewOfSection(g_sectionHandle,
                ZwCurrentProcess(),
                &g_sectionBase,
                0, 0, nullptr, &viewSize,
                ViewUnmap, 0, PAGE_READWRITE);

            if (!NT_SUCCESS(status)) {
                ZwClose(g_sectionHandle);
                g_sectionHandle = nullptr;
                DbgPrint("[NoMoreStealer] Comm: ZwMapViewOfSection failed 0x%08X\n", status);
                return status;
            }

            g_notifyData = (NoMoreStealerNotifyData*)g_sectionBase;
            RtlZeroMemory(g_notifyData, sizeof(NoMoreStealerNotifyData));
            DbgPrint("[NoMoreStealer] Comm: Shared section at %p\n", g_sectionBase);
            return STATUS_SUCCESS;
        }

        VOID Cleanup() {
            g_shutdownRequested = TRUE;

            LARGE_INTEGER delay;
            delay.QuadPart = -10000000LL;  // 1 sec

            for (int i = 0; i < 10 && g_activeWorkItems > 0; ++i) {
                KeDelayExecutionThread(KernelMode, FALSE, &delay);
            }

            KIRQL oldIrql;
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

            if (g_activeWorkItems > 0) {
                DbgPrint("[NoMoreStealer] Comm: Warning: %ld work items still active\n", g_activeWorkItems);
            }
        }

        VOID NotifyBlock(_In_ PUNICODE_STRING path, _In_opt_ const CHAR* procNameAnsi, _In_ ULONG pid) {
            if (!path || !path->Buffer || path->Length == 0 || g_shutdownRequested)
                return;

            PNOTIFY_WORK_ITEM workItem = (PNOTIFY_WORK_ITEM)ExAllocatePoolWithTag(
                NonPagedPool, sizeof(NOTIFY_WORK_ITEM), POOL_TAG_COMM);
            if (!workItem) return;

            RtlZeroMemory(workItem, sizeof(NOTIFY_WORK_ITEM));
            workItem->Pid = pid;

            if (procNameAnsi) {
                size_t i = 0;
                while (i < sizeof(workItem->ProcName) - 1 && procNameAnsi[i]) {
                    workItem->ProcName[i] = procNameAnsi[i];
                    ++i;
                }
                workItem->ProcName[i] = '\0';
            }
            else {
                workItem->ProcName[0] = '\0';
            }

            const ULONG bufferSize = path->Length + sizeof(WCHAR);
            workItem->Path.Buffer = (PWCHAR)ExAllocatePoolWithTag(NonPagedPool, bufferSize, POOL_TAG_COMM);
            if (!workItem->Path.Buffer) {
                ExFreePoolWithTag(workItem, POOL_TAG_COMM);
                return;
            }

            workItem->Path.Length = path->Length;
            workItem->Path.MaximumLength = (USHORT)bufferSize;
            RtlCopyMemory(workItem->Path.Buffer, path->Buffer, path->Length);
            workItem->Path.Buffer[path->Length / sizeof(WCHAR)] = L'\0';

            InterlockedIncrement(&g_activeWorkItems);
            ExInitializeWorkItem(&workItem->WorkItem, NotifyWorkRoutine, workItem);
            ExQueueWorkItem(&workItem->WorkItem, DelayedWorkQueue);
        }

    } // namespace Comm
} // namespace NoMoreStealer