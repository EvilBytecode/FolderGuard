#include <fltKernel.h>

#include "../Paths/paths.h"
#include "../Process/process.h"
#include "../Process/trusted_processes.h"
#include "../Comm/comm.h"
#include "callbacks.h"

extern "C" {
	NTKERNELAPI PSTR PsGetProcessImageFileName(_In_ PEPROCESS Process);
	NTKERNELAPI NTSTATUS PsLookupProcessByProcessId(
		_In_ HANDLE ProcessId,
		_Out_ PEPROCESS* Process
	);
}

namespace NoMoreStealer {

	namespace Callbacks {

		namespace {
			BOOLEAN IsRawDiskAccess(PFLT_CALLBACK_DATA Data, PFLT_FILE_NAME_INFORMATION nameInfo) {
				if (!nameInfo || !nameInfo->Name.Buffer)
					return FALSE;

				UNICODE_STRING volumePattern;
				RtlInitUnicodeString(&volumePattern, L"\\Device\\HarddiskVolume");

				UNICODE_STRING physicalPattern;
				RtlInitUnicodeString(&physicalPattern, L"\\Device\\Harddisk");

				UNICODE_STRING rawPattern;
				RtlInitUnicodeString(&rawPattern, L"\\\\.\\PhysicalDrive");

			if (RtlPrefixUnicodeString(&volumePattern, &nameInfo->Name, TRUE) ||
				RtlPrefixUnicodeString(&physicalPattern, &nameInfo->Name, TRUE) ||
				RtlPrefixUnicodeString(&rawPattern, &nameInfo->Name, TRUE)) {

				if (nameInfo->Name.Length <= volumePattern.Length + (4 * sizeof(WCHAR))) {
					return TRUE;
				}
			}

			if (nameInfo->Name.Buffer && nameInfo->Name.Length >= sizeof(WCHAR)) {
				ULONG maxLen = nameInfo->Name.Length / sizeof(WCHAR);
				PWCHAR buffer = nameInfo->Name.Buffer;
				
				for (ULONG i = 0; i < maxLen - 1; i++) {
					if (buffer[i] == L'\\' && buffer[i + 1] == L'$') {
						if ((i + 4 < maxLen && buffer[i + 2] == L'M' && buffer[i + 3] == L'F' && buffer[i + 4] == L'T') ||
							(i + 7 < maxLen && buffer[i + 2] == L'B' && buffer[i + 3] == L'i' && buffer[i + 4] == L't' && buffer[i + 5] == L'm' && buffer[i + 6] == L'a' && buffer[i + 7] == L'p') ||
							(i + 8 < maxLen && buffer[i + 2] == L'L' && buffer[i + 3] == L'o' && buffer[i + 4] == L'g' && buffer[i + 5] == L'F' && buffer[i + 6] == L'i' && buffer[i + 7] == L'l' && buffer[i + 8] == L'e') ||
							(i + 5 < maxLen && buffer[i + 2] == L'B' && buffer[i + 3] == L'o' && buffer[i + 4] == L'o' && buffer[i + 5] == L't') ||
							(i + 8 < maxLen && buffer[i + 2] == L'B' && buffer[i + 3] == L'a' && buffer[i + 4] == L'd' && buffer[i + 5] == L'C' && buffer[i + 6] == L'l' && buffer[i + 7] == L'u' && buffer[i + 8] == L's') ||
							(i + 7 < maxLen && buffer[i + 2] == L'S' && buffer[i + 3] == L'e' && buffer[i + 4] == L'c' && buffer[i + 5] == L'u' && buffer[i + 6] == L'r' && buffer[i + 7] == L'e') ||
							(i + 7 < maxLen && buffer[i + 2] == L'U' && buffer[i + 3] == L'p' && buffer[i + 4] == L'C' && buffer[i + 5] == L'a' && buffer[i + 6] == L's' && buffer[i + 7] == L'e') ||
							(i + 7 < maxLen && buffer[i + 2] == L'E' && buffer[i + 3] == L'x' && buffer[i + 4] == L't' && buffer[i + 5] == L'e' && buffer[i + 6] == L'n' && buffer[i + 7] == L'd')) {
							return TRUE;
						}
					}
				}
			}

			if (Data->Iopb->MajorFunction == IRP_MJ_FILE_SYSTEM_CONTROL) {
				ULONG fsctl = Data->Iopb->Parameters.FileSystemControl.Common.FsControlCode;

				if (fsctl == FSCTL_GET_RETRIEVAL_POINTERS ||
					fsctl == FSCTL_GET_VOLUME_BITMAP ||
					fsctl == FSCTL_MOVE_FILE ||
					fsctl == FSCTL_SET_SPARSE) {
					return TRUE;
				}
			}

			return FALSE;
		}

		BOOLEAN IsSuspiciousAccess(PFLT_CALLBACK_DATA Data) {
			if (Data->Iopb->MajorFunction == IRP_MJ_CREATE) {
				ULONG createOptions = Data->Iopb->Parameters.Create.Options;
				if (!Data->Iopb->Parameters.Create.SecurityContext) {
					return FALSE;
				}
				ACCESS_MASK desiredAccess = Data->Iopb->Parameters.Create.SecurityContext->DesiredAccess;

					if (FlagOn(createOptions, FILE_OPEN_BY_FILE_ID))
						return TRUE;
					if (FlagOn(createOptions, FILE_DELETE_ON_CLOSE))
						return TRUE;

					if (FlagOn(desiredAccess, DELETE) && FlagOn(desiredAccess, GENERIC_WRITE))
						return TRUE;
					if (FlagOn(desiredAccess, FILE_WRITE_DATA) && FlagOn(desiredAccess, FILE_WRITE_ATTRIBUTES) &&
						FlagOn(desiredAccess, FILE_WRITE_EA))
						return TRUE;
				}

				return FALSE;
			}

			BOOLEAN IsAlternateDataStream(PFLT_FILE_NAME_INFORMATION nameInfo) {
				if (!nameInfo || !nameInfo->Name.Buffer || nameInfo->Name.Length == 0)
					return FALSE;

				PWCHAR nameBuffer = nameInfo->Name.Buffer;
				USHORT nameLength = nameInfo->Name.Length / sizeof(WCHAR);

				for (USHORT i = 2; i < nameLength; i++) {
					if (nameBuffer[i] == L':' && i < nameLength - 1) {
						return TRUE;
					}
				}

				return FALSE;
			}

			BOOLEAN IsDestructiveOperation(PFLT_CALLBACK_DATA Data) {
				switch (Data->Iopb->MajorFunction) {
				case IRP_MJ_WRITE:
					return TRUE;
				case IRP_MJ_SET_INFORMATION:
				{
					FILE_INFORMATION_CLASS infoClass = Data->Iopb->Parameters.SetFileInformation.FileInformationClass;
					return (infoClass == FileDispositionInformation ||
						infoClass == FileDispositionInformationEx ||
						infoClass == FileRenameInformation ||
						infoClass == FileRenameInformationEx ||
						infoClass == FileLinkInformation ||
						infoClass == FileEndOfFileInformation ||
						infoClass == FileAllocationInformation);
				}
				case IRP_MJ_CREATE:
				{
					ULONG disposition = (Data->Iopb->Parameters.Create.Options >> 24) & 0xFF;
					return (disposition == FILE_SUPERSEDE ||
						disposition == FILE_OVERWRITE ||
						disposition == FILE_OVERWRITE_IF);
				}
				default:
					return FALSE;
				}
			}

			const CHAR* GetOperationDescription(PFLT_CALLBACK_DATA Data) {
				switch (Data->Iopb->MajorFunction) {
				case IRP_MJ_CREATE:
				{
					ULONG disposition = (Data->Iopb->Parameters.Create.Options >> 24) & 0xFF;
					switch (disposition) {
					case FILE_SUPERSEDE: return "SUPERSEDE";
					case FILE_CREATE: return "CREATE";
					case FILE_OPEN: return "OPEN";
					case FILE_OPEN_IF: return "OPEN_IF";
					case FILE_OVERWRITE: return "OVERWRITE";
					case FILE_OVERWRITE_IF: return "OVERWRITE_IF";
					default: return "CREATE (unknown)";
					}
				}
				case IRP_MJ_WRITE: return "WRITE";
				case IRP_MJ_SET_INFORMATION:
				{
					FILE_INFORMATION_CLASS infoClass = Data->Iopb->Parameters.SetFileInformation.FileInformationClass;
					switch (infoClass) {
					case FileDispositionInformation: return "DELETE";
					case FileDispositionInformationEx: return "DELETE_EX";
					case FileRenameInformation: return "RENAME";
					case FileRenameInformationEx: return "RENAME_EX";
					case FileLinkInformation: return "HARDLINK";
					case FileEndOfFileInformation: return "TRUNCATE";
					default: return "SET_INFO";
					}
				}
				case IRP_MJ_READ: return "READ";
				case IRP_MJ_CLEANUP: return "CLEANUP";
				case IRP_MJ_CLOSE: return "CLOSE";
				default: return "UNKNOWN";
				}
			}
		}

		FLT_PREOP_CALLBACK_STATUS __stdcall PreOperation(
			PFLT_CALLBACK_DATA Data,
			PCFLT_RELATED_OBJECTS FltObjects,
			PVOID* CompletionContext
		) {
			UNREFERENCED_PARAMETER(FltObjects);
			UNREFERENCED_PARAMETER(CompletionContext);
			BOOLEAN isPagingIo = FlagOn(Data->Iopb->IrpFlags, IRP_PAGING_IO);
			BOOLEAN isCreate = (Data->Iopb->MajorFunction == IRP_MJ_CREATE);
			
			if (isPagingIo && !isCreate) {
				return FLT_PREOP_SUCCESS_NO_CALLBACK;
			}

			if (Data->Iopb->MajorFunction != IRP_MJ_CREATE &&
				Data->Iopb->MajorFunction != IRP_MJ_WRITE &&
				Data->Iopb->MajorFunction != IRP_MJ_SET_INFORMATION &&
				Data->Iopb->MajorFunction != IRP_MJ_READ &&
				Data->Iopb->MajorFunction != IRP_MJ_FILE_SYSTEM_CONTROL &&
				Data->Iopb->MajorFunction != IRP_MJ_CLEANUP) {
				return FLT_PREOP_SUCCESS_NO_CALLBACK;
			}
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

			PEPROCESS current = PsGetCurrentProcess();
			HANDLE pidHandle = PsGetCurrentProcessId();
			ULONG pid = HandleToULong(pidHandle);
			
			CHAR procNameBuffer[512] = { 0 };
			CHAR fileNameBuffer[512] = { 0 };
			const CHAR* procName = nullptr;
			
			ULONG nameLength = 0;
			NTSTATUS nameStatus = STATUS_UNSUCCESSFUL;
			
			__try {
				nameStatus = NoMoreStealer::Process::GetProcessImageName(
					current, 
					procNameBuffer, 
					sizeof(procNameBuffer) - 1, 
					&nameLength
				);
			} __except(EXCEPTION_EXECUTE_HANDLER) {
				nameStatus = STATUS_UNSUCCESSFUL;
			}
			
			if (NT_SUCCESS(nameStatus) && nameLength > 0) {
				CHAR* lastSlash = nullptr;
				for (ULONG i = 0; i < nameLength && i < sizeof(procNameBuffer) - 1; i++) {
					if (procNameBuffer[i] == '\\') {
						lastSlash = &procNameBuffer[i];
					}
				}
				if (lastSlash && lastSlash[1] != '\0') {
					ULONG offset = (ULONG)(lastSlash - procNameBuffer);
					if (offset < nameLength && nameLength > offset) {
						ULONG fileNameLen = nameLength - offset - 1;
						if (fileNameLen < sizeof(fileNameBuffer)) {
							for (ULONG i = 0; i < fileNameLen; i++) {
								fileNameBuffer[i] = lastSlash[i + 1];
							}
							fileNameBuffer[fileNameLen] = '\0';
							procName = fileNameBuffer;
						} else {
							procName = procNameBuffer;
						}
					} else {
						procName = procNameBuffer;
					}
				} else {
					procName = procNameBuffer;
				}
				
				if (procName && !_stricmp(procName, "System") && pid != 4) {
					PEPROCESS processByPid = nullptr;
					if (NT_SUCCESS(PsLookupProcessByProcessId((HANDLE)pid, &processByPid))) {
						CHAR pidProcNameBuffer[512] = { 0 };
						ULONG pidNameLength = 0;
						NTSTATUS pidNameStatus = STATUS_UNSUCCESSFUL;
						
						__try {
							pidNameStatus = NoMoreStealer::Process::GetProcessImageName(
								processByPid,
								pidProcNameBuffer,
								sizeof(pidProcNameBuffer) - 1,
								&pidNameLength
							);
						} __except(EXCEPTION_EXECUTE_HANDLER) {
							pidNameStatus = STATUS_UNSUCCESSFUL;
						}
						
						if (NT_SUCCESS(pidNameStatus) && pidNameLength > 0) {
							CHAR* pidLastSlash = nullptr;
							for (ULONG i = 0; i < pidNameLength && i < sizeof(pidProcNameBuffer) - 1; i++) {
								if (pidProcNameBuffer[i] == '\\') {
									pidLastSlash = &pidProcNameBuffer[i];
								}
							}
							const CHAR* pidFileName = pidLastSlash ? (pidLastSlash + 1) : pidProcNameBuffer;
							if (pidFileName && pidFileName[0] != '\0' && _stricmp(pidFileName, "System")) {
								ULONG pidFileNameLen = 0;
								while (pidFileName[pidFileNameLen] != '\0' && pidFileNameLen < sizeof(fileNameBuffer) - 1) {
									fileNameBuffer[pidFileNameLen] = pidFileName[pidFileNameLen];
									pidFileNameLen++;
								}
								fileNameBuffer[pidFileNameLen] = '\0';
								procName = fileNameBuffer;
							}
						} else {
							__try {
								const CHAR* pidImageFileName = PsGetProcessImageFileName(processByPid);
								if (pidImageFileName && pidImageFileName[0] != '\0' && _stricmp(pidImageFileName, "System")) {
									ULONG pidLen = 0;
									while (pidImageFileName[pidLen] != '\0' && pidLen < sizeof(fileNameBuffer) - 1) {
										fileNameBuffer[pidLen] = pidImageFileName[pidLen];
										pidLen++;
									}
									fileNameBuffer[pidLen] = '\0';
									procName = fileNameBuffer;
								}
							} __except(EXCEPTION_EXECUTE_HANDLER) {
							}
						}
						ObDereferenceObject(processByPid);
					}
				}
			} else {
				__try {
					const CHAR* imageFileName = PsGetProcessImageFileName(current);
					if (imageFileName && imageFileName[0] != '\0') {
						ULONG len = 0;
						while (imageFileName[len] != '\0' && len < sizeof(fileNameBuffer) - 1) {
							fileNameBuffer[len] = imageFileName[len];
							len++;
						}
						fileNameBuffer[len] = '\0';
						
						if (!_stricmp(fileNameBuffer, "System") && pid != 4) {
							PEPROCESS processByPid = nullptr;
							if (NT_SUCCESS(PsLookupProcessByProcessId((HANDLE)pid, &processByPid))) {
								__try {
									const CHAR* pidImageFileName = PsGetProcessImageFileName(processByPid);
									if (pidImageFileName && pidImageFileName[0] != '\0' && _stricmp(pidImageFileName, "System")) {
										ULONG pidLen = 0;
										while (pidImageFileName[pidLen] != '\0' && pidLen < sizeof(fileNameBuffer) - 1) {
											fileNameBuffer[pidLen] = pidImageFileName[pidLen];
											pidLen++;
										}
										fileNameBuffer[pidLen] = '\0';
									}
								} __except(EXCEPTION_EXECUTE_HANDLER) {
								}
								ObDereferenceObject(processByPid);
							}
						}
						
						procName = fileNameBuffer;
					} else {
						procName = "unknown";
					}
				} __except(EXCEPTION_EXECUTE_HANDLER) {
					procName = "unknown";
				}
			}

			if (IsRawDiskAccess(Data, nameInfo) && NoMoreStealer::Paths::IsProtected(&nameInfo->Name)) {
				if (!NoMoreStealer::Process::IsAllowed(current)) {
					DbgPrint("[NoMoreStealer] BLOCKED (raw disk access): Proc=%s PID=%Iu Path=%wZ\n",
						procName ? procName : "unknown",
						pid,
						&nameInfo->Name);
					NoMoreStealer::Comm::NotifyBlock(&nameInfo->Name, procName, (ULONG)pid);
					Data->IoStatus.Status = STATUS_ACCESS_DENIED;
					Data->IoStatus.Information = 0;
					FltReleaseFileNameInformation(nameInfo);
					return FLT_PREOP_COMPLETE;
				}
			}

			if (IsAlternateDataStream(nameInfo) && NoMoreStealer::Paths::IsProtected(&nameInfo->Name)) {
				if (!NoMoreStealer::Process::IsAllowed(current)) {
					DbgPrint("[NoMoreStealer] BLOCKED (ADS): Proc=%s PID=%Iu Path=%wZ\n",
						procName ? procName : "unknown",
						pid,
						&nameInfo->Name);
					NoMoreStealer::Comm::NotifyBlock(&nameInfo->Name, procName, (ULONG)pid);
					Data->IoStatus.Status = STATUS_ACCESS_DENIED;
					Data->IoStatus.Information = 0;
					FltReleaseFileNameInformation(nameInfo);
					return FLT_PREOP_COMPLETE;
				}
			}

			if (IsSuspiciousAccess(Data)) {
				if (NoMoreStealer::Paths::IsProtected(&nameInfo->Name)) {
					DbgPrint("[NoMoreStealer] SUSPICIOUS: Proc=%s PID=%Iu Operation=%s Flags=0x%08X Path=%wZ\n",
						procName ? procName : "unknown",
						pid,
						GetOperationDescription(Data),
						Data->Iopb->Parameters.Create.Options,
						&nameInfo->Name);
				}
			}

			if (NoMoreStealer::Paths::IsProtected(&nameInfo->Name)) {
				BOOLEAN isAllowed = NoMoreStealer::Process::IsAllowed(current);
				
				BOOLEAN isMemoryMapped = (isPagingIo && isCreate);
				
				if (Data->Iopb->MajorFunction == IRP_MJ_CREATE || 
					Data->Iopb->MajorFunction == IRP_MJ_READ ||
					Data->Iopb->MajorFunction == IRP_MJ_CLEANUP) {
					ULONG pathLen = nameInfo->Name.Length / sizeof(WCHAR);
					if (pathLen >= 4) {
						PWCHAR pathBuffer = nameInfo->Name.Buffer;
						PWCHAR lastDot = nullptr;
						for (ULONG i = pathLen; i > 0; i--) {
							ULONG idx = i - 1;
							if (idx < pathLen && pathBuffer[idx] == L'.') {
								lastDot = &pathBuffer[idx];
								break;
							}
							if (idx < pathLen && (pathBuffer[idx] == L'\\' || pathBuffer[idx] == L'/')) {
								break;
							}
						}
						if (lastDot && lastDot[1] != L'\0') {
							BOOLEAN isExecutable = FALSE;
							if ((lastDot[1] == L'e' || lastDot[1] == L'E') &&
								(lastDot[2] == L'x' || lastDot[2] == L'X') &&
								(lastDot[3] == L'e' || lastDot[3] == L'E') &&
								(lastDot[4] == L'\0')) {
								isExecutable = TRUE;
							} else if ((lastDot[1] == L'd' || lastDot[1] == L'D') &&
									   (lastDot[2] == L'l' || lastDot[2] == L'L') &&
									   (lastDot[3] == L'l' || lastDot[3] == L'L') &&
									   (lastDot[4] == L'\0')) {
								isExecutable = TRUE;
							}
							
							if (isExecutable) {
								if (Data->Iopb->MajorFunction == IRP_MJ_READ ||
									Data->Iopb->MajorFunction == IRP_MJ_CLEANUP ||
									(Data->Iopb->MajorFunction == IRP_MJ_CREATE &&
									 (!Data->Iopb->Parameters.Create.SecurityContext ||
									  !FlagOn(Data->Iopb->Parameters.Create.SecurityContext->DesiredAccess, FILE_WRITE_DATA | DELETE | FILE_WRITE_ATTRIBUTES)))) {
									FltReleaseFileNameInformation(nameInfo);
									return FLT_PREOP_SUCCESS_NO_CALLBACK;
								}
							}
						}
					}
				}
				
				if (Data->Iopb->MajorFunction == IRP_MJ_CLEANUP && !isAllowed) {
					PEPROCESS processByPid = nullptr;
					if (NT_SUCCESS(PsLookupProcessByProcessId((HANDLE)pid, &processByPid))) {
						CHAR checkProcNameBuffer[512] = { 0 };
						ULONG checkNameLength = 0;
						NTSTATUS checkNameStatus = STATUS_UNSUCCESSFUL;
						
						__try {
							checkNameStatus = NoMoreStealer::Process::GetProcessImageName(
								processByPid,
								checkProcNameBuffer,
								sizeof(checkProcNameBuffer) - 1,
								&checkNameLength
							);
						} __except(EXCEPTION_EXECUTE_HANDLER) {
							checkNameStatus = STATUS_UNSUCCESSFUL;
						}
						
						const CHAR* checkFileName = nullptr;
						if (NT_SUCCESS(checkNameStatus) && checkNameLength > 0) {
							CHAR* lastSlash = nullptr;
							for (ULONG i = 0; i < checkNameLength && i < sizeof(checkProcNameBuffer) - 1; i++) {
								if (checkProcNameBuffer[i] == '\\') {
									lastSlash = &checkProcNameBuffer[i];
								}
							}
							checkFileName = lastSlash ? (lastSlash + 1) : checkProcNameBuffer;
						} else {
							__try {
								const CHAR* checkImageFileName = PsGetProcessImageFileName(processByPid);
								if (checkImageFileName && checkImageFileName[0] != '\0') {
									checkFileName = checkImageFileName;
								}
							} __except(EXCEPTION_EXECUTE_HANDLER) {
							}
						}
						
						if (checkFileName && checkFileName[0] != '\0') {
							for (ULONG i = 0; i < Process::Trusted::TRUSTED_PROCESSES_COUNT; i++) {
								if (!_stricmp(checkFileName, Process::Trusted::TRUSTED_PROCESSES[i])) {
									isAllowed = TRUE;
									break;
								}
							}
						}
						ObDereferenceObject(processByPid);
					}
				}
				
				if (!isAllowed) {
					BOOLEAN isDestructive = IsDestructiveOperation(Data);
					const CHAR* accessType = isMemoryMapped ? "MEMORY_MAPPED" : 
					                         (isDestructive ? "BLOCKED" : "READ_DENIED");

					if (isMemoryMapped) {
						DbgPrint("[NoMoreStealer] %s: Proc=%s PID=%Iu Operation=%s Path=%wZ (MEMORY_MAPPED_FILE)\n",
							accessType,
							procName ? procName : "unknown",
							pid,
							GetOperationDescription(Data),
							&nameInfo->Name);
					} else {
						DbgPrint("[NoMoreStealer] %s: Proc=%s PID=%Iu Operation=%s Path=%wZ\n",
							accessType,
							procName ? procName : "unknown",
							pid,
							GetOperationDescription(Data),
							&nameInfo->Name);
					}

					NoMoreStealer::Comm::NotifyBlock(&nameInfo->Name, procName, (ULONG)pid);
					Data->IoStatus.Status = STATUS_ACCESS_DENIED;
					Data->IoStatus.Information = 0;
					FltReleaseFileNameInformation(nameInfo);
					return FLT_PREOP_COMPLETE;
				}
				else {
					DbgPrint("[NoMoreStealer] ALLOWED: Proc=%s PID=%Iu Operation=%s Path=%wZ\n",
						procName ? procName : "unknown",
						pid,
						GetOperationDescription(Data),
						&nameInfo->Name);
				}
			}

			FltReleaseFileNameInformation(nameInfo);
			return FLT_PREOP_SUCCESS_NO_CALLBACK;
		}



	}

}