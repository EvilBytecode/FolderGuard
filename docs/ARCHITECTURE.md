# 🏗️ NoMoreStealer Architecture

> **Technical analysis of the actual implementation based on source code examination**

---

## System Overview

NoMoreStealer consists of two main components that communicate via shared memory:

```
┌─────────────────────────────────────────────────────────────────┐
│                    User-Mode Application                        │
│  ┌─────────────────┐  ┌──────────────────┐  ┌─────────────────┐ │
│  │  Wails Frontend │  │   Go Backend     │  │  Antispy Module │ │
│  │   (HTML/JS)     │  │  (Event Monitor) │  │ (Screen Block)  │ │
│  └─────────────────┘  └──────────────────┘  └─────────────────┘ │
└─────────────────────────────────────────────────────────────────┘
                                │
                    ┌───────────▼───────────┐
                    │   Shared Memory       │
                    │   (PAGE_SIZE = 4KB)   │
                    └───────────┬───────────┘
                                │
┌─────────────────────────────────────────────────────────────────┐
│                     Kernel Driver                               │
│  ┌─────────────────┐  ┌──────────────────┐  ┌─────────────────┐ │
│  │   Callbacks     │  │      Paths       │  │    Process      │ │
│  │ (IRP_MJ_CREATE) │  │  (Hardcoded)     │  │  (Allowlists)   │ │
│  └─────────────────┘  └──────────────────┘  └─────────────────┘ │
└─────────────────────────────────────────────────────────────────┘
                                │
                    ┌───────────▼───────────┐
                    │   Filter Manager      │
                    │   (Windows Kernel)    │
                    └───────────────────────┘
```

---

## Kernel Driver Implementation

### Driver Entry Point (`main.cpp`)

The driver registers as a minifilter with minimal configuration:

```cpp
FLT_OPERATION_REGISTRATION CallbacksArray[] = {
    { IRP_MJ_CREATE, 0, Callbacks::PreOperation, nullptr },
    { IRP_MJ_OPERATION_END, 0, nullptr, nullptr }
};
```

**Key Points:**
- Only hooks `IRP_MJ_CREATE` operations
- No post-operation callbacks
- Altitude: `371000` (Activity Monitor range)
- Filter Group: `FSFilter Activity Monitor`

### File System Callbacks (`internal/Callbacks/callbacks.cpp`)

The core interception logic in `PreOperation()`:

**What it actually does:**
1. **Skips paging I/O**: `if (FlagOn(Data->Iopb->IrpFlags, IRP_PAGING_IO))`
2. **Limited IRP filtering**: Only processes specific major functions
3. **Gets file path**: Uses `FltGetFileNameInformation()` with normalized paths
4. **Checks protection**: Calls `Paths::IsProtected()` for prefix matching
5. **Evaluates trust**: Calls `Process::IsAllowed()` for the calling process
6. **Blocks or allows**: Returns `FLT_PREOP_COMPLETE` with `STATUS_ACCESS_DENIED` or continues

**Detection Logic:**
- `IsRawDiskAccess()`: Detects direct disk/volume access attempts
- `IsDestructiveOperation()`: Identifies write/delete/rename operations
- `IsSuspiciousAccess()`: Flags delete-on-close and backup-intent operations

### Path Management (`internal/Paths/paths.cpp`)

**Implementation Details:**
- **Static Array**: `ProtectedPath gProtectedPaths[128]` - maximum 128 paths
- **Memory Management**: Uses `ExAllocatePoolZero()` with tag `'tPMM'`
- **Thread Safety**: Uses `ERESOURCE gPathsLock` for synchronization
- **Matching Algorithm**: Simple `RtlPrefixUnicodeString()` case-insensitive matching

**Hardcoded Protected Paths:**
```cpp
void DiscoverDefaultPaths() {
    // Browser data
    Add(L"\\Google\\Chrome\\User Data");
    Add(L"\\Microsoft\\Edge\\User Data");
    Add(L"\\Mozilla\\Firefox\\Profiles");
    
    // Cryptocurrency wallets
    Add(L"\\AppData\\Local\\Exodus");
    Add(L"\\AppData\\Roaming\\Electrum\\wallets");
    
    // Communication apps
    Add(L"\\AppData\\Roaming\\Discord");
    Add(L"\\AppData\\Roaming\\Telegram Desktop");
    
    // And many more...
}
```

### Process Trust Evaluation (`internal/Process/process.cpp`)

**Trust Determination Logic:**
1. **System Processes**: Hardcoded list of critical Windows processes
2. **Known Trusted**: Hardcoded allowlist of common applications
3. **Protected Processes**: Uses `PsIsProtectedProcessLight()` (Windows 10+ only)

**Actual Implementation:**
```cpp
BOOLEAN IsAllowed(PEPROCESS process) {
    if (!process) return FALSE;
    
    if (IsSystemProcess(process)) return TRUE;
    if (IsKnownTrustedProcess(process)) return TRUE;
    if (IsProcessProtected(process)) return TRUE;
    
    return FALSE;
}
```

**Known Trusted Processes (Hardcoded):**
- `chrome.exe`, `msedge.exe`, `firefox.exe`
- `discord.exe`, `telegram.exe`, `signal.exe`
- `exodus.exe`, `electrum.exe`, `bitcoin-qt.exe`
- `explorer.exe`, `java.exe`, `javaw.exe`

### Communication Module (`internal/Comm/comm.cpp`)

**Shared Memory Implementation:**
- **Section Name**: `\BaseNamedObjects\NoMoreStealersNotify`
- **Size**: `PAGE_SIZE` (4096 bytes)
- **Structure**: `NoMoreStealersNotifyData` with process info and path
- **Synchronization**: Uses `KSPIN_LOCK` and work queue items

**Notification Process:**
1. `NotifyBlock()` creates work item with event data
2. Work item queued to `DelayedWorkQueue`
3. `NotifyWorkRoutine()` writes to shared memory
4. Sets `ready` flag to signal user-mode application

---

## User-Mode Application Implementation

### Main Application (`internal/app/app.go`)

**Core Functionality:**
- **Shared Memory Reader**: Maps kernel section and polls for events
- **Event Processing**: Converts kernel notifications to UI events
- **WebSocket Server**: Broadcasts events to frontend on port `34116`
- **System Tray**: Creates tray icon with click-to-show functionality

**Event Monitoring Loop:**
```go
func (a *App) monitorLoop() {
    ticker := time.NewTicker(5 * time.Second)
    for {
        // Poll shared memory every 100ms
        currentReady := atomic.LoadUint32(atomicReady)
        if currentReady == 1 && a.LastReady == 0 {
            // Process new event from kernel
            // Extract path, process name, PID
            // Verify digital signature
            // Send to frontend
        }
    }
}
```

### Communication Layer (`internal/comm/comm.go`)

**Shared Memory Access:**
- **NT APIs**: Uses `NtOpenSection` and `NtMapViewOfSection`
- **Structure Mapping**: Maps to `NoMoreStealersNotifyData` struct
- **Data Layout**: PID (4 bytes) + PathLen (4 bytes) + ProcName (64 bytes) + Ready flag (4 bytes) + Path data

### Process Verification (`internal/process/process.go`)

**Digital Signature Checking:**
- **WinVerifyTrust**: Uses `wintrust.dll` for signature verification
- **Dual Verification**: Checks both embedded signatures and catalog signatures
- **Path Conversion**: Converts device paths to DOS paths for verification

### Anti-Spy Module (`internal/antispy/antispy.go`)

**Screen Capture Blocking:**
- **Overlay Window**: Creates fullscreen transparent window with `WS_EX_TOPMOST`
- **Display Affinity**: Uses `SetWindowDisplayAffinity(WDA_MONITOR)` to block capture
- **Window Management**: Proper cleanup with message loop handling

### Frontend (`frontend/`)

**Wails Integration:**
- **HTML/CSS/JS**: Modern web technologies with Tailwind CSS
- **WebSocket Client**: Connects to Go backend on `ws://localhost:34116/ws`
- **Real-time Updates**: Live event display with animations
- **Tutorial System**: Step-by-step driver setup guide

---

## Data Flow Analysis

### Event Processing Pipeline

1. **Kernel Detection**:
   ```
   File Access → IRP_MJ_CREATE → PreOperation() → Path Check → Trust Check → Block/Allow
   ```

2. **Notification Path**:
   ```
   NotifyBlock() → Work Queue → Shared Memory → User-Mode Poll → Frontend Display
   ```

3. **Frontend Updates**:
   ```
   Shared Memory → Go Backend → WebSocket → JavaScript → DOM Update
   ```

### Memory Layout

**Shared Memory Structure:**
```
Offset 0x00: PID (uint32)
Offset 0x04: PathLen (uint32) 
Offset 0x08: ProcName[64] (byte array)
Offset 0x48: Ready (uint32)
Offset 0x4C: Path data (variable length, UTF-16)
```

---

## Performance Characteristics

### Kernel Driver
- **Memory Usage**: ~10MB for path storage and work items
- **CPU Impact**: Minimal - only processes `IRP_MJ_CREATE` operations
- **I/O Impact**: No significant performance degradation observed

### User-Mode Application
- **Memory Usage**: ~50-100MB (typical Wails application)
- **CPU Usage**: Low - polls shared memory every 100ms
- **Network**: Local WebSocket server only

---

## Security Analysis

### Current Protections
- **Kernel-level interception** of file system operations
- **Process signature verification** using Windows APIs
- **Real-time monitoring** with immediate blocking
- **Screen capture prevention** via display affinity

### Known Vulnerabilities
- **Process name spoofing**: Malware can name itself `chrome.exe`
- **Limited IRP coverage**: Only monitors file creation, not writes
- **Hardcoded paths**: Cannot adapt to new stealer techniques
- **No parent process checking**: Doesn't verify process genealogy
- **Bypass via alternate APIs**: Direct NT APIs might bypass filter

### Attack Vectors
1. **Filename Impersonation**: Copy legitimate process names
2. **Process Hollowing**: Inject into trusted processes
3. **Alternate Data Streams**: Access files via ADS
4. **Memory Mapping**: Direct memory access to files
5. **Registry Manipulation**: Modify trust settings

---

## Configuration Details

### Registry Settings
```
HKLM\SYSTEM\CurrentControlSet\Services\NoMoreStealer\
├── Type = 2 (FILE_DEVICE_FILE_SYSTEM)
├── Start = 3 (SERVICE_DEMAND_START)
├── ErrorControl = 1 (SERVICE_ERROR_NORMAL)
├── Group = "FSFilter Activity Monitor"
├── ImagePath = "\SystemRoot\System32\drivers\NoMoreStealer.sys"
└── Instances\NoMoreStealer Instance\
    ├── Altitude = "371000"
    └── Flags = 0
```

### Wails Configuration (`wails.json`)
```json
{
  "name": "NoMoreStealers Client",
  "width": 1024,
  "height": 768,
  "frameless": true,
  "backgroundColour": "#000000",
  "devServer": "localhost:34115"
}
```

---

## Limitations and Future Improvements

### Current Limitations
1. **Demo Status**: Explicitly not production-ready
2. **Single IRP Type**: Only monitors `IRP_MJ_CREATE`
3. **Static Configuration**: Paths hardcoded in driver
4. **Basic Trust Model**: Easily bypassed by sophisticated malware
5. **Debug Logging**: Relies on `DbgPrint` for kernel debugging

### Potential Improvements
1. **Enhanced IRP Coverage**: Monitor writes, renames, deletions
2. **Dynamic Configuration**: Runtime path updates via IOCTL
3. **Behavioral Analysis**: Process behavior monitoring
4. **Certificate Validation**: Proper code signing verification
5. **Machine Learning**: Anomaly detection for unknown threats

---

## Development Notes

### Build Requirements
- **Visual Studio 2019/2022** with Windows Driver Kit (WDK)
- **Go 1.19+** for Wails application
- **Node.js** for frontend dependencies (optional)

### Debugging Tools
- **DebugView**: Essential for viewing `DbgPrint` output
- **Process Monitor**: Useful for testing file access patterns
- **WinDbg**: For kernel debugging (advanced)

### Testing Approach
- **Virtual Machines**: Always test in isolated environment
- **System Restore**: Create restore points before testing
- **Driver Verifier**: Use for stability testing (advanced)

---

<div align="center">

**This architecture analysis is based on actual source code examination**

*Understanding the implementation helps in contributing and extending the project*

# 🏗️ NoMoreStealer Architecture

> **Technical analysis of the actual implementation based on source code examination**

---

## System Overview

NoMoreStealer consists of two main components that communicate via shared memory:

```
┌─────────────────────────────────────────────────────────────────┐
│                    User-Mode Application                        │
│  ┌─────────────────┐  ┌──────────────────┐  ┌─────────────────┐ │
│  │  Wails Frontend │  │   Go Backend     │  │  Antispy Module │ │
│  │   (HTML/JS)     │  │  (Event Monitor) │  │ (Screen Block)  │ │
│  └─────────────────┘  └──────────────────┘  └─────────────────┘ │
└─────────────────────────────────────────────────────────────────┘
                                │
                    ┌───────────▼───────────┐
                    │   Shared Memory       │
                    │   (PAGE_SIZE = 4KB)   │
                    └───────────┬───────────┘
                                │
┌─────────────────────────────────────────────────────────────────┐
│                     Kernel Driver                               │
│  ┌─────────────────┐  ┌──────────────────┐  ┌─────────────────┐ │
│  │   Callbacks     │  │      Paths       │  │    Process      │ │
│  │ (IRP_MJ_CREATE) │  │  (Hardcoded)     │  │  (Allowlists)   │ │
│  └─────────────────┘  └──────────────────┘  └─────────────────┘ │
└─────────────────────────────────────────────────────────────────┘
                                │
                    ┌───────────▼───────────┐
                    │   Filter Manager      │
                    │   (Windows Kernel)    │
                    └───────────────────────┘
```

---

## Kernel Driver Implementation

### Driver Entry Point (`main.cpp`)

The driver registers as a minifilter with minimal configuration:

```cpp
FLT_OPERATION_REGISTRATION CallbacksArray[] = {
    { IRP_MJ_CREATE, 0, Callbacks::PreOperation, nullptr },
    { IRP_MJ_OPERATION_END, 0, nullptr, nullptr }
};
```

**Key Points:**
- Only hooks `IRP_MJ_CREATE` operations
- No post-operation callbacks
- Altitude: `371000` (Activity Monitor range)
- Filter Group: `FSFilter Activity Monitor`

### File System Callbacks (`internal/Callbacks/callbacks.cpp`)

The core interception logic in `PreOperation()`:

**What it actually does:**
1. **Skips paging I/O**: `if (FlagOn(Data->Iopb->IrpFlags, IRP_PAGING_IO))`
2. **Limited IRP filtering**: Only processes specific major functions
3. **Gets file path**: Uses `FltGetFileNameInformation()` with normalized paths
4. **Checks protection**: Calls `Paths::IsProtected()` for prefix matching
5. **Evaluates trust**: Calls `Process::IsAllowed()` for the calling process
6. **Blocks or allows**: Returns `FLT_PREOP_COMPLETE` with `STATUS_ACCESS_DENIED` or continues

**Detection Logic:**
- `IsRawDiskAccess()`: Detects direct disk/volume access attempts
- `IsDestructiveOperation()`: Identifies write/delete/rename operations
- `IsSuspiciousAccess()`: Flags delete-on-close and backup-intent operations

### Path Management (`internal/Paths/paths.cpp`)

**Implementation Details:**
- **Static Array**: `ProtectedPath gProtectedPaths[128]` - maximum 128 paths
- **Memory Management**: Uses `ExAllocatePoolZero()` with tag `'tPMM'`
- **Thread Safety**: Uses `ERESOURCE gPathsLock` for synchronization
- **Matching Algorithm**: Simple `RtlPrefixUnicodeString()` case-insensitive matching

**Hardcoded Protected Paths:**
```cpp
void DiscoverDefaultPaths() {
    // Browser data
    Add(L"\\Google\\Chrome\\User Data");
    Add(L"\\Microsoft\\Edge\\User Data");
    Add(L"\\Mozilla\\Firefox\\Profiles");
    
    // Cryptocurrency wallets
    Add(L"\\AppData\\Local\\Exodus");
    Add(L"\\AppData\\Roaming\\Electrum\\wallets");
    
    // Communication apps
    Add(L"\\AppData\\Roaming\\Discord");
    Add(L"\\AppData\\Roaming\\Telegram Desktop");
    
    // And many more...
}
```

### Process Trust Evaluation (`internal/Process/process.cpp`)

**Trust Determination Logic:**
1. **System Processes**: Hardcoded list of critical Windows processes
2. **Known Trusted**: Hardcoded allowlist of common applications
3. **Protected Processes**: Uses `PsIsProtectedProcessLight()` (Windows 10+ only)

**Actual Implementation:**
```cpp
BOOLEAN IsAllowed(PEPROCESS process) {
    if (!process) return FALSE;
    
    if (IsSystemProcess(process)) return TRUE;
    if (IsKnownTrustedProcess(process)) return TRUE;
    if (IsProcessProtected(process)) return TRUE;
    
    return FALSE;
}
```

**Known Trusted Processes (Hardcoded):**
- `chrome.exe`, `msedge.exe`, `firefox.exe`
- `discord.exe`, `telegram.exe`, `signal.exe`
- `exodus.exe`, `electrum.exe`, `bitcoin-qt.exe`
- `explorer.exe`, `java.exe`, `javaw.exe`

### Communication Module (`internal/Comm/comm.cpp`)

**Shared Memory Implementation:**
- **Section Name**: `\BaseNamedObjects\NoMoreStealersNotify`
- **Size**: `PAGE_SIZE` (4096 bytes)
- **Structure**: `NoMoreStealersNotifyData` with process info and path
- **Synchronization**: Uses `KSPIN_LOCK` and work queue items

**Notification Process:**
1. `NotifyBlock()` creates work item with event data
2. Work item queued to `DelayedWorkQueue`
3. `NotifyWorkRoutine()` writes to shared memory
4. Sets `ready` flag to signal user-mode application

---

## User-Mode Application Implementation

### Main Application (`internal/app/app.go`)

**Core Functionality:**
- **Shared Memory Reader**: Maps kernel section and polls for events
- **Event Processing**: Converts kernel notifications to UI events
- **WebSocket Server**: Broadcasts events to frontend on port `34116`
- **System Tray**: Creates tray icon with click-to-show functionality

**Event Monitoring Loop:**
```go
func (a *App) monitorLoop() {
    ticker := time.NewTicker(5 * time.Second)
    for {
        // Poll shared memory every 100ms
        currentReady := atomic.LoadUint32(atomicReady)
        if currentReady == 1 && a.LastReady == 0 {
            // Process new event from kernel
            // Extract path, process name, PID
            // Verify digital signature
            // Send to frontend
        }
    }
}
```

### Communication Layer (`internal/comm/comm.go`)

**Shared Memory Access:**
- **NT APIs**: Uses `NtOpenSection` and `NtMapViewOfSection`
- **Structure Mapping**: Maps to `NoMoreStealersNotifyData` struct
- **Data Layout**: PID (4 bytes) + PathLen (4 bytes) + ProcName (64 bytes) + Ready flag (4 bytes) + Path data

### Process Verification (`internal/process/process.go`)

**Digital Signature Checking:**
- **WinVerifyTrust**: Uses `wintrust.dll` for signature verification
- **Dual Verification**: Checks both embedded signatures and catalog signatures
- **Path Conversion**: Converts device paths to DOS paths for verification

### Anti-Spy Module (`internal/antispy/antispy.go`)

**Screen Capture Blocking:**
- **Overlay Window**: Creates fullscreen transparent window with `WS_EX_TOPMOST`
- **Display Affinity**: Uses `SetWindowDisplayAffinity(WDA_MONITOR)` to block capture
- **Window Management**: Proper cleanup with message loop handling

### Frontend (`frontend/`)

**Wails Integration:**
- **HTML/CSS/JS**: Modern web technologies with Tailwind CSS
- **WebSocket Client**: Connects to Go backend on `ws://localhost:34116/ws`
- **Real-time Updates**: Live event display with animations
- **Tutorial System**: Step-by-step driver setup guide

---

## Data Flow Analysis

### Event Processing Pipeline

1. **Kernel Detection**:
   ```
   File Access → IRP_MJ_CREATE → PreOperation() → Path Check → Trust Check → Block/Allow
   ```

2. **Notification Path**:
   ```
   NotifyBlock() → Work Queue → Shared Memory → User-Mode Poll → Frontend Display
   ```

3. **Frontend Updates**:
   ```
   Shared Memory → Go Backend → WebSocket → JavaScript → DOM Update
   ```

### Memory Layout

**Shared Memory Structure:**
```
Offset 0x00: PID (uint32)
Offset 0x04: PathLen (uint32) 
Offset 0x08: ProcName[64] (byte array)
Offset 0x48: Ready (uint32)
Offset 0x4C: Path data (variable length, UTF-16)
```

---

## Performance Characteristics

### Kernel Driver
- **Memory Usage**: ~10MB for path storage and work items
- **CPU Impact**: Minimal - only processes `IRP_MJ_CREATE` operations
- **I/O Impact**: No significant performance degradation observed

### User-Mode Application
- **Memory Usage**: ~50-100MB (typical Wails application)
- **CPU Usage**: Low - polls shared memory every 100ms
- **Network**: Local WebSocket server only

---

## Security Analysis

### Current Protections
- **Kernel-level interception** of file system operations
- **Process signature verification** using Windows APIs
- **Real-time monitoring** with immediate blocking
- **Screen capture prevention** via display affinity

### Known Vulnerabilities
- **Process name spoofing**: Malware can name itself `chrome.exe`
- **Limited IRP coverage**: Only monitors file creation, not writes
- **Hardcoded paths**: Cannot adapt to new stealer techniques
- **No parent process checking**: Doesn't verify process genealogy
- **Bypass via alternate APIs**: Direct NT APIs might bypass filter

### Attack Vectors
1. **Filename Impersonation**: Copy legitimate process names
2. **Process Hollowing**: Inject into trusted processes
3. **Alternate Data Streams**: Access files via ADS
4. **Memory Mapping**: Direct memory access to files
5. **Registry Manipulation**: Modify trust settings

---

## Configuration Details

### Registry Settings
```
HKLM\SYSTEM\CurrentControlSet\Services\NoMoreStealer\
├── Type = 2 (FILE_DEVICE_FILE_SYSTEM)
├── Start = 3 (SERVICE_DEMAND_START)
├── ErrorControl = 1 (SERVICE_ERROR_NORMAL)
├── Group = "FSFilter Activity Monitor"
├── ImagePath = "\SystemRoot\System32\drivers\NoMoreStealer.sys"
└── Instances\NoMoreStealer Instance\
    ├── Altitude = "371000"
    └── Flags = 0
```

### Wails Configuration (`wails.json`)
```json
{
  "name": "NoMoreStealers Client",
  "width": 1024,
  "height": 768,
  "frameless": true,
  "backgroundColour": "#000000",
  "devServer": "localhost:34115"
}
```

---

## Limitations and Future Improvements

### Current Limitations
1. **Demo Status**: Explicitly not production-ready
2. **Single IRP Type**: Only monitors `IRP_MJ_CREATE`
3. **Static Configuration**: Paths hardcoded in driver
4. **Basic Trust Model**: Easily bypassed by sophisticated malware
5. **Debug Logging**: Relies on `DbgPrint` for kernel debugging

### Potential Improvements
1. **Enhanced IRP Coverage**: Monitor writes, renames, deletions
2. **Dynamic Configuration**: Runtime path updates via IOCTL
3. **Behavioral Analysis**: Process behavior monitoring
4. **Certificate Validation**: Proper code signing verification
5. **Machine Learning**: Anomaly detection for unknown threats

---

## Development Notes

### Build Requirements
- **Visual Studio 2019/2022** with Windows Driver Kit (WDK)
- **Go 1.19+** for Wails application
- **Node.js** for frontend dependencies (optional)

### Debugging Tools
- **DebugView**: Essential for viewing `DbgPrint` output
- **Process Monitor**: Useful for testing file access patterns
- **WinDbg**: For kernel debugging (advanced)

### Testing Approach
- **Virtual Machines**: Always test in isolated environment
- **System Restore**: Create restore points before testing
- **Driver Verifier**: Use for stability testing (advanced)

---

<div align="center">

**This architecture analysis is based on actual source code examination**

*Understanding the implementation helps in contributing and extending the project*

</div>