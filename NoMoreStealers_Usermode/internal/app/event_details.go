package app

import (
	"bufio"
	"crypto/sha256"
	"debug/pe"
	"encoding/hex"
	"errors"
	"fmt"
	"io"
	"math"
	"os"
	"path/filepath"
	"strings"
	"time"
	"unicode"
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
    PE               *PEAnalysis      `json:"pe,omitempty"`
    StringsSummary   *StringSummary   `json:"strings,omitempty"`
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
            a.populatePEAnalysis(fileDetails)
            a.populateStringsSummary(fileDetails)
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
                a.populatePEAnalysis(fileDetails)
                a.populateStringsSummary(fileDetails)
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

type PEAnalysis struct {
    Architecture     string          `json:"architecture"`
    FileType         string          `json:"fileType"`
    EntryPoint       string          `json:"entryPoint,omitempty"`
    ImageBase        string          `json:"imageBase,omitempty"`
    NumberOfSections int             `json:"numberOfSections"`
    SizeOfImage      string          `json:"sizeOfImage,omitempty"`
    ImportCount      int             `json:"importCount,omitempty"`
    ExportCount      int             `json:"exportCount,omitempty"`
    Sections         []PESectionInfo `json:"sections,omitempty"`
}

type PESectionInfo struct {
    Name           string   `json:"name"`
    VirtualSize    string   `json:"virtualSize"`
    VirtualAddress string   `json:"virtualAddress"`
    RawSize        string   `json:"rawSize"`
    Entropy        float64  `json:"entropy"`
    EntropyLabel   string   `json:"entropyLabel"`
    Permissions    []string `json:"permissions,omitempty"`
    Risk           string   `json:"risk"`
}

type StringSummary struct {
    Count    int               `json:"count"`
    Samples  []string          `json:"samples"`
    Keywords map[string][]string `json:"keywords,omitempty"`
}

var errNotPE = errors.New("not a PE file")

func (a *App) populatePEAnalysis(file *FileDetails) {
    if file == nil || !file.Exists || file.IsDir || strings.TrimSpace(file.Path) == "" {
        return
    }

    analysis, err := analyzePE(file.Path)
    if err != nil {
        if !errors.Is(err, errNotPE) {
            file.Notes = append(file.Notes, fmt.Sprintf("PE analysis failed: %v", err))
        }
        return
    }
    file.PE = analysis
}

func (a *App) populateStringsSummary(file *FileDetails) {
    if file == nil || !file.Exists || file.IsDir {
        return
    }
    summary, err := extractStrings(file.Path, 4, 1024)
    if err != nil {
        file.Notes = append(file.Notes, fmt.Sprintf("String extraction failed: %v", err))
        return
    }
    file.StringsSummary = summary
}

func analyzePE(path string) (*PEAnalysis, error) {
    peFile, err := pe.Open(path)
    if err != nil {
        var formatErr *pe.FormatError
        if errors.As(err, &formatErr) {
            return nil, errNotPE
        }
        return nil, err
    }
    defer peFile.Close()

    analysis := &PEAnalysis{}
    analysis.Sections = make([]PESectionInfo, 0, len(peFile.Sections))

    switch opt := peFile.OptionalHeader.(type) {
    case *pe.OptionalHeader32:
        analysis.Architecture = "x86 (32-bit)"
        entryVA := uint64(opt.ImageBase) + uint64(opt.AddressOfEntryPoint)
        analysis.EntryPoint = fmt.Sprintf("0x%08X", entryVA)
        analysis.ImageBase = fmt.Sprintf("0x%08X", opt.ImageBase)
        analysis.SizeOfImage = fmt.Sprintf("0x%X", opt.SizeOfImage)
    case *pe.OptionalHeader64:
        analysis.Architecture = "x64 (64-bit)"
        entryVA := opt.ImageBase + uint64(opt.AddressOfEntryPoint)
        analysis.EntryPoint = fmt.Sprintf("0x%016X", entryVA)
        analysis.ImageBase = fmt.Sprintf("0x%016X", opt.ImageBase)
        analysis.SizeOfImage = fmt.Sprintf("0x%X", opt.SizeOfImage)
    default:
        analysis.Architecture = "Unknown"
    }

    fileType := "Native Binary"
    characteristics := peFile.FileHeader.Characteristics
    if characteristics&pe.IMAGE_FILE_DLL != 0 {
        fileType = "Dynamic Library"
    }
    analysis.FileType = fileType

    imports, _ := peFile.ImportedSymbols()
    analysis.ImportCount = len(imports)
    analysis.ExportCount = 0
    analysis.NumberOfSections = len(peFile.Sections)

    for _, section := range peFile.Sections {
        name := strings.TrimRight(section.Name, "\x00")
        if name == "" {
            name = fmt.Sprintf("section_%d", len(analysis.Sections)+1)
        }

        data, err := section.Data()
        if err != nil {
            data = nil
        }
        entropy := calculateEntropy(data)
        entropyLabel := classifyEntropy(entropy)
        perms := sectionPermissions(section.Characteristics)
        risk := sectionRisk(entropyLabel, perms)

        info := PESectionInfo{
            Name:           name,
            VirtualSize:    fmt.Sprintf("0x%X", section.VirtualSize),
            VirtualAddress: fmt.Sprintf("0x%X", section.VirtualAddress),
            RawSize:        fmt.Sprintf("0x%X", section.Size),
            Entropy:        entropy,
            EntropyLabel:   entropyLabel,
            Permissions:    perms,
            Risk:           risk,
        }
        analysis.Sections = append(analysis.Sections, info)
    }

    if analysis.EntryPoint == "" {
        analysis.EntryPoint = "Unknown"
    }
    if analysis.ImageBase == "" {
        analysis.ImageBase = "Unknown"
    }

    return analysis, nil
}

func calculateEntropy(data []byte) float64 {
    if len(data) == 0 {
        return 0
    }

    var freq [256]float64
    for _, b := range data {
        freq[b]++
    }

    entropy := 0.0
    dataLen := float64(len(data))
    for _, count := range freq {
        if count == 0 {
            continue
        }
        p := count / dataLen
        entropy -= p * math.Log2(p)
    }
    return entropy
}

func classifyEntropy(entropy float64) string {
    switch {
    case entropy >= 7.2:
        return "High"
    case entropy >= 6.0:
        return "Medium"
    default:
        return "Low"
    }
}

func sectionPermissions(characteristics uint32) []string {
    perms := make([]string, 0, 3)
    if characteristics&pe.IMAGE_SCN_MEM_READ != 0 {
        perms = append(perms, "Read")
    }
    if characteristics&pe.IMAGE_SCN_MEM_WRITE != 0 {
        perms = append(perms, "Write")
    }
    if characteristics&pe.IMAGE_SCN_MEM_EXECUTE != 0 {
        perms = append(perms, "Execute")
    }
    return perms
}

func sectionRisk(entropyLabel string, perms []string) string {
    hasExecute := false
    hasWrite := false
    for _, p := range perms {
        if p == "Execute" {
            hasExecute = true
        }
        if p == "Write" {
            hasWrite = true
        }
    }

    if hasExecute && hasWrite {
        return "High"
    }
    if hasExecute && entropyLabel == "High" {
        return "High"
    }
    if entropyLabel == "High" || (hasExecute && entropyLabel == "Medium") {
        return "Medium"
    }
    return "Low"
}

func extractStrings(path string, minLen int, sampleLimit int) (*StringSummary, error) {
    f, err := os.Open(path)
    if err != nil {
        return nil, err
    }
    defer f.Close()

    reader := bufio.NewReader(f)
    var current strings.Builder
    stringsFound := make([]string, 0)

    flush := func() {
        if current.Len() >= minLen {
            str := current.String()
            stringsFound = append(stringsFound, str)
        }
        current.Reset()
    }

    for {
        r, _, err := reader.ReadRune()
        if err != nil {
            if err == io.EOF {
                flush()
                break
            }
            return nil, err
        }

        if unicode.IsPrint(r) {
            current.WriteRune(r)
        } else {
            flush()
        }
    }

    summary := &StringSummary{Count: len(stringsFound)}
    if len(stringsFound) == 0 {
        return summary, nil
    }

    limit := sampleLimit
    if limit <= 0 || limit > len(stringsFound) {
        limit = len(stringsFound)
    }
    summary.Samples = append(summary.Samples, stringsFound[:limit]...)

    keywords := []string{"http", "https", "cmd.exe", "powershell", "token", "secret", "api", "password"}
    summary.Keywords = make(map[string][]string)
    maxMatches := 50

    for _, str := range stringsFound {
        lower := strings.ToLower(str)
        for _, kw := range keywords {
            if strings.Contains(lower, kw) {
                list := summary.Keywords[kw]
                if len(list) < maxMatches {
                    summary.Keywords[kw] = append(list, str)
                }
            }
        }
    }

    return summary, nil
}

