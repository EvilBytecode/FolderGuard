#include <fltKernel.h>

#include "../Paths/paths.h"
#include "../Process/process.h"
#include "../Comm/comm.h"
#include "callbacks.h"

extern "C" {
    NTKERNELAPI PSTR PsGetProcessImageFileName(_In_ PEPROCESS Process);
}

namespace NoMoreStealer {          // ← Singular!
    namespace Callbacks {

        // ------------------------------------------------------------
        // 1. Hilfsfunktionen (bleiben unverändert)
        // ------------------------------------------------------------
        BOOLEAN IsRawDiskAccess(PFLT_CALLBACK_DATA Data, PFLT_FILE_NAME_INFORMATION nameInfo) {
            if (!nameInfo || !nameInfo->Name.Buffer)
                return FALSE;

            UNICODE_STRING volumePattern;   RtlInitUnicodeString(&volumePattern, L"\\Device\\HarddiskVolume");
            UNICODE_STRING physicalPattern; RtlInitUnicodeString(&physicalPattern, L"\\Device\\Harddisk");
            UNICODE_STRING rawPattern;     RtlInitUnicodeString(&rawPattern, L"\\\\.\\PhysicalDrive");

            if (RtlPrefixUnicodeString(&volumePattern, &nameInfo->Name, TRUE) ||
                RtlPrefixUnicodeString(&physicalPattern, &nameInfo->Name, TRUE) ||
                RtlPrefixUnicodeString(&rawPattern, &nameInfo->Name, TRUE)) {

                if (nameInfo->Name.Length <= volumePattern.Length + (4 * sizeof(WCHAR)))
                    return TRUE;
            }

            if (wcsstr(nameInfo->Name.Buffer, L"\\$MFT") ||
                wcsstr(nameInfo->Name.Buffer, L"\\$Bitmap") ||
                wcsstr(nameInfo->Name.Buffer, L"\\$LogFile") ||
                wcsstr(nameInfo->Name.Buffer, L"\\$Boot") ||
                wcsstr(nameInfo->Name.Buffer, L"\\$BadClus") ||
                wcsstr(nameInfo->Name.Buffer, L"\\$Secure") ||
                wcsstr(nameInfo->Name.Buffer, L"\\$UpCase") ||
                wcsstr(nameInfo->Name.Buffer, L"\\$Extend"))
                return TRUE;

            if (Data->Iopb->MajorFunction == IRP_MJ_FILE_SYSTEM_CONTROL) {
                ULONG fsctl = Data->Iopb->Parameters.FileSystemControl.Common.FsControlCode;
                if (fsctl == FSCTL_GET_RETRIEVAL_POINTERS ||
                    fsctl == FSCTL_GET_VOLUME_BITMAP ||
                    fsctl == FSCTL_MOVE_FILE ||
                    fsctl == FSCTL_SET_SPARSE)
                    return TRUE;
            }
            return FALSE;
        }

        BOOLEAN IsDestructiveOperation(PFLT_CALLBACK_DATA Data) {
            switch (Data->Iopb->MajorFunction) {
            case IRP_MJ_WRITE: return TRUE;
            case IRP_MJ_SET_INFORMATION: {
                FILE_INFORMATION_CLASS infoClass = Data->Iopb->Parameters.SetFileInformation.FileInformationClass;
                return (infoClass == FileDispositionInformation ||
                    infoClass == FileDispositionInformationEx ||
                    infoClass == FileRenameInformation ||
                    infoClass == FileRenameInformationEx ||
                    infoClass == FileLinkInformation ||
                    infoClass == FileEndOfFileInformation ||
                    infoClass == FileAllocationInformation);
            }
            case IRP_MJ_CREATE: {
                ULONG disposition = (Data->Iopb->Parameters.Create.Options >> 24) & 0xFF;
                return (disposition == FILE_SUPERSEDE ||
                    disposition == FILE_OVERWRITE ||
                    disposition == FILE_OVERWRITE_IF);
            }
            default: return FALSE;
            }
        }

        const CHAR* GetOperationDescription(PFLT_CALLBACK_DATA Data) {
            switch (Data->Iopb->MajorFunction) {
            case IRP_MJ_CREATE: {
                ULONG d = (Data->Iopb->Parameters.Create.Options >> 24) & 0xFF;
                switch (d) {
                case FILE_SUPERSEDE:     return "SUPERSEDE";
                case FILE_CREATE:        return "CREATE";
                case FILE_OPEN:          return "OPEN";
                case FILE_OPEN_IF:       return "OPEN_IF";
                case FILE_OVERWRITE:     return "OVERWRITE";
                case FILE_OVERWRITE_IF:  return "OVERWRITE_IF";
                default:                 return "CREATE (unknown)";
                }
            }
            case IRP_MJ_WRITE:               return "WRITE";
            case IRP_MJ_SET_INFORMATION: {
                FILE_INFORMATION_CLASS ic = Data->Iopb->Parameters.SetFileInformation.FileInformationClass;
                switch (ic) {
                case FileDispositionInformation:     return "DELETE";
                case FileDispositionInformationEx:   return "DELETE_EX";
                case FileRenameInformation:          return "RENAME";
                case FileRenameInformationEx:        return "RENAME_EX";
                case FileLinkInformation:            return "HARDLINK";
                case FileEndOfFileInformation:       return "TRUNCATE";
                default:                             return "SET_INFO";
                }
            }
            case IRP_MJ_READ:                return "READ";
            case IRP_MJ_CLEANUP:             return "CLEANUP";
            case IRP_MJ_CLOSE:               return "CLOSE";
            default:                         return "UNKNOWN";
            }
        }

        BOOLEAN IsSuspiciousAccess(PFLT_CALLBACK_DATA Data) {
            if (Data->Iopb->MajorFunction == IRP_MJ_CREATE) {
                ULONG opt = Data->Iopb->Parameters.Create.Options;
                ACCESS_MASK da = Data->Iopb->Parameters.Create.SecurityContext->DesiredAccess;

                if (FlagOn(opt, FILE_OPEN_BY_FILE_ID) ||
                    FlagOn(opt, FILE_DELETE_ON_CLOSE))
                    return TRUE;

                if (FlagOn(da, DELETE) && FlagOn(da, GENERIC_WRITE))
                    return TRUE;
                if (FlagOn(da, FILE_WRITE_DATA) && FlagOn(da, FILE_WRITE_ATTRIBUTES) && FlagOn(da, FILE_WRITE_EA))
                    return TRUE;
            }
            return FALSE;
        }

        // ------------------------------------------------------------
        // 2. Pre-Operation Callback
        // ------------------------------------------------------------
        FLT_PREOP_CALLBACK_STATUS __stdcall PreOperation(
            PFLT_CALLBACK_DATA Data,
            PCFLT_RELATED_OBJECTS FltObjects,
            PVOID* CompletionContext
        ) {
            UNREFERENCED_PARAMETER(FltObjects);
            UNREFERENCED_PARAMETER(CompletionContext);

            if (FlagOn(Data->Iopb->IrpFlags, IRP_PAGING_IO))
                return FLT_PREOP_SUCCESS_NO_CALLBACK;

            if (Data->Iopb->MajorFunction != IRP_MJ_CREATE &&
                Data->Iopb->MajorFunction != IRP_MJ_WRITE &&
                Data->Iopb->MajorFunction != IRP_MJ_SET_INFORMATION &&
                Data->Iopb->MajorFunction != IRP_MJ_READ &&
                Data->Iopb->MajorFunction != IRP_MJ_FILE_SYSTEM_CONTROL &&
                Data->Iopb->MajorFunction != IRP_MJ_CLEANUP)
                return FLT_PREOP_SUCCESS_NO_CALLBACK;

            PFLT_FILE_NAME_INFORMATION nameInfo = nullptr;
            NTSTATUS status = FltGetFileNameInformation(Data,
                FLT_FILE_NAME_NORMALIZED | FLT_FILE_NAME_QUERY_DEFAULT, &nameInfo);

            if (!NT_SUCCESS(status) || !nameInfo) {
                if (nameInfo) FltReleaseFileNameInformation(nameInfo);
                return FLT_PREOP_SUCCESS_NO_CALLBACK;
            }

            status = FltParseFileNameInformation(nameInfo);
            if (!NT_SUCCESS(status)) {
                FltReleaseFileNameInformation(nameInfo);
                return FLT_PREOP_SUCCESS_NO_CALLBACK;
            }

            PEPROCESS curProc = PsGetCurrentProcess();
            const CHAR* procName = PsGetProcessImageFileName(curProc);
            ULONG pid = HandleToULong(PsGetCurrentProcessId());

            // ---------- RAW-DISK ----------
            if (IsRawDiskAccess(Data, nameInfo) && Paths::IsProtected(&nameInfo->Name)) {
                if (!Process::IsAllowed(curProc)) {
                    DbgPrint("[NoMoreStealer] BLOCKED (raw disk): Proc=%s PID=%lu Path=%wZ\n",
                        procName ? procName : "unknown", pid, &nameInfo->Name);
                    Comm::NotifyBlock(&nameInfo->Name, procName, pid);
                    Data->IoStatus.Status = STATUS_ACCESS_DENIED;
                    Data->IoStatus.Information = 0;
                    FltReleaseFileNameInformation(nameInfo);
                    return FLT_PREOP_COMPLETE;
                }
            }

            // ---------- SUSPICIOUS ----------
            if (IsSuspiciousAccess(Data) && Paths::IsProtected(&nameInfo->Name)) {
                DbgPrint("[NoMoreStealer] SUSPICIOUS: Proc=%s PID=%lu Op=%s Flags=0x%08X Path=%wZ\n",
                    procName ? procName : "unknown", pid,
                    GetOperationDescription(Data),
                    Data->Iopb->Parameters.Create.Options,
                    &nameInfo->Name);
            }

            // ---------- PROTECTED PATH ----------
            if (Paths::IsProtected(&nameInfo->Name)) {
                if (!Process::IsAllowed(curProc)) {
                    BOOLEAN destructive = IsDestructiveOperation(Data);
                    DbgPrint("[NoMoreStealer] %s: Proc=%s PID=%lu Op=%s Path=%wZ\n",
                        destructive ? "BLOCKED" : "READ_DENIED",
                        procName ? procName : "unknown", pid,
                        GetOperationDescription(Data),
                        &nameInfo->Name);

                    Comm::NotifyBlock(&nameInfo->Name, procName, pid);
                    Data->IoStatus.Status = STATUS_ACCESS_DENIED;
                    Data->IoStatus.Information = 0;
                    FltReleaseFileNameInformation(nameInfo);
                    return FLT_PREOP_COMPLETE;
                }
                else {
                    DbgPrint("[NoMoreStealer] ALLOWED: Proc=%s PID=%lu Op=%s Path=%wZ\n",
                        procName ? procName : "unknown", pid,
                        GetOperationDescription(Data),
                        &nameInfo->Name);
                }
            }

            FltReleaseFileNameInformation(nameInfo);
            return FLT_PREOP_SUCCESS_NO_CALLBACK;
        }

    } // namespace Callbacks
} // namespace NoMoreStealer