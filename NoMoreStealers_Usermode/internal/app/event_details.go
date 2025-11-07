package app

import (
    "crypto/sha256"
    "encoding/hex"
    "fmt"
    "io"
    "os"
    "path/filepath"
    "strings"
    "time"
    "unsafe"

    "NoMoreStealers/internal/process"
    "golang.org/x/sys/windows"
)

const maxHashSizeBytes int64 = 100 * 1024 * 1024

type FileDetails struct {
    Path             string   `json:"path"`
    Exists           bool     `json:"exists"`
    IsDir            bool     `json:"isDir"`
    Size             int64    `json:"size,omitempty"`
    Modified         string   `json:"modified,omitempty"`
    Created          string   `json:"created,omitempty"`
    Sha256           string   `json:"sha256,omitempty"`
    HashAvailable    bool     `json:"hashAvailable"`
    HashSkippedReason string  `json:"hashSkippedReason,omitempty"`
    IsSigned         bool     `json:"isSigned"`
    Notes            []string `json:"notes,omitempty"`
    FirstSeen        string   `json:"firstSeen,omitempty"`
    VirusTotal       *VirusTotalReport `json:"virusTotal,omitempty"`
}

type VirusTotalReport struct {
    Status          string   `json:"status"`
    Hash            string   `json:"hash"`
    Link            string   `json:"link,omitempty"`
    LastAnalysisDate string  `json:"lastAnalysisDate,omitempty"`
    Malicious       int      `json:"malicious,omitempty"`
    Suspicious      int      `json:"suspicious,omitempty"`
    Undetected      int      `json:"undetected,omitempty"`
    Harmless        int      `json:"harmless,omitempty"`
    Notes           []string `json:"notes,omitempty"`
}

func (v *VirusTotalReport) Clone() *VirusTotalReport {
    if v == nil {
        return nil
    }
    clone := *v
    if v.Notes != nil {
        clone.Notes = append([]string(nil), v.Notes...)
    }
    return &clone
}

type EventDetails struct {
    EventType        string       `json:"eventType"`
    Source           string       `json:"source"`
    Timestamp        string       `json:"timestamp"`
    ProcessName      string       `json:"processName"`
    PID              uint32       `json:"pid"`
    IsProcessRunning bool         `json:"isProcessRunning"`
    Executable       *FileDetails `json:"executable,omitempty"`
    Target           *FileDetails `json:"target,omitempty"`
    TargetRaw        string       `json:"targetRaw,omitempty"`
    Notes            []string     `json:"notes,omitempty"`
}

const processQueryLimitedInformation = 0x1000

func (a *App) GetEventDetails(event Event) (EventDetails, error) {
    details := EventDetails{
        EventType:   event.Type,
        Source:      determineEventSource(event.Type),
        Timestamp:   event.Timestamp,
        ProcessName: event.ProcessName,
        PID:         event.PID,
    }

    if strings.TrimSpace(details.Timestamp) == "" {
        details.Timestamp = time.Now().UTC().Format(time.RFC3339)
    }

    if event.PID != 0 {
        details.IsProcessRunning = isProcessRunning(event.PID)
        if !details.IsProcessRunning {
            details.Notes = append(details.Notes, fmt.Sprintf("Process %d is no longer running", event.PID))
        }
    }

    executablePath := strings.TrimSpace(event.ExecutablePath)
    if executablePath == "" && event.PID != 0 {
        if resolved, err := process.GetFilePathFromPID(event.PID); err == nil && resolved != "" {
            executablePath = resolved
        }
    }

    if executablePath != "" {
        cleaned := filepath.Clean(executablePath)
        if fileDetails, err := gatherFileDetails(cleaned, true); err == nil {
            details.Executable = fileDetails
            if firstSeen, ok := a.getFileFirstSeen(cleaned); ok && !firstSeen.IsZero() {
                fileDetails.FirstSeen = firstSeen.UTC().Format(time.RFC3339)
            }
            a.populateVirusTotalDetails(fileDetails)
            if !fileDetails.Exists {
                details.Notes = append(details.Notes, fmt.Sprintf("Executable path not found: %s", cleaned))
            }
        } else {
            details.Notes = append(details.Notes, fmt.Sprintf("Failed to read executable metadata: %v", err))
        }
    } else {
        details.Notes = append(details.Notes, "Executable path unavailable")
    }

    targetPath := strings.TrimSpace(event.Path)
    if targetPath != "" {
        if looksLikeFilePath(targetPath) {
            cleaned := filepath.Clean(targetPath)
            if fileDetails, err := gatherFileDetails(cleaned, false); err == nil {
                details.Target = fileDetails
                if firstSeen, ok := a.getFileFirstSeen(cleaned); ok && !firstSeen.IsZero() {
                    fileDetails.FirstSeen = firstSeen.UTC().Format(time.RFC3339)
                }
                a.populateVirusTotalDetails(fileDetails)
            } else {
                details.Notes = append(details.Notes, fmt.Sprintf("Failed to read target metadata: %v", err))
            }
        } else {
            details.TargetRaw = targetPath
        }
    }

    return details, nil
}

func determineEventSource(eventType string) string {
    switch strings.ToLower(eventType) {
    case "blocked", "allowed":
        return "Protector"
    case "killswitch_terminated", "killswitch_failed":
        return "Anti Rat"
    default:
        return "Events"
    }
}

func isProcessRunning(pid uint32) bool {
    if pid == 0 {
        return false
    }
    handle, err := windows.OpenProcess(processQueryLimitedInformation, false, pid)
    if err != nil {
        return false
    }
    defer windows.CloseHandle(handle)
    return true
}

func gatherFileDetails(path string, checkSignature bool) (*FileDetails, error) {
    details := &FileDetails{
        Path: strings.TrimSpace(path),
    }

    info, err := os.Stat(path)
    if err != nil {
        if os.IsNotExist(err) {
            details.Exists = false
            details.Notes = append(details.Notes, "File does not exist")
            return details, nil
        }
        details.Exists = false
        details.Notes = append(details.Notes, err.Error())
        return details, nil
    }

    details.Exists = true
    details.IsDir = info.IsDir()
    if !info.IsDir() {
        details.Size = info.Size()
    }
    details.Modified = info.ModTime().UTC().Format(time.RFC3339)

    if !info.IsDir() {
        if created, err := getFileCreationTime(path); err == nil {
            details.Created = created.UTC().Format(time.RFC3339)
        }

        if info.Size() <= maxHashSizeBytes {
            if hash, err := computeFileHash(path); err == nil {
                details.Sha256 = hash
                details.HashAvailable = true
            } else {
                details.HashSkippedReason = fmt.Sprintf("Failed to compute hash: %v", err)
            }
        } else {
            details.HashAvailable = false
            details.HashSkippedReason = fmt.Sprintf("Skipped - file larger than %d MB", maxHashSizeBytes/1024/1024)
        }

        if checkSignature {
            details.IsSigned = process.IsFileSigned(path)
        }
    } else {
        details.HashAvailable = false
        if checkSignature {
            details.Notes = append(details.Notes, "Signature check skipped for directory")
        }
    }

    return details, nil
}

func (a *App) populateVirusTotalDetails(file *FileDetails) {
    if file == nil {
        return
    }
    if !file.HashAvailable || strings.TrimSpace(file.Sha256) == "" {
        return
    }

    report, err := a.lookupVirusTotal(file.Sha256)
    if err != nil {
        lower := strings.ToLower(err.Error())
        if strings.Contains(lower, "not configured") {
            file.Notes = append(file.Notes, "Add a VirusTotal API key in Settings (or set the VT_API_KEY environment variable) to enable VirusTotal lookups.")
        } else {
            file.Notes = append(file.Notes, fmt.Sprintf("VirusTotal lookup failed: %v", err))
        }
        return
    }
    file.VirusTotal = report
}

func computeFileHash(path string) (string, error) {
    file, err := os.Open(path)
    if err != nil {
        return "", err
    }
    defer file.Close()

    hasher := sha256.New()
    if _, err := io.Copy(hasher, file); err != nil {
        return "", err
    }
    return hex.EncodeToString(hasher.Sum(nil)), nil
}

func getFileCreationTime(path string) (time.Time, error) {
    var data windows.Win32FileAttributeData
    pathPtr, err := windows.UTF16PtrFromString(path)
    if err != nil {
        return time.Time{}, err
    }
    err = windows.GetFileAttributesEx(pathPtr, windows.GetFileExInfoStandard, (*byte)(unsafe.Pointer(&data)))
    if err != nil {
        return time.Time{}, err
    }
    return time.Unix(0, data.CreationTime.Nanoseconds()), nil
}

func looksLikeFilePath(path string) bool {
    trimmed := strings.TrimSpace(path)
    if trimmed == "" {
        return false
    }
    if strings.HasPrefix(trimmed, "\\\\") {
        return true
    }
    if len(trimmed) >= 3 && trimmed[1] == ':' && (trimmed[2] == '\\' || trimmed[2] == '/') {
        return true
    }
    return filepath.IsAbs(trimmed)
}

