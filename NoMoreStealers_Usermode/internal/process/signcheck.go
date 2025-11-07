//go:build windows

package process

import (
	"errors"
	"syscall"
	"unsafe"

	"golang.org/x/sys/windows"
)

var (
	modWintrust        = syscall.NewLazyDLL("wintrust.dll")
	procWinVerifyTrust = modWintrust.NewProc("WinVerifyTrust")
)

var wintrustActionGenericVerifyV2 = windows.GUID{
	Data1: 0x00aac56b,
	Data2: 0xcd44,
	Data3: 0x11d0,
	Data4: [8]byte{0x8c, 0xc2, 0x00, 0xc0, 0x4f, 0xc2, 0x94, 0x10},
}

// Structures for WinVerifyTrust
type wintrustFileInfo struct {
	cbStruct      uint32
	pcwszFilePath *uint16
	hFile         windows.Handle
	pgKnownSubject *windows.GUID
}

type wintrustData struct {
	cbStruct            uint32
	pPolicyCallbackData uintptr
	pSIPClientData      uintptr
	dwUIChoice          uint32
	fdWRevocationChecks uint32
	dwUnionChoice       uint32
	pInfoStruct         uintptr
	dwStateAction       uint32
	hWVTStateData       windows.Handle
	pwszURLReference    *uint16
	dwProvFlags         uint32
	dwUIContext         uint32
}

// constants
const (
	WTD_UI_NONE             = 2
	WTD_REVOKE_NONE         = 0
	WTD_CHOICE_FILE         = 1
	WTD_STATEACTION_IGNORE  = 0
	WTD_REVOCATION_CHECK_NONE = 0x00000000
	WTD_SAFER_FLAG          = 0x00000100
)

// IsFileSignedWinVerifyTrust checks a file's signature via WinVerifyTrust.
func IsFileSignedWinVerifyTrust(path string) (bool, error) {
	if path == "" {
		return false, errors.New("empty path")
	}
	pathPtr, err := windows.UTF16PtrFromString(path)
	if err != nil {
		return false, err
	}

	var fileInfo wintrustFileInfo
	fileInfo.cbStruct = uint32(unsafe.Sizeof(fileInfo))
	fileInfo.pcwszFilePath = pathPtr

	var data wintrustData
	data.cbStruct = uint32(unsafe.Sizeof(data))
	data.dwUIChoice = WTD_UI_NONE
	data.fdWRevocationChecks = WTD_REVOKE_NONE
	data.dwUnionChoice = WTD_CHOICE_FILE
	data.pInfoStruct = uintptr(unsafe.Pointer(&fileInfo))
	data.dwStateAction = WTD_STATEACTION_IGNORE
	data.dwProvFlags = WTD_SAFER_FLAG

	r1, _, callErr := procWinVerifyTrust.Call(
		0,
		uintptr(unsafe.Pointer(&wintrustActionGenericVerifyV2)),
		uintptr(unsafe.Pointer(&data)),
	)

	if r1 == 0 {
		return true, nil
	}

	if callErr != nil && callErr != syscall.Errno(0) {
		return false, callErr
	}
	return false, nil
}

// IsFileSigned is a convenience wrapper for backward compatibility.
func IsFileSigned(path string) bool {
	ok, _ := IsFileSignedWinVerifyTrust(path)
	return ok
}
