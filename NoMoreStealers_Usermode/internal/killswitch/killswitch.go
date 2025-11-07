package killswitch

import (
	"context"
	"fmt"
	"log"
	"os"
	"path/filepath"
	"strings"
	"sync"
	"time"

	"NoMoreStealers/internal/logging"
	"NoMoreStealers/internal/process"
	"NoMoreStealers/internal/tcp"
	"golang.org/x/sys/windows"
)

const (
	PROCESS_TERMINATE        = 0x0001
	PROCESS_QUERY_INFORMATION = 0x0400
)

var (
	// System process names that should never be terminated
	systemProcessNames = map[string]bool{
		"csrss.exe":                true,
		"dwm.exe":                  true,
		"lsass.exe":                true,
		"lsm.exe":                  true,
		"services.exe":             true,
		"smss.exe":                 true,
		"spoolsv.exe":              true,
		"svchost.exe":              true,
		"wininit.exe":              true,
		"winlogon.exe":             true,
		"system":                   true,
		"System":                   true,
		"explorer.exe":             true,
		"conhost.exe":              true,
		"audiodg.exe":              true,
		"dllhost.exe":              true,
		"taskhost.exe":             true,
		"taskhostw.exe":            true,
		"RuntimeBroker.exe":         true,
		"WmiPrvSE.exe":             true,
		"sihost.exe":               true,
		"SearchIndexer.exe":        true,
		"SearchProtocolHost.exe":    true,
		"SearchFilterHost.exe":     true,
		"MsMpEng.exe":              true, // Windows Defender
		"SecurityHealthService.exe": true,
		"SgrmBroker.exe":           true,
		"SecurityHealthSystray.exe": true,
		"WUDFHost.exe":             true,
		"dashost.exe":              true,
		"DasHost.exe":              true,
		"ApplicationFrameHost.exe": true,
		"fontdrvhost.exe":          true,
		"SystemSettings.exe":       true,
		"SystemSettingsBroker.exe": true,
		"ctfmon.exe":               true,
		"CompPkgSrv.exe":           true,
		"WpnService.exe":           true,
		"WpnUserService.exe":       true,
	}
	
	// System directories that should never be terminated (case-insensitive check)
	systemDirs = []string{
		"c:\\windows\\system32",
		"c:\\windows\\syswow64",
		"c:\\windows\\system",
		"c:\\windows\\",
		"c:\\program files\\windows defender",
		"c:\\program files (x86)\\windows defender",
		"c:\\program files\\windows nt",
		"c:\\program files\\common files\\microsoft shared",
		"c:\\programdata\\microsoft\\windows defender",
	}
)

type KillswitchEvent struct {
	Type            string
	ProcessName     string
	PID             uint32
	ExecutablePath  string
	Path            string // TCP connection info
	IsSigned        bool
	Timestamp       string
}

type Killswitch struct {
	active         bool
	mu             sync.RWMutex
	ctx            context.Context
	cancel         context.CancelFunc
	terminated     map[uint32]bool
	termMu         sync.Mutex
	logger         *logging.Logger
	eventChan      chan KillswitchEvent
	selfPID        uint32
	selfExecutable string
}

func New() *Killswitch {
	ks := &Killswitch{
		terminated: make(map[uint32]bool),
		eventChan:  nil, // Will be set by SetEventChan
	}
	
	// Initialize logger
	tmp := os.TempDir()
	dir := filepath.Join(tmp, "NoMoreStealers")
	fpath := filepath.Join(dir, "Killswitch.log")
	if lg, err := logging.NewLogger(fpath); err == nil {
		ks.logger = lg
	} else {
		log.Printf("Failed to create killswitch logger: %v", err)
	}
	
	return ks
}

func (k *Killswitch) SetEventChan(eventChan chan KillswitchEvent) {
	k.mu.Lock()
	defer k.mu.Unlock()
	k.eventChan = eventChan
}

func (k *Killswitch) GetEventChan() chan KillswitchEvent {
	k.mu.RLock()
	defer k.mu.RUnlock()
	return k.eventChan
}

func (k *Killswitch) Enable() error {
	k.mu.Lock()
	defer k.mu.Unlock()
	
	if k.active {
		return nil
	}
	
	// Get our own process ID and executable path for self-protection
	k.selfPID = uint32(os.Getpid())
	if selfPath, err := os.Executable(); err == nil {
		// Resolve to absolute path
		if absPath, err := filepath.Abs(selfPath); err == nil {
			k.selfExecutable = strings.ToLower(absPath)
		} else {
			k.selfExecutable = strings.ToLower(selfPath)
		}
	} else {
		log.Printf("Warning: Could not get self executable path: %v", err)
	}
	
	k.ctx, k.cancel = context.WithCancel(context.Background())
	k.active = true
	k.terminated = make(map[uint32]bool) // Reset terminated list
	
	k.log(fmt.Sprintf("Killswitch enabled - monitoring processes for unsigned TCP connections (Self PID: %d, Self Path: %s)", k.selfPID, k.selfExecutable))
	go k.monitorLoop()
	
	return nil
}

func (k *Killswitch) Disable() error {
	k.mu.Lock()
	defer k.mu.Unlock()
	
	if !k.active {
		return nil
	}
	
	if k.cancel != nil {
		k.cancel()
	}
	
	k.active = false
	k.log("Killswitch disabled")
	
	return nil
}

func (k *Killswitch) Shutdown() {
	if k.logger != nil {
		_ = k.logger.Shutdown()
	}
}

func (k *Killswitch) IsActive() bool {
	k.mu.RLock()
	defer k.mu.RUnlock()
	return k.active
}

func (k *Killswitch) monitorLoop() {
	ticker := time.NewTicker(2 * time.Second)
	defer ticker.Stop()
	
	for {
		select {
		case <-k.ctx.Done():
			return
		case <-ticker.C:
			k.checkAndTerminate()
		}
	}
}

func (k *Killswitch) checkAndTerminate() {
	// Get detailed TCP connection information
	tcpConnections, err := tcp.GetTCPConnectionsDetailed()
	if err != nil {
		log.Printf("Failed to get TCP connections: %v", err)
		return
	}
	
	// Check each process
	for pid, connInfos := range tcpConnections {
		// Skip if we've already terminated this process
		k.termMu.Lock()
		if k.terminated[pid] {
			k.termMu.Unlock()
			continue
		}
		k.termMu.Unlock()
		
		// Skip our own process - NEVER terminate ourselves
		if pid == k.selfPID {
			continue
		}
		
		// Get process executable path
		exePath, err := process.GetFilePathFromPID(pid)
		if err != nil {
			// Can't access process, skip it
			continue
		}
		
		// Check if this is our own executable - NEVER terminate ourselves
		exePathLower := strings.ToLower(exePath)
		if k.selfExecutable != "" && exePathLower == k.selfExecutable {
			continue
		}
		
		// Check if this is a system process - NEVER terminate system processes
		if k.isSystemProcess(exePath) {
			// Log that we're skipping a system process (for debugging, but don't spam)
			// Only log once per unique path to avoid log spam
			continue
		}
		
		// Get process name
		processName := filepath.Base(exePath)
		
		// Check if process is signed
		isSigned := process.IsFileSigned(exePath)
		
		// If unsigned and has TCP connection, terminate it
		if !isSigned {
			// Format TCP connection details for logging
			connDetails := make([]string, 0, len(connInfos))
			for _, conn := range connInfos {
				connDetails = append(connDetails, fmt.Sprintf("%s:%d -> %s:%d", conn.LocalAddr, conn.LocalPort, conn.RemoteAddr, conn.RemotePort))
			}
			connStr := strings.Join(connDetails, ", ")
			
			if err := k.terminateProcess(pid, exePath, processName, connStr); err != nil {
				log.Printf("Failed to terminate process %d (%s): %v", pid, exePath, err)
				k.log(fmt.Sprintf("FAILED to terminate PID %d | Process: %s | Path: %s | TCP: %s | Error: %v", pid, processName, exePath, connStr, err))
				// Send failure event
				k.sendEvent(KillswitchEvent{
					Type:           "killswitch_failed",
					ProcessName:     processName,
					PID:            pid,
					ExecutablePath: exePath,
					Path:           fmt.Sprintf("TCP: %s", connStr),
					IsSigned:       false,
					Timestamp:      time.Now().Format(time.RFC3339),
				})
			} else {
				log.Printf("Terminated unsigned process with TCP connection: PID %d (%s)", pid, exePath)
				k.log(fmt.Sprintf("TERMINATED PID %d | Process: %s | Path: %s | TCP Connections: %s", pid, processName, exePath, connStr))
				// Send success event
				k.sendEvent(KillswitchEvent{
					Type:           "killswitch_terminated",
					ProcessName:     processName,
					PID:            pid,
					ExecutablePath: exePath,
					Path:           fmt.Sprintf("TCP: %s", connStr),
					IsSigned:       false,
					Timestamp:      time.Now().Format(time.RFC3339),
				})
				k.termMu.Lock()
				k.terminated[pid] = true
				k.termMu.Unlock()
			}
		}
	}
}

func (k *Killswitch) isSystemProcess(exePath string) bool {
	if exePath == "" {
		return true // Can't verify, assume system process for safety
	}
	
	// Normalize path to lowercase for comparison
	exePathLower := strings.ToLower(exePath)
	
	// Check if process name is in system process list
	processName := strings.ToLower(filepath.Base(exePathLower))
	if systemProcessNames[processName] {
		return true
	}
	
	// Check if process is in system directory
	for _, sysDir := range systemDirs {
		if strings.HasPrefix(exePathLower, sysDir) {
			return true
		}
	}
	
	// Additional safety checks:
	// 1. If path contains Windows directory (any case)
	if strings.Contains(exePathLower, "\\windows\\") || strings.Contains(exePathLower, "\\windows ") {
		return true
	}
	
	// 2. If path is in Program Files (most legitimate apps are signed anyway, but extra safety)
	if strings.Contains(exePathLower, "\\program files\\") || strings.Contains(exePathLower, "\\program files (x86)\\") {
		// For Program Files, we're more lenient - only skip if it's a known Windows component
		if strings.Contains(exePathLower, "\\microsoft\\") || strings.Contains(exePathLower, "\\windows ") {
			return true
		}
	}
	
	// 3. If path is in ProgramData (system data directory)
	if strings.HasPrefix(exePathLower, "c:\\programdata\\") {
		if strings.Contains(exePathLower, "\\microsoft\\") || strings.Contains(exePathLower, "\\windows ") {
			return true
		}
	}
	
	// 4. If it's running from System32 or SysWOW64 (catch any variations)
	if strings.Contains(exePathLower, "\\system32\\") || strings.Contains(exePathLower, "\\syswow64\\") {
		return true
	}
	
	return false
}

func (k *Killswitch) log(message string) {
	if k.logger != nil {
		timestamp := time.Now().Format(time.RFC3339)
		k.logger.Log(fmt.Sprintf("[%s] %s", timestamp, message))
	}
	log.Println(message)
}

func (k *Killswitch) sendEvent(event KillswitchEvent) {
	if k.eventChan != nil {
		select {
		case k.eventChan <- event:
		default:
			// Channel full, skip event
		}
	}
}

func (k *Killswitch) terminateProcess(pid uint32, exePath, processName, tcpConnections string) error {
	hProcess, err := windows.OpenProcess(PROCESS_TERMINATE|PROCESS_QUERY_INFORMATION, false, pid)
	if err != nil {
		// Try with limited info
		hProcess, err = windows.OpenProcess(PROCESS_TERMINATE, false, pid)
		if err != nil {
			return err
		}
	}
	defer windows.CloseHandle(hProcess)
	
	err = windows.TerminateProcess(hProcess, 1)
	if err != nil {
		return err
	}
	
	return nil
}

