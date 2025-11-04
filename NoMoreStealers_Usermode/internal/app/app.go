package app

import (
	"context"
	"fmt"
	"log"
	"os"
	"path/filepath"
	"strings"
	"sync"
	"sync/atomic"
	"time"
	"unsafe"

	"NoMoreStealers/internal/antispy"
	"NoMoreStealers/internal/comm"
	"NoMoreStealers/internal/logging"
	"NoMoreStealers/internal/process"
	"NoMoreStealers/internal/tray"
	"NoMoreStealers/internal/ws"

	"github.com/wailsapp/wails/v2/pkg/runtime"
	"golang.org/x/sys/windows"
)

type App struct {
	Ctx           context.Context
	Section       windows.Handle
	BaseAddr      uintptr
	NotifyData    *comm.NoMoreStealersNotifyData
	EventChan     chan Event
	LastReady     uint32
	WsServer      *ws.Server
	AntispyActive bool
	AntispyMutex  sync.RWMutex
	TrayHwnd      windows.Handle
	Logger        *logging.Logger
	InternalCtx   context.Context
	Cancel        context.CancelFunc
}

type Event struct {
	Type            string `json:"type"`
	ProcessName     string `json:"processName"`
	PID             uint32 `json:"pid"`
	ExecutablePath  string `json:"executablePath"`
	Path            string `json:"path"`
	IsSigned        bool   `json:"isSigned"`
	Timestamp       string `json:"timestamp"`
}

func New() *App {
	ctx, cancel := context.WithCancel(context.Background())
	a := &App{
		EventChan:   make(chan Event, 100),
		InternalCtx: ctx,
		Cancel:      cancel,
	}

	tmp := os.TempDir()
	dir := filepath.Join(tmp, "NoMoreStealers")
	fpath := filepath.Join(dir, "Events.txt")
	if lg, err := logging.NewLogger(fpath); err == nil {
		a.Logger = lg
	} else {
		log.Printf("Failed to create async logger: %v", err)
	}

	return a
}

func (a *App) OnStartup(ctx context.Context) {
	a.Ctx = ctx
	a.WsServer = ws.NewServer()
	a.WsServer.Start("localhost:34116")

	var err error
	a.Section, a.BaseAddr, a.NotifyData, err = comm.Init()
	if err != nil {
		log.Printf("Failed to initialize kernel communication: %v", err)
		
		isAccessDenied := strings.Contains(err.Error(), "ACCESS_DENIED") || strings.Contains(err.Error(), "administrator privileges")
		var errorMsg string
		if isAccessDenied {
			errorMsg = "Failed to initialize kernel communication: " + err.Error() + 
				"\n\nAdministrator privileges are required to access the secure shared memory section." +
				"\n\nPlease run this application as Administrator."
			log.Println("ERROR: Administrator privileges required - please run as Administrator")
		} else {
			errorMsg = "Failed to initialize kernel communication: " + err.Error() + 
				"\n\nMake sure the NoMoreStealers kernel driver is loaded:\nfltmc load NoMoreStealers"
			log.Println("Make sure the NoMoreStealers kernel driver is loaded (NoMoreStealers): fltmc load NoMoreStealers")
		}
		
		go func() {
			errorEvent := Event{Type: "error", ProcessName: "System", Path: errorMsg, Timestamp: time.Now().Format(time.RFC3339)}
			for i := 0; i < 20; i++ {
				time.Sleep(500 * time.Millisecond)
				a.EventChan <- errorEvent
				if a.WsServer != nil && a.WsServer.ClientCount() > 0 {
					a.WsServer.Broadcast(errorEvent)
					break
				}
				if i >= 2 && a.WsServer != nil {
					a.WsServer.Broadcast(errorEvent)
				}
			}
		}()
		return
	}
	log.Println("Successfully initialized kernel communication")
	go a.monitorLoop()
}

func (a *App) OnDomReady(ctx context.Context) {
	a.Ctx = ctx
	go a.initTrayIcon()
}

func (a *App) OnBeforeClose(ctx context.Context) (prevent bool) {
	runtime.WindowHide(ctx)
	return true
}

func (a *App) Quit() {
	a.DisableAntispy()
	tray.RemoveTrayIcon()
	if a.Cancel != nil {
		a.Cancel()
	}
	if a.WsServer != nil {
		a.WsServer.Shutdown()
	}
	if a.Section != 0 || a.BaseAddr != 0 {
		comm.Cleanup(a.Section, a.BaseAddr)
	}
	if a.Logger != nil {
		_ = a.Logger.Shutdown()
	}
	if a.Ctx != nil {
		runtime.Quit(a.Ctx)
	}
}

func (a *App) Show() {
	if a.Ctx != nil {
		runtime.WindowShow(a.Ctx)
	}
}

func (a *App) GetEvents() []Event {
	events := make([]Event, 0)
	for {
		select {
		case event := <-a.EventChan:
			events = append(events, event)
		default:
			return events
		}
	}
}

func (a *App) monitorLoop() {
	log.Println("Monitor loop started")
	ticker := time.NewTicker(5 * time.Second)
	defer ticker.Stop()
	lastLogTime := time.Now()
	for {
		select {
		case <-a.InternalCtx.Done():
			log.Println("Monitor loop stopped")
			return
		case <-ticker.C:
		default:
			time.Sleep(100 * time.Millisecond)
			if a.NotifyData == nil {
				if time.Since(lastLogTime) > 10*time.Second {
					log.Println("Warning: notifyData is nil - driver may not be loaded")
					lastLogTime = time.Now()
				}
				continue
			}

			currentReady := uint32(0)
			atomicReady := (*uint32)(unsafe.Pointer(&a.NotifyData.Ready))
			currentReady = atomic.LoadUint32(atomicReady)
			if currentReady == 1 && a.LastReady == 0 {
				pathPtr := (*uint16)(unsafe.Pointer(uintptr(unsafe.Pointer(a.NotifyData)) + unsafe.Sizeof(*a.NotifyData)))
				pathChars := a.NotifyData.PathLen / 2
				if a.NotifyData.PathLen%2 != 0 {
					log.Printf("Warning: Invalid path length (not even): %d", a.NotifyData.PathLen)
					a.LastReady = currentReady
					continue
				}
				maxPathBytes := comm.PAGE_SIZE - uintptr(unsafe.Sizeof(comm.NoMoreStealersNotifyData{}))
				maxPathChars := uint32(maxPathBytes / 2)
				if pathChars == 0 || pathChars > maxPathChars {
					if pathChars > maxPathChars {
						log.Printf("Warning: Path length %d exceeds max %d, truncating", pathChars, maxPathChars)
						pathChars = maxPathChars
					} else {
						log.Printf("Warning: Invalid path length: %d", pathChars)
						a.LastReady = currentReady
						continue
					}
				}
				pathLocal := make([]uint16, pathChars+1)
				copy(pathLocal, unsafe.Slice(pathPtr, pathChars))
				pathLocal[pathChars] = 0
				pathStr := windows.UTF16ToString(pathLocal)
				procName := ""
				if a.NotifyData.ProcName[0] != 0 {
					procName = string(a.NotifyData.ProcName[:])
					for i := 0; i < len(procName); i++ {
						if procName[i] == 0 {
							procName = procName[:i]
							break
						}
					}
				}
				if procName == "" {
					procName = "(unknown)"
				}
				executablePath := ""
				if pidPath, err := process.GetFilePathFromPID(uint32(a.NotifyData.Pid)); err == nil {
					executablePath = pidPath
				}
				isSigned := false
				if executablePath != "" {
					isSigned = process.IsFileSigned(executablePath)
				}
				event := Event{
					ProcessName:    procName,
					PID:            a.NotifyData.Pid,
					ExecutablePath: executablePath,
					Path:           pathStr,
					IsSigned:       isSigned,
					Timestamp:      time.Now().Format(time.RFC3339),
				}
				if isSigned {
					event.Type = "allowed"
				} else {
					event.Type = "blocked"
				}
				go a.logEventToFile(event)
				select {
				case a.EventChan <- event:
				default:
				}
				if a.WsServer != nil {
					a.WsServer.Broadcast(event)
				}
				atomicReady := (*uint32)(unsafe.Pointer(&a.NotifyData.Ready))
				atomic.StoreUint32(atomicReady, 0)
				a.LastReady = 0
			} else {
				a.LastReady = currentReady
			}
		}
	}
}

func (a *App) EnableAntispy() error {
	a.AntispyMutex.Lock()
	defer a.AntispyMutex.Unlock()
	if a.AntispyActive {
		return nil
	}
	err := antispy.EnableScreenBlock()
	if err == nil {
		a.AntispyActive = true
		log.Println("Antispy enabled - screen capture blocked")
	}
	return err
}

func (a *App) DisableAntispy() error {
	a.AntispyMutex.Lock()
	defer a.AntispyMutex.Unlock()

	err := antispy.DisableScreenBlock()
	if err != nil {
		log.Printf("DisableScreenBlock returned error: %v", err)
		a.AntispyActive = false
		return err
	}

	a.AntispyActive = false
	log.Println("Antispy disabled")
	return nil
}

func (a *App) IsAntispyActive() bool {
	return antispy.IsActive()
}

func (a *App) initTrayIcon() {
	user32 := windows.NewLazySystemDLL("user32.dll")
	findWindow := user32.NewProc("FindWindowW")
	time.Sleep(500 * time.Millisecond)
	className, _ := windows.UTF16PtrFromString("Chrome_WidgetWin_1")
	hwnd, _, _ := findWindow.Call(uintptr(unsafe.Pointer(className)), 0)
	if hwnd != 0 {
		a.TrayHwnd = windows.Handle(hwnd)
		tray.CreateTrayIcon(a.TrayHwnd, "NoMoreStealers - Click to show")
		go a.handleTrayMessages()
	}
}

func (a *App) logEventToFile(e Event) {
	safePath := e.Path
	safePath = string([]byte(safePath))
	safePath = replaceNewlines(safePath)
	safeExecPath := e.ExecutablePath
	safeExecPath = string([]byte(safeExecPath))
	safeExecPath = replaceNewlines(safeExecPath)
	line := fmt.Sprintf("%s	%s	PID:%d	Signed:%t	Process:%s	ExecPath:%s	TargetPath:%s", time.Now().Format(time.RFC3339), e.Type, e.PID, e.IsSigned, e.ProcessName, safeExecPath, safePath)
	if a.Logger != nil {
		a.Logger.Log(line)
		return
	}
	tmp := os.TempDir()
	dir := filepath.Join(tmp, "NoMoreStealers")
	_ = os.MkdirAll(dir, 0o755)
	fpath := filepath.Join(dir, "Events.txt")
	f, err := os.OpenFile(fpath, os.O_CREATE|os.O_WRONLY|os.O_APPEND, 0o644)
	if err != nil {
		log.Printf("Failed to open events file %s: %v", fpath, err)
		return
	}
	defer f.Close()
	if _, err := f.WriteString(line + "\n"); err != nil {
		log.Printf("Failed to write event to file: %v", err)
		return
	}
}

func replaceNewlines(s string) string {
	//replacing crlf and lf with \n
	out := s
	out = strings.ReplaceAll(out, "\r\n", "\\n")
	out = strings.ReplaceAll(out, "\n", "\\n")
	return out
}

func (a *App) handleTrayMessages() {
	user32 := windows.NewLazySystemDLL("user32.dll")
	translateMessage := user32.NewProc("TranslateMessage")
	dispatchMessage := user32.NewProc("DispatchMessageW")
	peekMessage := user32.NewProc("PeekMessageW")
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
	const (
		PM_REMOVE    = 0x0001
		WM_LBUTTONUP = 0x0202
		WM_RBUTTONUP = 0x0205
	)
	msg := MSG{}
	for {
		select {
		case <-a.InternalCtx.Done():
			return
		default:
			peekRet, _, _ := peekMessage.Call(uintptr(unsafe.Pointer(&msg)), uintptr(a.TrayHwnd), 0, 0, PM_REMOVE)
			if peekRet != 0 {
				if msg.Message == uint32(tray.WM_TRAYICON) {
					if msg.LParam == WM_LBUTTONUP {
						a.Show()
					} else if msg.LParam == WM_RBUTTONUP {
						a.Show()
					}
				} else {
					translateMessage.Call(uintptr(unsafe.Pointer(&msg)))
					dispatchMessage.Call(uintptr(unsafe.Pointer(&msg)))
				}
			} else {
				time.Sleep(100 * time.Millisecond)
			}
		}
	}
}
