package app

import (
	"bytes"
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"log"
	"mime/multipart"
	"net/http"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"sync"
	"sync/atomic"
	"time"
	"unsafe"

	"NoMoreStealers/internal/antispy"
	"NoMoreStealers/internal/comm"
	"NoMoreStealers/internal/killswitch"
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
	Killswitch    *killswitch.Killswitch
	fileSightings map[string]time.Time
	fileSightingsMu sync.RWMutex
	vtAPIKey      string
	vtClient      *http.Client
	vtCache       map[string]*VirusTotalReport
	vtCacheMu     sync.RWMutex
	vtKeyMu       sync.RWMutex
	configPath    string
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

const maxVirusTotalUploadSize int64 = 32 * 1024 * 1024

type Settings struct {
	VirusTotalAPIKey string `json:"virusTotalApiKey"`
}

type appConfig struct {
	Settings
}

func New() *App {
	ctx, cancel := context.WithCancel(context.Background())
	a := &App{
		EventChan:      make(chan Event, 100),
		InternalCtx:    ctx,
		Cancel:         cancel,
		Killswitch:     killswitch.New(),
		fileSightings: make(map[string]time.Time),
	}

	a.vtClient = &http.Client{Timeout: 15 * time.Second}
	a.vtCache = make(map[string]*VirusTotalReport)
	a.configPath = determineConfigPath()

	if initialKey := strings.TrimSpace(os.Getenv("VT_API_KEY")); initialKey != "" {
		a.setVirusTotalAPIKey(initialKey)
	}

	if err := a.loadConfig(); err != nil {
		log.Printf("Failed to load settings: %v", err)
	}

	tmp := os.TempDir()
	dir := filepath.Join(tmp, "NoMoreStealers")
	fpath := filepath.Join(dir, "Events.txt")
	if lg, err := logging.NewLogger(fpath); err == nil {
		a.Logger = lg
	} else {
		log.Printf("Failed to create async logger: %v", err)
	}

	// Set up killswitch event channel (handler will be started in OnStartup after WsServer is ready)
	killswitchEventChan := make(chan killswitch.KillswitchEvent, 50)
	a.Killswitch.SetEventChan(killswitchEventChan)

	return a
}

func (a *App) handleKillswitchEvents(killswitchEventChan chan killswitch.KillswitchEvent) {
	for {
		select {
		case <-a.InternalCtx.Done():
			return
		case ksEvent := <-killswitchEventChan:
			// Convert killswitch event to app event
			event := Event{
				Type:           ksEvent.Type,
				ProcessName:    ksEvent.ProcessName,
				PID:            ksEvent.PID,
				ExecutablePath: ksEvent.ExecutablePath,
				Path:           ksEvent.Path,
				IsSigned:       ksEvent.IsSigned,
				Timestamp:      ksEvent.Timestamp,
			}

			seenAt := time.Now().UTC()
			if parsed, err := time.Parse(time.RFC3339, event.Timestamp); err == nil {
				seenAt = parsed.UTC()
			}
			a.recordFileSighting(event.Path, seenAt)
			a.recordFileSighting(event.ExecutablePath, seenAt)

			log.Printf("Killswitch event received: %s - %s (PID %d)", event.Type, event.ProcessName, event.PID)
			
			// Send to EventChan (non-blocking)
			select {
			case a.EventChan <- event:
				log.Printf("Killswitch event added to EventChan")
			default:
				log.Printf("EventChan full, dropping killswitch event")
			}
			
			// Broadcast via WebSocket
			if a.WsServer != nil {
				log.Printf("Broadcasting killswitch event via WebSocket (clients: %d)", a.WsServer.ClientCount())
				a.WsServer.Broadcast(event)
			} else {
				log.Printf("WsServer is nil, cannot broadcast killswitch event")
			}
			
			// Log to file
			go a.logEventToFile(event)
		}
	}
}

func (a *App) OnStartup(ctx context.Context) {
	a.Ctx = ctx
	a.WsServer = ws.NewServer()
	a.WsServer.Start("localhost:34116")
	
	// Start killswitch event handler after WsServer is initialized
	if a.Killswitch != nil {
		killswitchEventChan := a.Killswitch.GetEventChan()
		if killswitchEventChan != nil {
			go a.handleKillswitchEvents(killswitchEventChan)
		}
	}

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
				select {
				case a.EventChan <- errorEvent:
				default:
				}
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
    runtime.WindowSetMinSize(ctx, 1400, 900)
    runtime.WindowSetSize(ctx, 1600, 1020)
    go a.initTrayIcon()
}

func (a *App) OnBeforeClose(ctx context.Context) (prevent bool) {
	// Ensure tray icon is created if it wasn't already
	if a.TrayHwnd == 0 {
		go a.initTrayIcon()
	}
	runtime.WindowHide(ctx)
	return true
}

func (a *App) Quit() {
	a.DisableAntispy()
	if a.Killswitch != nil {
		a.Killswitch.Disable()
		a.Killswitch.Shutdown()
	}
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

func (a *App) ShowMainWindow() {
	a.Show()
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

func (a *App) ClearAllEvents() {
	// Drain the event channel
	for {
		select {
		case <-a.EventChan:
		default:
			return
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
                now := time.Now().UTC()
                event := Event{
					ProcessName:    procName,
					PID:            a.NotifyData.Pid,
					ExecutablePath: executablePath,
					Path:           pathStr,
					IsSigned:       isSigned,
                    Timestamp:      now.Format(time.RFC3339),
				}
				if isSigned {
					event.Type = "allowed"
				} else {
					event.Type = "blocked"
				}
                a.recordFileSighting(event.Path, now)
                a.recordFileSighting(event.ExecutablePath, now)
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

func (a *App) EnableKillswitch() error {
	if a.Killswitch != nil {
		return a.Killswitch.Enable()
	}
	return nil
}

func (a *App) DisableKillswitch() error {
	if a.Killswitch != nil {
		return a.Killswitch.Disable()
	}
	return nil
}

func (a *App) IsKillswitchActive() bool {
	if a.Killswitch != nil {
		return a.Killswitch.IsActive()
	}
	return false
}

func (a *App) RevealPath(path string) error {
	cleaned := strings.TrimSpace(path)
	if cleaned == "" {
		return fmt.Errorf("path is empty")
	}
	cleaned = filepath.Clean(cleaned)

	info, err := os.Stat(cleaned)
	if err != nil {
		return err
	}

	explorer, err := exec.LookPath("explorer.exe")
	if err != nil {
		explorer = "explorer.exe"
	}

	var args []string
	if info.IsDir() {
		args = []string{cleaned}
	} else {
		args = []string{"/select,", cleaned}
	}

	cmd := exec.Command(explorer, args...)
	return cmd.Start()
}

func (a *App) TerminateProcess(pid uint32) error {
    if pid == 0 {
        return fmt.Errorf("process id must be greater than zero")
    }
    if !isProcessRunning(pid) {
        return fmt.Errorf("process %d is not currently running", pid)
    }
    return process.TerminateByPID(pid)
}

func (a *App) SuspendProcess(pid uint32) error {
    if pid == 0 {
        return fmt.Errorf("process id must be greater than zero")
    }
    if !isProcessRunning(pid) {
        return fmt.Errorf("process %d is not currently running", pid)
    }
    return process.SuspendByPID(pid)
}

func (a *App) ResumeProcess(pid uint32) error {
    if pid == 0 {
        return fmt.Errorf("process id must be greater than zero")
    }
    return process.ResumeByPID(pid)
}

func (a *App) LookupVirusTotal(hash string) (*VirusTotalReport, error) {
	report, err := a.lookupVirusTotal(strings.TrimSpace(hash))
	if err != nil {
		return nil, err
	}
	return report, nil
}

func (a *App) RescanVirusTotal(hash string) (*VirusTotalReport, error) {
	report, err := a.triggerVirusTotalRescan(strings.TrimSpace(hash))
	if err != nil {
		return nil, err
	}
	return report, nil
}

func (a *App) UploadVirusTotal(path string) (*VirusTotalReport, error) {
	cleaned := filepath.Clean(strings.TrimSpace(path))
	if cleaned == "" {
		return nil, fmt.Errorf("path is empty")
	}

	info, err := os.Stat(cleaned)
	if err != nil {
		return nil, err
	}
	if info.IsDir() {
		return nil, fmt.Errorf("path points to a directory")
	}
	if info.Size() > maxVirusTotalUploadSize {
		return nil, fmt.Errorf("file exceeds VirusTotal public API upload limit of %d MB", maxVirusTotalUploadSize/1024/1024)
	}

	key := a.getVirusTotalAPIKey()
	if key == "" {
		return nil, fmt.Errorf("VirusTotal API key not configured. Add one in Settings or set VT_API_KEY environment variable")
	}

	hash, err := computeFileHash(cleaned)
	if err != nil {
		return nil, fmt.Errorf("failed to compute file hash: %w", err)
	}
	normalized := strings.ToLower(hash)

	file, err := os.Open(cleaned)
	if err != nil {
		return nil, err
	}
	defer file.Close()

	body := &bytes.Buffer{}
	writer := multipart.NewWriter(body)
	part, err := writer.CreateFormFile("file", filepath.Base(cleaned))
	if err != nil {
		return nil, err
	}
	if _, err := io.Copy(part, file); err != nil {
		return nil, err
	}
	if err := writer.Close(); err != nil {
		return nil, err
	}

	client := a.vtClient
	if client == nil {
		client = &http.Client{Timeout: 15 * time.Second}
		a.vtClient = client
	}

	req, err := http.NewRequest(http.MethodPost, "https://www.virustotal.com/api/v3/files", body)
	if err != nil {
		return nil, err
	}
	req.Header.Set("x-apikey", key)
	req.Header.Set("accept", "application/json")
	req.Header.Set("Content-Type", writer.FormDataContentType())

	resp, err := client.Do(req)
	if err != nil {
		return nil, err
	}
	defer resp.Body.Close()

	if resp.StatusCode >= 300 {
		payload, _ := io.ReadAll(io.LimitReader(resp.Body, 4096))
		return nil, fmt.Errorf("VirusTotal upload failed with status %d: %s", resp.StatusCode, strings.TrimSpace(string(payload)))
	}

	var uploadResp struct {
		Data struct {
			ID string `json:"id"`
		} `json:"data"`
	}
	if err := json.NewDecoder(resp.Body).Decode(&uploadResp); err != nil {
		return nil, fmt.Errorf("failed to decode VirusTotal upload response: %w", err)
	}

	if strings.TrimSpace(uploadResp.Data.ID) == "" {
		return nil, fmt.Errorf("VirusTotal upload did not return an analysis identifier")
	}

	// Clear cached entry for this hash to force a refresh
	a.vtCacheMu.Lock()
	delete(a.vtCache, normalized)
	a.vtCacheMu.Unlock()

	report, err := a.awaitVirusTotalAnalysis(uploadResp.Data.ID, normalized)
	if err != nil {
		return nil, err
	}

	a.setVirusTotalCache(normalized, report)
	return report.Clone(), nil
}

func (a *App) lookupVirusTotal(hash string) (*VirusTotalReport, error) {
	if hash == "" {
		return nil, fmt.Errorf("hash is empty")
	}
	key := a.getVirusTotalAPIKey()
	if key == "" {
		return nil, fmt.Errorf("VirusTotal API key not configured. Add one in Settings or set VT_API_KEY environment variable")
	}

	normalized := strings.ToLower(hash)
	a.vtCacheMu.RLock()
	if cached, ok := a.vtCache[normalized]; ok {
		a.vtCacheMu.RUnlock()
		return cached.Clone(), nil
	}
	a.vtCacheMu.RUnlock()

	client := a.vtClient
	if client == nil {
		client = &http.Client{Timeout: 15 * time.Second}
		a.vtClient = client
	}

	url := fmt.Sprintf("https://www.virustotal.com/api/v3/files/%s", normalized)
	req, err := http.NewRequest(http.MethodGet, url, nil)
	if err != nil {
		return nil, err
	}
	req.Header.Set("x-apikey", key)
	req.Header.Set("accept", "application/json")

	resp, err := client.Do(req)
	if err != nil {
		return nil, err
	}
	defer resp.Body.Close()

	if resp.StatusCode == http.StatusNotFound {
		report := &VirusTotalReport{
			Status: "not_found",
			Hash:   normalized,
			Link:   fmt.Sprintf("https://www.virustotal.com/gui/file/%s/detection", normalized),
		}
		a.setVirusTotalCache(normalized, report)
		return report.Clone(), nil
	}

	if resp.StatusCode == http.StatusUnauthorized || resp.StatusCode == http.StatusForbidden {
		return nil, fmt.Errorf("VirusTotal API access denied (%s)", resp.Status)
	}

	if resp.StatusCode >= 300 {
		body, _ := io.ReadAll(io.LimitReader(resp.Body, 4096))
		return nil, fmt.Errorf("VirusTotal lookup failed with status %d: %s", resp.StatusCode, strings.TrimSpace(string(body)))
	}

	var payload struct {
		Data struct {
			ID         string `json:"id"`
			Attributes struct {
				LastAnalysisDate int64            `json:"last_analysis_date"`
				LastAnalysisStats map[string]int `json:"last_analysis_stats"`
			} `json:"attributes"`
		} `json:"data"`
	}

	if err := json.NewDecoder(resp.Body).Decode(&payload); err != nil {
		return nil, fmt.Errorf("failed to decode VirusTotal response: %w", err)
	}

	stats := payload.Data.Attributes.LastAnalysisStats
	report := &VirusTotalReport{
		Status:    "found",
		Hash:      normalized,
		Link:      fmt.Sprintf("https://www.virustotal.com/gui/file/%s/detection", normalized),
		Malicious: stats["malicious"],
		Suspicious: stats["suspicious"],
		Undetected: stats["undetected"],
		Harmless:   stats["harmless"],
	}

	if ts := payload.Data.Attributes.LastAnalysisDate; ts > 0 {
		report.LastAnalysisDate = time.Unix(ts, 0).UTC().Format(time.RFC3339)
	}

	a.setVirusTotalCache(normalized, report)
	return report.Clone(), nil
}

func (a *App) triggerVirusTotalRescan(hash string) (*VirusTotalReport, error) {
	if hash == "" {
		return nil, fmt.Errorf("hash is empty")
	}
	key := a.getVirusTotalAPIKey()
	if key == "" {
		return nil, fmt.Errorf("VirusTotal API key not configured. Add one in Settings or set VT_API_KEY environment variable")
	}

	normalized := strings.ToLower(hash)
	client := a.vtClient
	if client == nil {
		client = &http.Client{Timeout: 15 * time.Second}
		a.vtClient = client
	}

	url := fmt.Sprintf("https://www.virustotal.com/api/v3/files/%s/analyse", normalized)
	req, err := http.NewRequest(http.MethodPost, url, nil)
	if err != nil {
		return nil, err
	}
	req.Header.Set("x-apikey", key)
	req.Header.Set("accept", "application/json")

	resp, err := client.Do(req)
	if err != nil {
		return nil, err
	}
	defer resp.Body.Close()

	if resp.StatusCode == http.StatusNotFound {
		return nil, fmt.Errorf("file hash not found on VirusTotal; upload required via web interface")
	}

	if resp.StatusCode == http.StatusUnauthorized || resp.StatusCode == http.StatusForbidden {
		return nil, fmt.Errorf("VirusTotal API access denied (%s)", resp.Status)
	}

	if resp.StatusCode >= 300 {
		body, _ := io.ReadAll(io.LimitReader(resp.Body, 4096))
		return nil, fmt.Errorf("VirusTotal rescan request failed with status %d: %s", resp.StatusCode, strings.TrimSpace(string(body)))
	}

	// Delete cached report to force fresh fetch on next lookup
	a.vtCacheMu.Lock()
	delete(a.vtCache, normalized)
	a.vtCacheMu.Unlock()

	report := &VirusTotalReport{
		Status: "queued",
		Hash:   normalized,
		Link:   fmt.Sprintf("https://www.virustotal.com/gui/file/%s/detection", normalized),
		Notes: []string{
			"Rescan requested. VirusTotal may take a few moments to update the analysis.",
		},
	}

	a.setVirusTotalCache(normalized, report)
	return report.Clone(), nil
}

func (a *App) setVirusTotalCache(hash string, report *VirusTotalReport) {
	if report == nil {
		return
	}
	a.vtCacheMu.Lock()
	a.vtCache[hash] = report.Clone()
	a.vtCacheMu.Unlock()
}

func (a *App) awaitVirusTotalAnalysis(analysisID, hash string) (*VirusTotalReport, error) {
	key := a.getVirusTotalAPIKey()
	if key == "" {
		return nil, fmt.Errorf("VirusTotal API key not configured. Add one in Settings or set VT_API_KEY environment variable")
	}

	client := a.vtClient
	if client == nil {
		client = &http.Client{Timeout: 15 * time.Second}
		a.vtClient = client
	}

	url := fmt.Sprintf("https://www.virustotal.com/api/v3/analyses/%s", strings.TrimSpace(analysisID))
	if strings.TrimSpace(analysisID) == "" {
		return nil, fmt.Errorf("analysis identifier is empty")
	}

	const maxAttempts = 6
	for attempt := 0; attempt < maxAttempts; attempt++ {
		req, err := http.NewRequest(http.MethodGet, url, nil)
		if err != nil {
			return nil, err
		}
		req.Header.Set("x-apikey", key)
		req.Header.Set("accept", "application/json")

		resp, err := client.Do(req)
		if err != nil {
			return nil, err
		}

		var analysis struct {
			Data struct {
				Attributes struct {
					Status string         `json:"status"`
					Stats  map[string]int `json:"stats"`
					Date   int64          `json:"date"`
				} `json:"attributes"`
			} `json:"data"`
		}

		decodeErr := json.NewDecoder(resp.Body).Decode(&analysis)
		resp.Body.Close()
		if decodeErr != nil {
			return nil, fmt.Errorf("failed to decode VirusTotal analysis response: %w", decodeErr)
		}

		status := strings.ToLower(strings.TrimSpace(analysis.Data.Attributes.Status))
		if status == "completed" {
			report, err := a.lookupVirusTotal(hash)
			if err == nil {
				return report, nil
			}
			// fallback to analysis stats if file lookup fails
			stats := analysis.Data.Attributes.Stats
			report = &VirusTotalReport{
				Status:    "found",
				Hash:      hash,
				Link:      fmt.Sprintf("https://www.virustotal.com/gui/file/%s/detection", hash),
				Malicious: stats["malicious"],
				Suspicious: stats["suspicious"],
				Undetected: stats["undetected"],
				Harmless:   stats["harmless"],
			}
			if ts := analysis.Data.Attributes.Date; ts > 0 {
				report.LastAnalysisDate = time.Unix(ts, 0).UTC().Format(time.RFC3339)
			}
			return report, nil
		}

		if status == "queued" || status == "in-progress" || status == "pending" {
			if attempt < maxAttempts-1 {
				time.Sleep(5 * time.Second)
				continue
			}
		}

		if status == "failed" {
			return nil, fmt.Errorf("VirusTotal reported analysis failure")
		}
	}

	queuedReport := &VirusTotalReport{
		Status: "queued",
		Hash:   hash,
		Link:   fmt.Sprintf("https://www.virustotal.com/gui/file/%s/detection", hash),
		Notes: []string{
			"Upload queued on VirusTotal. Refresh the report in a few moments to view the analysis results.",
		},
	}
	return queuedReport, nil
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

func (a *App) recordFileSighting(path string, seenAt time.Time) {
    cleaned := strings.TrimSpace(path)
    if cleaned == "" {
        return
    }
    if !looksLikeFilePath(cleaned) {
        return
    }
    cleaned = filepath.Clean(cleaned)
    if cleaned == "" {
        return
    }

    seen := seenAt.UTC()
    a.fileSightingsMu.Lock()
    if existing, ok := a.fileSightings[cleaned]; !ok || seen.Before(existing) {
        a.fileSightings[cleaned] = seen
    }
    a.fileSightingsMu.Unlock()
}

func (a *App) getFileFirstSeen(path string) (time.Time, bool) {
    cleaned := strings.TrimSpace(path)
    if cleaned == "" {
        return time.Time{}, false
    }
    cleaned = filepath.Clean(cleaned)

    a.fileSightingsMu.RLock()
    ts, ok := a.fileSightings[cleaned]
    a.fileSightingsMu.RUnlock()
    return ts, ok
}

func (a *App) GetSettings() Settings {
	return Settings{VirusTotalAPIKey: a.getVirusTotalAPIKey()}
}

func (a *App) UpdateSettings(settings Settings) (Settings, error) {
	key := strings.TrimSpace(settings.VirusTotalAPIKey)
	a.setVirusTotalAPIKey(key)

	if err := a.saveConfig(appConfig{Settings: Settings{VirusTotalAPIKey: key}}); err != nil {
		return Settings{}, err
	}

	return Settings{VirusTotalAPIKey: key}, nil
}

func (a *App) getVirusTotalAPIKey() string {
	a.vtKeyMu.RLock()
	defer a.vtKeyMu.RUnlock()
	return a.vtAPIKey
}

func (a *App) setVirusTotalAPIKey(key string) {
	trimmed := strings.TrimSpace(key)

	a.vtKeyMu.Lock()
	old := a.vtAPIKey
	a.vtAPIKey = trimmed
	a.vtKeyMu.Unlock()

	if trimmed != "" {
		_ = os.Setenv("VT_API_KEY", trimmed)
	} else {
		_ = os.Unsetenv("VT_API_KEY")
	}

	if trimmed != old {
		a.vtCacheMu.Lock()
		a.vtCache = make(map[string]*VirusTotalReport)
		a.vtCacheMu.Unlock()
	}
}

func (a *App) loadConfig() error {
	if strings.TrimSpace(a.configPath) == "" {
		return nil
	}

	data, err := os.ReadFile(a.configPath)
	if err != nil {
		if errors.Is(err, os.ErrNotExist) {
			return nil
		}
		return err
	}

	var cfg appConfig
	if len(data) > 0 {
		if err := json.Unmarshal(data, &cfg); err != nil {
			return err
		}
	}

	a.setVirusTotalAPIKey(strings.TrimSpace(cfg.VirusTotalAPIKey))

	return nil
}

func (a *App) saveConfig(cfg appConfig) error {
	if strings.TrimSpace(a.configPath) == "" {
		return fmt.Errorf("config path not initialized")
	}

	if err := os.MkdirAll(filepath.Dir(a.configPath), 0o755); err != nil {
		return err
	}

	data, err := json.MarshalIndent(cfg, "", "  ")
	if err != nil {
		return err
	}

	return os.WriteFile(a.configPath, data, 0o600)
}

func determineConfigPath() string {
	base := strings.TrimSpace(os.Getenv("APPDATA"))
	if base == "" {
		base = filepath.Join(os.TempDir(), "NoMoreStealers")
	} else {
		base = filepath.Join(base, "NoMoreStealers")
	}
	return filepath.Join(base, "settings.json")
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
