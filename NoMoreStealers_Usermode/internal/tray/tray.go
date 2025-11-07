//go:build windows

package tray

import (
	"syscall"
	"sync"
	"unsafe"

	"golang.org/x/sys/windows"
)

var (
	shell32              = windows.NewLazySystemDLL("shell32.dll")
	user32               = windows.NewLazySystemDLL("user32.dll")
	procShellNotifyIconW = shell32.NewProc("Shell_NotifyIconW")
	procLoadIconW        = user32.NewProc("LoadIconW")
	procDefWindowProcW   = user32.NewProc("DefWindowProcW")
	procCreateWindowExW  = user32.NewProc("CreateWindowExW")
	procDestroyWindow    = user32.NewProc("DestroyWindow")
	procRegisterClassExW = user32.NewProc("RegisterClassExW")
	procGetMessageW      = user32.NewProc("GetMessageW")
	procTranslateMessage = user32.NewProc("TranslateMessage")
	procDispatchMessageW = user32.NewProc("DispatchMessageW")
	procCreatePopupMenu  = user32.NewProc("CreatePopupMenu")
	procAppendMenuW      = user32.NewProc("AppendMenuW")
	procTrackPopupMenu   = user32.NewProc("TrackPopupMenu")
	procSetForegroundWindow = user32.NewProc("SetForegroundWindow")
	procGetCursorPos     = user32.NewProc("GetCursorPos")
	procDestroyMenu      = user32.NewProc("DestroyMenu")
)

var (
	trayMutex sync.Mutex
	trayHwnd  windows.Handle
	isAdded   bool
)

const (
	NIM_ADD         = 0x00000000
	NIM_MODIFY      = 0x00000001
	NIM_DELETE      = 0x00000002
	NIF_MESSAGE     = 0x00000001
	NIF_ICON        = 0x00000002
	NIF_TIP         = 0x00000004
	WM_USER         = 0x0400
	WM_DESTROY      = 0x0002
	WM_LBUTTONUP    = 0x0202
	WM_RBUTTONUP    = 0x0205
	IDI_APPLICATION = 32512
	CW_USEDEFAULT   = 0x80000000
	TPM_LEFTALIGN   = 0x0000
	TPM_BOTTOMALIGN = 0x0020
	TPM_RETURNCMD   = 0x0100
)

var WM_TRAYICON = uint32(WM_USER + 1)

type notifyIconData struct {
	StructSize       uint32
	HWnd             windows.Handle
	UID              uint32
	UFlags           uint32
	UCallbackMessage uint32
	HIcon            windows.Handle
	Tip              [128]uint16
}

type wndClassEx struct {
	Size       uint32
	Style      uint32
	WndProc    uintptr
	ClassExtra int32
	WndExtra   int32
	Instance   windows.Handle
	Icon       windows.Handle
	Cursor     windows.Handle
	Background windows.Handle
	MenuName   *uint16
	ClassName  *uint16
	IconSm     windows.Handle
}

var hiddenHwnd windows.Handle
var trayAppShow func()
var trayAppExit func()

// CreateTrayIcon creates a tray icon and starts a message loop to handle clicks
func CreateTrayIcon(_ windows.Handle, tooltip string) error {
	trayMutex.Lock()
	defer trayMutex.Unlock()

	if isAdded {
		return nil
	}

	hwnd, err := createHiddenWindow()
	if err != nil {
		return err
	}

	// ✅ Start Windows message loop in background
	go runMessageLoop(hwnd)

	hIcon, _, _ := procLoadIconW.Call(0, IDI_APPLICATION)
	if hIcon == 0 {
		return windows.GetLastError()
	}

	tipUTF16, _ := windows.UTF16FromString(tooltip)
	if len(tipUTF16) > 128 {
		tipUTF16 = tipUTF16[:128]
	}

	nid := notifyIconData{
		StructSize:       uint32(unsafe.Sizeof(notifyIconData{})),
		HWnd:             hwnd,
		UID:              1,
		UFlags:           NIF_ICON | NIF_MESSAGE | NIF_TIP,
		UCallbackMessage: WM_TRAYICON,
		HIcon:            windows.Handle(hIcon),
	}

	copy(nid.Tip[:], tipUTF16)
	ret, _, _ := procShellNotifyIconW.Call(NIM_ADD, uintptr(unsafe.Pointer(&nid)))
	if ret == 0 {
		return windows.GetLastError()
	}

	trayHwnd = hwnd
	isAdded = true
	return nil
}

// RemoveTrayIcon removes the icon from the system tray
func RemoveTrayIcon() error {
	trayMutex.Lock()
	defer trayMutex.Unlock()

	if !isAdded {
		return nil
	}

	nid := notifyIconData{
		StructSize: uint32(unsafe.Sizeof(notifyIconData{})),
		HWnd:       trayHwnd,
		UID:        1,
	}
	procShellNotifyIconW.Call(NIM_DELETE, uintptr(unsafe.Pointer(&nid)))
	isAdded = false
	return nil
}

// SetTrayCallbacks registers a callback for left-click actions
func SetTrayCallbacks(onShow func(), onExit func()) {
	trayAppShow = onShow
	trayAppExit = onExit
}

// createHiddenWindow creates an invisible window that listens for tray icon messages
func createHiddenWindow() (windows.Handle, error) {
	className, _ := windows.UTF16PtrFromString("NoMoreStealersTrayClass")

	wndProc := syscall.NewCallback(func(hwnd windows.Handle, msg uint32, wParam, lParam uintptr) uintptr {
		switch msg {
		case WM_TRAYICON:
			switch lParam {
			case WM_LBUTTONUP:
				onTrayClick()
			case WM_RBUTTONUP:
				onTrayRightClick()
			}
		case WM_DESTROY:
			procDefWindowProcW.Call(uintptr(hwnd), uintptr(msg), wParam, lParam)
		}
		ret, _, _ := procDefWindowProcW.Call(uintptr(hwnd), uintptr(msg), wParam, lParam)
		return ret
	})

	wc := wndClassEx{
		Size:      uint32(unsafe.Sizeof(wndClassEx{})),
		WndProc:   wndProc,
		ClassName: className,
		Instance:  windows.CurrentProcess(),
	}

	ret, _, err := procRegisterClassExW.Call(uintptr(unsafe.Pointer(&wc)))
	if ret == 0 {
		return 0, err
	}

	hwnd, _, err := procCreateWindowExW.Call(
		0,
		uintptr(unsafe.Pointer(className)),
		0,
		0,
		CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
		0, 0, uintptr(windows.CurrentProcess()), 0,
	)
	if hwnd == 0 {
		return 0, err
	}

	hiddenHwnd = windows.Handle(hwnd)
	return hiddenHwnd, nil
}

// runMessageLoop keeps the hidden window responsive to tray icon clicks
func runMessageLoop(hwnd windows.Handle) {
	var msg struct {
		Hwnd    windows.Handle
		Message uint32
		WParam  uintptr
		LParam  uintptr
		Time    uint32
		Pt      struct{ X, Y int32 }
	}

	for {
		ret, _, _ := procGetMessageW.Call(uintptr(unsafe.Pointer(&msg)), 0, 0, 0)
		if int32(ret) <= 0 {
			break
		}
		procTranslateMessage.Call(uintptr(unsafe.Pointer(&msg)))
		procDispatchMessageW.Call(uintptr(unsafe.Pointer(&msg)))
	}
}

// onTrayClick handles left-clicks
func onTrayClick() {
	if trayAppShow != nil {
		trayAppShow()
	} else {
		println("[Tray] Left-click detected (no callback set)")
	}
}

// onTrayRightClick handles right-clicks
func onTrayRightClick() {
	menu, _, err := procCreatePopupMenu.Call()
	if menu == 0 {
		if err != windows.ERROR_SUCCESS {
			println("[Tray] Failed to create popup menu:", err)
		}
		return
	}
	defer procDestroyMenu.Call(menu)

	exitText, _ := windows.UTF16PtrFromString("Exit")
	const exitCmd = 1
	procAppendMenuW.Call(menu, 0, exitCmd, uintptr(unsafe.Pointer(exitText)))

	var pt struct{ X, Y int32 }
	procGetCursorPos.Call(uintptr(unsafe.Pointer(&pt)))
	procSetForegroundWindow.Call(uintptr(hiddenHwnd))

	selection, _, _ := procTrackPopupMenu.Call(
		menu,
		TPM_LEFTALIGN|TPM_BOTTOMALIGN|TPM_RETURNCMD,
		uintptr(pt.X),
		uintptr(pt.Y),
		0,
		uintptr(hiddenHwnd),
		0,
	)

	if selection == exitCmd && trayAppExit != nil {
		trayAppExit()
	}
}
