package comm

import (
	"fmt"
	"unsafe"

	"golang.org/x/sys/windows"
)

const (
	NmsSectionName = `\BaseNamedObjects\NoMoreStealerNotify`
)

type NoMoreStealersNotifyData struct {
	Pid      uint32
	PathLen  uint32
	ProcName [64]byte
	Ready    uint32
}

var (
	Ntdll                    = windows.NewLazySystemDLL("ntdll.dll")
	ProcNtOpenSection        = Ntdll.NewProc("NtOpenSection")
	ProcNtMapViewOfSection   = Ntdll.NewProc("NtMapViewOfSection")
	ProcNtUnmapViewOfSection = Ntdll.NewProc("NtUnmapViewOfSection")
	ProcNtClose              = Ntdll.NewProc("NtClose")
)

type UnicodeString struct {
	Length        uint16
	MaximumLength uint16
	Buffer        *uint16
}

type ObjectAttributes struct {
	Length                   uint32
	RootDirectory            windows.Handle
	ObjectName               *UnicodeString
	Attributes               uint32
	SecurityDescriptor       uintptr
	SecurityQualityOfService uintptr
}

const (
	OBJ_CASE_INSENSITIVE = 0x00000040
	SECTION_MAP_READ     = 0x0004
	SECTION_MAP_WRITE    = 0x0002
	PAGE_READWRITE       = 0x04
	PAGE_SIZE            = 4096
)

func NtSuccess(status uint32) bool {
	return int32(status) >= 0
}

// Init opens and maps the shared section used for kernel-to-user notifications.
// The section is secured with a DACL that only allows SYSTEM and Administrators access.
// This function requires the calling process to have administrator privileges.
// It returns the section handle, base address of the mapping and a typed pointer
// to the notification structure.
func Init() (windows.Handle, uintptr, *NoMoreStealersNotifyData, error) {
	sectionName, err := windows.UTF16PtrFromString(NmsSectionName)
	if err != nil {
		return 0, 0, nil, fmt.Errorf("failed to convert section name: %v", err)
	}

	var us UnicodeString
	us.Buffer = sectionName
	us.Length = uint16(len(NmsSectionName) * 2)
	us.MaximumLength = us.Length

	var oa ObjectAttributes
	oa.Length = uint32(unsafe.Sizeof(oa))
	oa.ObjectName = &us
	oa.Attributes = OBJ_CASE_INSENSITIVE

	var sectionHandle windows.Handle
	ret, _, _ := ProcNtOpenSection.Call(uintptr(unsafe.Pointer(&sectionHandle)), SECTION_MAP_READ|SECTION_MAP_WRITE, uintptr(unsafe.Pointer(&oa)))
	if !NtSuccess(uint32(ret)) {
		// ACCESS_DENIED (0xC0000022) indicates insufficient privileges
		// The section is secured to require Administrator or SYSTEM access
		if uint32(ret) == 0xC0000022 {
			return 0, 0, nil, fmt.Errorf("NtOpenSection failed: ACCESS_DENIED (0x%08X) - administrator privileges required", ret)
		}
		return 0, 0, nil, fmt.Errorf("NtOpenSection failed: 0x%08X", ret)
	}

	viewSize := uintptr(4096)
	var baseAddr uintptr

	ret, _, _ = ProcNtMapViewOfSection.Call(uintptr(sectionHandle), uintptr(windows.CurrentProcess()), uintptr(unsafe.Pointer(&baseAddr)), 0, 0, 0, uintptr(unsafe.Pointer(&viewSize)), 2, 0, PAGE_READWRITE)
	if !NtSuccess(uint32(ret)) {
		ProcNtClose.Call(uintptr(sectionHandle))
		return 0, 0, nil, fmt.Errorf("NtMapViewOfSection failed: 0x%08X", ret)
	}

	notifyData := (*NoMoreStealersNotifyData)(unsafe.Pointer(baseAddr))
	return sectionHandle, baseAddr, notifyData, nil
}

func Cleanup(section windows.Handle, baseAddr uintptr) {
	if baseAddr != 0 {
		ProcNtUnmapViewOfSection.Call(uintptr(windows.CurrentProcess()), baseAddr)
	}
	if section != 0 {
		ProcNtClose.Call(uintptr(section))
	}
}
