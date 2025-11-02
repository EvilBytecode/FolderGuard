package tray

import (
	"sync"
	"unsafe"

	"golang.org/x/sys/windows"
)

var (
	Shell32              = windows.NewLazySystemDLL("shell32.dll")
	User32               = windows.NewLazySystemDLL("user32.dll")
	ProcShellNotifyIconW = Shell32.NewProc("Shell_NotifyIconW")
	ProcLoadIconW        = User32.NewProc("LoadIconW")
	ProcFindWindowW      = User32.NewProc("FindWindowW")
	TrayMutex            sync.Mutex
	TrayHwnd             windows.Handle
	IsAdded              bool
)

const (
	NIM_ADD         = 0x00000000
	NIM_MODIFY      = 0x00000001
	NIM_DELETE      = 0x00000002
	NIF_ICON        = 0x00000002
	NIF_MESSAGE     = 0x00000001
	NIF_TIP         = 0x00000004
	WM_USER         = 0x0400
	IDI_APPLICATION = 32512
)

var WM_TRAYICON = uint32(WM_USER + 1)

type NotifyIconData struct {
	StructSize       uint32
	HWnd             windows.Handle
	UID              uint32
	UFlags           uint32
	UCallbackMessage uint32
	HIcon            windows.Handle
	Tip              [128]uint16
}

func CreateTrayIcon(hwnd windows.Handle, tooltip string) error {
	TrayMutex.Lock()
	defer TrayMutex.Unlock()

	if IsAdded {
		return nil
	}

	TrayHwnd = hwnd

	hIcon, _, _ := ProcLoadIconW.Call(0, IDI_APPLICATION)
	if hIcon == 0 {
		return windows.GetLastError()
	}

	tipUTF16, _ := windows.UTF16FromString(tooltip)
	if len(tipUTF16) > 128 {
		tipUTF16 = tipUTF16[:128]
	}

	nid := NotifyIconData{
		StructSize:       uint32(unsafe.Sizeof(NotifyIconData{})),
		HWnd:             hwnd,
		UID:              1,
		UFlags:           NIF_ICON | NIF_MESSAGE | NIF_TIP,
		UCallbackMessage: WM_TRAYICON,
		HIcon:            windows.Handle(hIcon),
	}

	copy(nid.Tip[:], tipUTF16)
	ret, _, _ := ProcShellNotifyIconW.Call(NIM_ADD, uintptr(unsafe.Pointer(&nid)))
	if ret == 0 {
		return windows.GetLastError()
	}

	IsAdded = true
	return nil
}

func RemoveTrayIcon() error {
	TrayMutex.Lock()
	defer TrayMutex.Unlock()

	if !IsAdded {
		return nil
	}

	nid := NotifyIconData{StructSize: uint32(unsafe.Sizeof(NotifyIconData{})), HWnd: TrayHwnd, UID: 1}
	ret, _, _ := ProcShellNotifyIconW.Call(NIM_DELETE, uintptr(unsafe.Pointer(&nid)))
	if ret == 0 {
		return windows.GetLastError()
	}

	IsAdded = false
	return nil
}

func UpdateTrayIcon(hwnd windows.Handle, tooltip string) error {
	TrayMutex.Lock()
	defer TrayMutex.Unlock()

	if !IsAdded {
		return nil
	}

	hIcon, _, _ := ProcLoadIconW.Call(0, IDI_APPLICATION)
	tipUTF16, _ := windows.UTF16FromString(tooltip)
	if len(tipUTF16) > 128 {
		tipUTF16 = tipUTF16[:128]
	}

	nid := NotifyIconData{
		StructSize:       uint32(unsafe.Sizeof(NotifyIconData{})),
		HWnd:             hwnd,
		UID:              1,
		UFlags:           NIF_ICON | NIF_TIP,
		UCallbackMessage: WM_TRAYICON,
		HIcon:            windows.Handle(hIcon),
	}

	copy(nid.Tip[:], tipUTF16)
	ret, _, _ := ProcShellNotifyIconW.Call(NIM_MODIFY, uintptr(unsafe.Pointer(&nid)))
	if ret == 0 {
		return windows.GetLastError()
	}
	return nil
}
