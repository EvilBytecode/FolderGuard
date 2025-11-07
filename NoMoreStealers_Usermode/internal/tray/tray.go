package process

import (
	"errors"
	"unsafe"

	"golang.org/x/sys/windows"

	"NoMoreStealers/internal/paths"
)

const (
	SE_DEBUG_NAME              = "SeDebugPrivilege"
	PROCESS_QUERY_INFORMATION  = 0x0400
	PROCESS_QUERY_LIMITED_INFO = 0x1000
	PROCESS_VM_READ            = 0x0010
)

var (
	Psapi                     = windows.NewLazySystemDLL("psapi.dll")
	Advapi32                  = windows.NewLazySystemDLL("advapi32.dll")
	ProcGetModuleFileNameExW  = Psapi.NewProc("GetModuleFileNameExW")
	ProcLookupPrivilegeValueW = Advapi32.NewProc("LookupPrivilegeValueW")
	ProcAdjustTokenPrivileges = Advapi32.NewProc("AdjustTokenPrivileges")
)

// GetFilePathFromPID retrieves the executable file path of a given process ID.
func GetFilePathFromPID(pid uint32) (string, error) {
	token, err := windows.OpenCurrentProcessToken()
	if err == nil {
		defer token.Close()

		privName, _ := windows.UTF16PtrFromString(SE_DEBUG_NAME)
		var luid windows.LUID
		ret, _, _ := ProcLookupPrivilegeValueW.Call(0, uintptr(unsafe.Pointer(privName)), uintptr(unsafe.Pointer(&luid)))
		if ret != 0 {
			type tokenPrivileges struct {
				PrivilegeCount uint32
				Privileges     [1]struct {
					Luid       windows.LUID
					Attributes uint32
				}
			}
			tp := tokenPrivileges{
				PrivilegeCount: 1,
				Privileges: [1]struct {
					Luid       windows.LUID
					Attributes uint32
				}{{Luid: luid, Attributes: windows.SE_PRIVILEGE_ENABLED}},
			}
			ProcAdjustTokenPrivileges.Call(uintptr(token), 0, uintptr(unsafe.Pointer(&tp)), 0, 0, 0)
		}
	}

	hProcess, err := windows.OpenProcess(PROCESS_QUERY_INFORMATION|PROCESS_VM_READ, false, pid)
	if err != nil {
		hProcess, err = windows.OpenProcess(PROCESS_QUERY_LIMITED_INFO, false, pid)
		if err != nil {
			return "", err
		}
	}
	defer windows.CloseHandle(hProcess)

	buf := make([]uint16, windows.MAX_PATH)
	ret, _, err := ProcGetModuleFileNameExW.Call(uintptr(hProcess), 0, uintptr(unsafe.Pointer(&buf[0])), uintptr(len(buf)))
	if ret == 0 {
		if err != nil && err.Error() == "The operation completed successfully." {
			if windows.UTF16ToString(buf) != "" {
				return windows.UTF16ToString(buf), nil
			}
		}
		return "", err
	}
	return windows.UTF16ToString(buf), nil
}

// TerminateByPID attempts to terminate the process identified by the provided PID.
func TerminateByPID(pid uint32) error {
	if pid == 0 {
		return errors.New("invalid process id")
	}

	handle, err := windows.OpenProcess(windows.PROCESS_TERMINATE, false, pid)
	if err != nil {
		return err
	}
	defer windows.CloseHandle(handle)

	return windows.TerminateProcess(handle, 1)
}

// IsFileSigned checks if a file is digitally signed
func IsFileSigned(filePath string) bool {
	actualPath := filePath

	if len(filePath) >= 8 && filePath[:8] == "\\Device\\" {
		if dosPath, ok := paths.DevicePathToDOSPath(filePath); ok {
			actualPath = dosPath
		} else {
			return false
		}
	}

	pathUTF16, err := windows.UTF16PtrFromString(actualPath)
	if err != nil {
		return false
	}

	attrs, err := windows.GetFileAttributes(pathUTF16)
	if err != nil || attrs == windows.INVALID_FILE_ATTRIBUTES {
		return false
	}

	wintrust := windows.NewLazySystemDLL("wintrust.dll")
	procWinVerifyTrust := wintrust.NewProc("WinVerifyTrust")

	type winTrustFileInfo struct {
		StructSize   uint32
		FilePath     *uint16
		HFile        uintptr
		KnownSubject uintptr
	}

	type winTrustData struct {
		StructSize         uint32
		PolicyCallbackData uintptr
		SIPClientData      uintptr
		UIChoice           uint32
		RevocationChecks   uint32
		UnionChoice        uint32
		File               uintptr
		StateAction        uint32
		StateData          uintptr
		URLReference       *uint16
		ProviderFlags      uint32
		UIContext          uint32
		SignatureSettings  uintptr
	}

	type winTrustCatalogInfo struct {
		StructSize             uint32
		CatalogVersion         uint32
		CatalogName            *uint16
		MemberTag              *uint16
		MemberFile             *uint16
		CalculatedFileHash     uintptr
		CalculatedFileHashSize uint32
		CatalogBase            uint32
		HashAlgorithm          uint32
		HashOffset             uint32
		HashSize               uint32
	}

	const (
		WTD_UI_NONE               = 2
		WTD_REVOKE_NONE           = 0
		WTD_CHOICE_FILE           = 1
		WTD_CHOICE_CATALOG        = 3
		WTD_STATEACTION_VERIFY    = 1
		WTD_STATEACTION_CLOSE     = 2
		WTD_REVOCATION_CHECK_NONE = 0x00000010
		ERROR_SUCCESS             = 0
	)

	var actionGUID = [16]byte{0x00, 0xA0, 0x56, 0x0A, 0x44, 0x98, 0xFC, 0x10, 0x10, 0x17, 0x9A, 0x47, 0x0B, 0xDC, 0x9C, 0xE5}

	fileData := winTrustFileInfo{StructSize: uint32(unsafe.Sizeof(winTrustFileInfo{})), FilePath: pathUTF16}

	wtData := winTrustData{
		StructSize:       uint32(unsafe.Sizeof(winTrustData{})),
		UIChoice:         WTD_UI_NONE,
		RevocationChecks: WTD_REVOKE_NONE,
		UnionChoice:      WTD_CHOICE_FILE,
		File:             uintptr(unsafe.Pointer(&fileData)),
		StateAction:      WTD_STATEACTION_VERIFY,
		ProviderFlags:    WTD_REVOCATION_CHECK_NONE,
	}

	ret, _, _ := procWinVerifyTrust.Call(0, uintptr(unsafe.Pointer(&actionGUID[0])), uintptr(unsafe.Pointer(&wtData)))
	fileRet := uint32(ret)

	wtData.StateAction = WTD_STATEACTION_CLOSE
	procWinVerifyTrust.Call(0, uintptr(unsafe.Pointer(&actionGUID[0])), uintptr(unsafe.Pointer(&wtData)))

	if fileRet == ERROR_SUCCESS {
		return true
	}

	catalogInfo := winTrustCatalogInfo{StructSize: uint32(unsafe.Sizeof(winTrustCatalogInfo{})), CatalogVersion: 0, MemberTag: pathUTF16, HashAlgorithm: 0}

	wtData = winTrustData{
		StructSize:       uint32(unsafe.Sizeof(winTrustData{})),
		UIChoice:         WTD_UI_NONE,
		RevocationChecks: WTD_REVOKE_NONE,
		UnionChoice:      WTD_CHOICE_CATALOG,
		File:             uintptr(unsafe.Pointer(&catalogInfo)),
		StateAction:      WTD_STATEACTION_VERIFY,
		ProviderFlags:    WTD_REVOCATION_CHECK_NONE,
	}

	ret, _, _ = procWinVerifyTrust.Call(0, uintptr(unsafe.Pointer(&actionGUID[0])), uintptr(unsafe.Pointer(&wtData)))
	catalogRet := uint32(ret)

	wtData.StateAction = WTD_STATEACTION_CLOSE
	procWinVerifyTrust.Call(0, uintptr(unsafe.Pointer(&actionGUID[0])), uintptr(unsafe.Pointer(&wtData)))

	if catalogRet == ERROR_SUCCESS {
		return true
	}

	return false
}
