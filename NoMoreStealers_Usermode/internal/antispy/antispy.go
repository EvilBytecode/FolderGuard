package antispy

import (
	"context"
	"fmt"
	"log"
	"os"
	"sync"
	"time"
	"unsafe"

	"golang.org/x/sys/windows"
)

const (
	WS_EX_TOPMOST     = 0x00000008
	WS_EX_TRANSPARENT = 0x00000020
	WS_EX_LAYERED     = 0x00080000
	WS_EX_TOOLWINDOW  = 0x00000080
	WS_POPUP          = 0x80000000

	SM_CXSCREEN = 0
	SM_CYSCREEN = 1

	SW_SHOW = 5
	SW_HIDE = 0

	LWA_COLORKEY = 0x00000001

	WDA_MONITOR = 0x00000001

	PM_REMOVE  = 0x0001
	WM_CLOSE   = 0x0010
	WM_DESTROY = 0x0002

	MESSAGE_LOOP_SLEEP = 10 * time.Millisecond
	CLEANUP_DELAY     = 100 * time.Millisecond
)

var (
	user32   = windows.NewLazySystemDLL("user32.dll")
	kernel32 = windows.NewLazySystemDLL("kernel32.dll")

	procDefWindowProcW             = user32.NewProc("DefWindowProcW")
	procRegisterClassW             = user32.NewProc("RegisterClassW")
	procCreateWindowExW            = user32.NewProc("CreateWindowExW")
	procSetLayeredWindowAttributes = user32.NewProc("SetLayeredWindowAttributes")
	procSetWindowDisplayAffinity   = user32.NewProc("SetWindowDisplayAffinity")
	procShowWindow                 = user32.NewProc("ShowWindow")
	procGetSystemMetrics           = user32.NewProc("GetSystemMetrics")
	procPeekMessageW               = user32.NewProc("PeekMessageW")
	procTranslateMessage           = user32.NewProc("TranslateMessage")
	procDispatchMessageW           = user32.NewProc("DispatchMessageW")
	procGetModuleHandleW           = kernel32.NewProc("GetModuleHandleW")
	procDestroyWindow              = user32.NewProc("DestroyWindow")
	procUnregisterClassW           = user32.NewProc("UnregisterClassW")
	procPostMessageW               = user32.NewProc("PostMessageW")
)

type WNDCLASSW struct {
	Style         uint32
	LpfnWndProc   uintptr
	CbClsExtra    int32
	CbWndExtra    int32
	HInstance     windows.Handle
	HIcon         windows.Handle
	HCursor       windows.Handle
	HbrBackground windows.Handle
	LpszMenuName  *uint16
	LpszClassName *uint16
}

type MSG struct {
	Hwnd    windows.Handle
	Message uint32
	WParam  uintptr
	LParam  uintptr
	Time    uint32
	Pt      struct {
		X int32
		Y int32
	}
}

type ScreenBlocker struct {
	mu              sync.RWMutex
	active          bool
	hwnd            windows.Handle
	className       string
	ctx             context.Context
	cancel          context.CancelFunc
	messageLoopDone sync.WaitGroup
}

func NewScreenBlocker() *ScreenBlocker {
	return &ScreenBlocker{}
}

func (sb *ScreenBlocker) Enable() error {
	sb.mu.Lock()
	defer sb.mu.Unlock()

	if sb.active {
		log.Println("Antispy: Already active")
		return nil
	}

	log.Println("Antispy: Enabling screen block...")

	sb.ctx, sb.cancel = context.WithCancel(context.Background())

	hInstance, _, _ := procGetModuleHandleW.Call(0)
	if hInstance == 0 {
		return fmt.Errorf("failed to get module handle")
	}

	sb.className = fmt.Sprintf("NoMoreStealersOverlay_%d_%d", os.Getpid(), time.Now().UnixNano())
	className, err := windows.UTF16PtrFromString(sb.className)
	if err != nil {
		return fmt.Errorf("failed to convert class name: %w", err)
	}

	wc := WNDCLASSW{
		LpfnWndProc:   procDefWindowProcW.Addr(),
		HInstance:     windows.Handle(hInstance),
		LpszClassName: className,
	}

	ret, _, _ := procRegisterClassW.Call(uintptr(unsafe.Pointer(&wc)))
	if ret == 0 {
		err := windows.GetLastError()
		if err != windows.Errno(1410) { // ERROR_CLASS_ALREADY_EXISTS
			return fmt.Errorf("failed to register window class: %w", err)
		}
		log.Println("Antispy: Window class already exists, continuing...")
	}

	cxScreen, _, _ := procGetSystemMetrics.Call(SM_CXSCREEN)
	cyScreen, _, _ := procGetSystemMetrics.Call(SM_CYSCREEN)
	log.Printf("Antispy: Screen dimensions: %dx%d", cxScreen, cyScreen)

	hwnd, _, _ := procCreateWindowExW.Call(
		WS_EX_TOPMOST|WS_EX_TRANSPARENT|WS_EX_LAYERED|WS_EX_TOOLWINDOW,
		uintptr(unsafe.Pointer(className)),
		0,
		WS_POPUP,
		0, 0, cxScreen, cyScreen,
		0, 0, hInstance, 0,
	)

	if hwnd == 0 {
		err := windows.GetLastError()
		return fmt.Errorf("failed to create window: %w", err)
	}

	sb.hwnd = windows.Handle(hwnd)
	log.Printf("Antispy: Created overlay window")

	procSetLayeredWindowAttributes.Call(uintptr(hwnd), 0, 0, LWA_COLORKEY)
	procSetWindowDisplayAffinity.Call(uintptr(hwnd), WDA_MONITOR)
	procShowWindow.Call(uintptr(hwnd), SW_SHOW)

	sb.messageLoopDone.Add(1)
	go sb.messageLoop()

	sb.active = true
	log.Println("Antispy: Screen block enabled successfully")
	return nil
}

func (sb *ScreenBlocker) Disable() error {
	sb.mu.Lock()
	defer sb.mu.Unlock()

	if !sb.active {
		log.Println("Antispy: Already inactive")
		return nil
	}

	log.Println("Antispy: Disabling screen block...")

	if sb.cancel != nil {
		sb.cancel()
	}

	done := make(chan struct{})
	go func() {
		sb.messageLoopDone.Wait()
		close(done)
	}()

	select {
	case <-done:
		log.Println("Antispy: Message loop finished")
	case <-time.After(5 * time.Second):
		log.Println("Antispy: Warning - message loop timeout")
	}

	if sb.hwnd != 0 {
		log.Println("Antispy: Cleaning up overlay window")
		procShowWindow.Call(uintptr(sb.hwnd), SW_HIDE)
		procPostMessageW.Call(uintptr(sb.hwnd), WM_CLOSE, 0, 0)
		time.Sleep(CLEANUP_DELAY)
		procDestroyWindow.Call(uintptr(sb.hwnd))
		sb.hwnd = 0
	}

	if sb.className != "" {
		className, err := windows.UTF16PtrFromString(sb.className)
		if err == nil {
			hInstance, _, _ := procGetModuleHandleW.Call(0)
			ret, _, _ := procUnregisterClassW.Call(uintptr(unsafe.Pointer(className)), hInstance)
			if ret != 0 {
				log.Println("Antispy: Window class unregistered")
			} else {
				log.Printf("Antispy: Failed to unregister window class: %v", windows.GetLastError())
			}
		}
		sb.className = ""
	}

	sb.active = false
	sb.ctx = nil
	sb.cancel = nil
	log.Println("Antispy: Screen block disabled successfully")
	return nil
}

func (sb *ScreenBlocker) IsActive() bool {
	sb.mu.RLock()
	defer sb.mu.RUnlock()
	return sb.active
}

func (sb *ScreenBlocker) messageLoop() {
	defer sb.messageLoopDone.Done()

	var msg MSG
	for {
		select {
		case <-sb.ctx.Done():
			return
		default:
		}

		ret, _, _ := procPeekMessageW.Call(uintptr(unsafe.Pointer(&msg)), 0, 0, 0, PM_REMOVE)
		if ret != 0 {
			if msg.Message == WM_CLOSE || msg.Message == WM_DESTROY {
				return
			}
			procTranslateMessage.Call(uintptr(unsafe.Pointer(&msg)))
			procDispatchMessageW.Call(uintptr(unsafe.Pointer(&msg)))
		} else {
			time.Sleep(MESSAGE_LOOP_SLEEP)
		}
	}
}

var globalBlocker = NewScreenBlocker()

func EnableScreenBlock() error {
	return globalBlocker.Enable()
}

func DisableScreenBlock() error {
	return globalBlocker.Disable()
}

func IsActive() bool {
	return globalBlocker.IsActive()
}
