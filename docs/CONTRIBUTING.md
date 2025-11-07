# 🤝 Contributing to NoMoreStealer

> **Help improve this demonstration security project - contributions welcome!**

---

## Project Status

**This is explicitly a demonstration project** with known security limitations. Contributions are welcome to:
- Fix current implementation issues
- Add educational value
- Improve code quality
- Enhance documentation

**This is not intended for production use** - it's a learning and research project.

---

## Current Implementation Reality

### What Actually Works
- ✅ Basic `IRP_MJ_CREATE` interception via Filter Manager
- ✅ Hardcoded path protection (128 paths maximum)
- ✅ Simple process allowlists (filename-based)
- ✅ Shared memory communication (4KB section)
- ✅ Wails-based frontend with real-time events
- ✅ Digital signature verification using WinVerifyTrust
- ✅ Anti-spy overlay windows for screen capture blocking
- ✅ System tray integration

### Known Limitations & Issues

#### 🚨 Critical Security Issues

**1. Process Name Spoofing (Trivial Bypass)**
- **Problem**: Malware can name itself `chrome.exe` to bypass protection
- **Current Code**: Simple `strcmp()` against hardcoded filenames
- **Location**: `NoMoreStealer/internal/Process/process.cpp:64-89`
- **Impact**: Complete bypass by filename manipulation

**2. Limited IRP Coverage**
- **Problem**: Only monitors `IRP_MJ_CREATE`, ignores writes/modifications
- **Current Code**: `CallbacksArray[] = { { IRP_MJ_CREATE, 0, ... } }`
- **Location**: `NoMoreStealer/main.cpp:43-46`
- **Impact**: Malware can open files normally, then write/modify them

**3. Hardcoded Configuration**
- **Problem**: Protected paths compiled into driver, can't adapt
- **Current Code**: `DiscoverDefaultPaths()` with hardcoded strings
- **Location**: `NoMoreStealer/internal/Paths/paths.cpp:90-136`
- **Impact**: Cannot protect against new stealer techniques

#### 🔧 Implementation Issues

**4. No User-Mode Communication**
- **Problem**: Uses `DbgPrint` instead of proper notifications
- **Current Code**: `DbgPrint("[NoMoreStealer] ...")` throughout callbacks
- **Location**: `NoMoreStealer/internal/Callbacks/callbacks.cpp`
- **Impact**: Poor user experience, debug-only visibility

**5. Basic Trust Model**
- **Problem**: No parent process checking, no behavioral analysis
- **Current Code**: Simple allowlist + `PsIsProtectedProcessLight()`
- **Location**: `NoMoreStealer/internal/Process/process.cpp:93-101`
- **Impact**: Sophisticated malware can easily bypass

---

## Priority Contribution Areas

### 🔥 High Impact Improvements

#### 1. Enhanced Process Verification
**Goal**: Make process spoofing harder

**Current Implementation:**
```cpp
BOOLEAN IsKnownTrustedProcess(PEPROCESS process) {
    const CHAR* image = PsGetProcessImageFileName(process);
    return (!_stricmp(image, "chrome.exe") || ...);  // Easily spoofed
}
```

**Needed Improvements:**
- Parent process verification
- Process path validation (not just filename)
- Process genealogy tracking
- Code signing verification in kernel mode

**Skills Needed**: Windows kernel development, process management APIs

#### 2. Broader IRP Coverage
**Goal**: Monitor file modifications, not just creation

**Current Implementation:**
```cpp
FLT_OPERATION_REGISTRATION CallbacksArray[] = {
    { IRP_MJ_CREATE, 0, Callbacks::PreOperation, nullptr },  // Only this
    { IRP_MJ_OPERATION_END, 0, nullptr, nullptr }
};
```

**Needed Additions:**
- `IRP_MJ_WRITE` monitoring
- `IRP_MJ_SET_INFORMATION` for renames/deletes
- `IRP_MJ_READ` for sensitive data access
- Post-operation callbacks for cleanup

**Skills Needed**: Windows Filter Manager, IRP handling

#### 3. Dynamic Configuration
**Goal**: Update protected paths without recompiling

**Current Implementation:**
```cpp
void DiscoverDefaultPaths() {
    Add(L"\\Google\\Chrome\\User Data");  // Hardcoded
    Add(L"\\Microsoft\\Edge\\User Data"); // Hardcoded
    // ...
}
```

**Needed Implementation:**
- IOCTL interface for path management
- Registry-based configuration
- Runtime path addition/removal
- Configuration validation

**Skills Needed**: Windows IOCTL, registry APIs, kernel-user communication

### 🛠️ Technical Improvements

#### 4. Proper User-Mode Communication
**Goal**: Replace DbgPrint with real notifications

**Current Implementation:**
```cpp
DbgPrint("[NoMoreStealer] BLOCKED: Proc=%s PID=%lu Path=%wZ\n", ...);
```

**Needed Implementation:**
- Named pipe communication
- Event-based notifications
- Structured message format
- Reliable delivery mechanism

**Skills Needed**: Named pipes, Windows IPC, synchronization

#### 5. Anti-Bypass Mechanisms
**Goal**: Detect and prevent common bypass techniques

**Current Gaps:**
- No detection of process hollowing
- No monitoring of alternate data streams
- No protection against direct NT API usage
- No behavioral analysis

**Needed Features:**
- Process integrity checking
- API hooking detection
- Behavioral pattern analysis
- Memory scanning capabilities

**Skills Needed**: Advanced Windows internals, anti-malware techniques

---

## Getting Started

### Development Environment Setup

1. **Clone the Repository**
```bash
git clone https://github.com/yourusername/NoMoreStealers.git
cd NoMoreStealers
```

2. **Set Up Kernel Development**
- Visual Studio 2019/2022 with WDK
- Windows 10/11 SDK
- Test signing enabled system

3. **Set Up User-Mode Development**
```bash
# Install Go and Wails
go install github.com/wailsapp/wails/v2/cmd/wails@latest
cd NoMoreStealers_Usermode
go mod tidy
```

4. **Essential Tools**
- **DebugView**: For kernel debug output
- **Process Monitor**: For testing file access patterns
- **Virtual Machine**: For safe testing

### Code Structure Understanding

```
NoMoreStealers/
├── NoMoreStealer/              # Kernel driver (C++)
│   ├── main.cpp               # Entry point, filter registration
│   ├── internal/Callbacks/    # IRP_MJ_CREATE handler (MAIN LOGIC)
│   ├── internal/Paths/        # Protected path management
│   ├── internal/Process/      # Trust evaluation (NEEDS WORK)
│   └── internal/Comm/         # Shared memory (BASIC IMPLEMENTATION)
├── NoMoreStealers_Usermode/   # User application (Go + Wails)
│   ├── internal/app/         # Main app logic, event processing
│   ├── internal/comm/        # Shared memory reader
│   ├── internal/process/     # Signature verification
│   └── frontend/             # HTML/CSS/JavaScript UI
```

---

## Contribution Guidelines

### Before You Start

1. **Understand the Limitations**: This is a demo project with known bypasses
2. **Test Safely**: Always use VMs with snapshots
3. **Document Changes**: Explain what you're fixing and why
4. **Consider Impact**: Will this change break existing functionality?

### Code Style

#### Kernel Code (C++)
```cpp
// Use the existing namespace structure
namespace NoMoreStealer {
    namespace ModuleName {
        // PascalCase for functions
        NTSTATUS ProcessRequest(PFLT_CALLBACK_DATA Data);
        
        // camelCase for variables
        PFLT_FILTER gFilterHandle = nullptr;
        
        // UPPER_CASE for constants
        const ULONG MAX_PATH_LENGTH = 260;
    }
}
```

#### User-Mode Code (Go)
```go
// Follow standard Go conventions
func processDriverMessage(msg *DriverMessage) error {
    // Implementation
}

// Use meaningful names
var driverConnection *SharedMemoryReader
```

### Testing Requirements

#### For Kernel Changes
- [ ] Compiles without warnings in Release x64
- [ ] Tested in VM with system snapshots
- [ ] No system crashes during normal operation
- [ ] DebugView shows expected log messages
- [ ] Driver loads and unloads cleanly

#### For User-Mode Changes
- [ ] Go code follows standard conventions
- [ ] Frontend functionality works correctly
- [ ] No memory leaks in long-running tests
- [ ] Error handling implemented properly

### Specific Contribution Ideas

#### For Beginners
- **Documentation improvements**: Fix inaccuracies, add examples
- **UI enhancements**: Improve the Wails frontend
- **Testing**: Create test cases for different scenarios
- **Build scripts**: Automate compilation and deployment

#### For Intermediate Developers
- **Registry configuration**: Replace hardcoded paths
- **Enhanced logging**: Better structured logging system
- **Performance optimization**: Reduce memory usage, improve speed
- **Error handling**: More robust error recovery

#### For Advanced Developers
- **Process verification**: Implement parent process checking
- **IRP expansion**: Add support for write/modify operations
- **Anti-bypass**: Detect process hollowing, API hooking
- **Behavioral analysis**: Pattern recognition for unknown threats

---

## Submitting Contributions

### Pull Request Process

1. **Fork and Branch**
```bash
git checkout -b feature/your-improvement
```

2. **Make Changes**
- Focus on one issue per PR
- Include tests where applicable
- Update documentation

3. **Test Thoroughly**
- Test in clean VM environment
- Verify no regressions
- Check both kernel and user-mode components

4. **Submit PR**
- Clear description of changes
- Explain the problem being solved
- Include testing steps

### PR Template
```markdown
## Description
Brief description of the change and why it's needed.

## Type of Change
- [ ] Bug fix (non-breaking change)
- [ ] New feature (non-breaking change)
- [ ] Breaking change (fix or feature that would cause existing functionality to not work as expected)
- [ ] Documentation update

## Testing
- [ ] Tested in VM environment
- [ ] No system crashes observed
- [ ] DebugView output verified
- [ ] User-mode application functions correctly

## Security Impact
- [ ] No new security vulnerabilities introduced
- [ ] Existing bypasses not made worse
- [ ] Changes improve security posture

## Checklist
- [ ] Code follows project style guidelines
- [ ] Self-review completed
- [ ] Documentation updated if needed
- [ ] No merge conflicts
```

---

## Understanding Current Bypasses

### How Malware Currently Bypasses NoMoreStealer

1. **Filename Spoofing**
```cpp
// Current vulnerable code:
if (!_stricmp(image, "chrome.exe")) return TRUE;

// Bypass: Rename malware.exe to chrome.exe
```

2. **Write After Open**
```cpp
// Current limitation:
{ IRP_MJ_CREATE, 0, Callbacks::PreOperation, nullptr }

// Bypass: Open file normally, then write to it (no IRP_MJ_WRITE monitoring)
```

3. **Alternate Paths**
```cpp
// Current hardcoded protection:
Add(L"\\Google\\Chrome\\User Data");

// Bypass: Access via different path or symlink
```

4. **Process Injection**
```cpp
// Current trust check:
IsKnownTrustedProcess(process)  // Only checks current process

// Bypass: Inject into trusted process, no parent checking
```

Understanding these bypasses helps contributors focus on meaningful improvements.

---

## Research and Learning

### Recommended Reading
- **Windows Internals** by Russinovich & Solomon
- **Windows Kernel Programming** by Pavel Yosifovich
- **Rootkits and Bootkits** by Matrosov, Rodionov & Bratus

### Useful Resources
- [Microsoft Filter Manager Documentation](https://docs.microsoft.com/en-us/windows-hardware/drivers/ifs/filter-manager-architecture)
- [Windows Driver Kit (WDK) Samples](https://github.com/Microsoft/Windows-driver-samples)
- [OSR Online](https://www.osronline.com/) - Windows driver development community

### Testing Resources
- **Malware samples**: Use VirusTotal or malware research sites
- **VM environments**: VMware, VirtualBox with snapshots
- **Analysis tools**: Process Monitor, API Monitor, WinAPIOverride

---

## Recognition

### Contributors
All contributors will be recognized in the project README and documentation.

### Types of Contributions Valued
- 🐛 **Bug fixes** - Fixing implementation issues
- 🔒 **Security improvements** - Making bypasses harder
- 📚 **Documentation** - Improving accuracy and clarity
- 🧪 **Testing** - Finding edge cases and issues
- 💡 **Research** - Understanding new attack vectors
- 🎨 **UI/UX** - Making the interface better

---

## Communication

### Getting Help
- 🐛 **Bug reports**: [GitHub Issues](../../issues)
- 💡 **Feature discussions**: [GitHub Discussions](../../discussions)
- 📧 **Security issues**: Create private issue for sensitive topics

### Response Expectations
- **Issues**: Usually within 2-3 days
- **Pull Requests**: Within 1 week
- **Discussions**: Within 1-2 days

---

## Final Notes

### Remember
- This is a **learning project**, not production security software
- **Known bypasses exist** and are documented
- **Safety first** - always test in isolated environments
- **Quality over quantity** - well-tested small changes are better than large untested ones

### Project Goals
1. **Educational value** - Help people learn about Windows kernel development
2. **Research platform** - Provide a base for security research
3. **Community building** - Foster collaboration on security projects
4. **Awareness** - Demonstrate both protection techniques and their limitations

---

<div align="center">

**🚀 Ready to contribute? Pick an issue and let's improve this project together! 🚀**

*Every contribution, no matter how small, helps make this a better learning resource*

</div>
# 🤝 Contributing to NoMoreStealer

> **Help improve this demonstration security project - contributions welcome!**

---

## Project Status

**This is explicitly a demonstration project** with known security limitations. Contributions are welcome to:
- Fix current implementation issues
- Add educational value
- Improve code quality
- Enhance documentation

**This is not intended for production use** - it's a learning and research project.

---

## Current Implementation Reality

### What Actually Works
- ✅ Basic `IRP_MJ_CREATE` interception via Filter Manager
- ✅ Extended IRP coverage: `IRP_MJ_WRITE`, `IRP_MJ_SET_INFORMATION`, `IRP_MJ_READ`, `IRP_MJ_CLEANUP`
- ✅ Hardcoded path protection (128 paths maximum)
- ✅ Process path verification (not just filename matching)
- ✅ Support for process names >15 characters
- ✅ Memory-mapped file access protection
- ✅ Shared memory communication (4KB section)
- ✅ Wails-based frontend with real-time events
- ✅ Digital signature verification using WinVerifyTrust
- ✅ Anti-spy overlay windows for screen capture blocking
- ✅ System tray integration

### Known Limitations & Issues

#### 🚨 Critical Security Issues

**1. Process Name Spoofing** ✅ **FIXED**
- **Status**: Fixed - processes now require path verification
- **Implementation**: `VerifyTrustedProcessPath()` validates process installation path
- **Location**: `NoMoreStealer/internal/Process/process.cpp:218-278`
- **Remaining**: Process injection into trusted processes still possible

**2. Limited IRP Coverage** ✅ **IMPROVED**
- **Status**: Extended - now monitors `WRITE`, `SET_INFORMATION`, `READ`, `CLEANUP`, `FILE_SYSTEM_CONTROL`
- **Current Coverage**: `IRP_MJ_CREATE`, `IRP_MJ_WRITE`, `IRP_MJ_SET_INFORMATION`, `IRP_MJ_READ`, `IRP_MJ_CLEANUP`, `IRP_MJ_FILE_SYSTEM_CONTROL`
- **Location**: `NoMoreStealer/main.cpp:43-46`, `NoMoreStealer/internal/Callbacks/callbacks.cpp:195-202`
- **Remaining**: Post-operation callbacks not implemented

**3. Hardcoded Configuration**
- **Problem**: Protected paths compiled into driver, can't adapt
- **Current Code**: `DiscoverDefaultPaths()` with hardcoded strings
- **Location**: `NoMoreStealer/internal/Paths/paths.cpp:90-136`
- **Impact**: Cannot protect against new stealer techniques

#### 🔧 Implementation Issues

**4. User-Mode Communication** ✅ **IMPLEMENTED**
- **Status**: Fully implemented - Shared memory + work items + event system
- **Current Implementation**: 
  - Shared memory section (`\BaseNamedObjects\NoMoreStealerNotify`) with DACL security
  - Kernel work items (`IoQueueWorkItem`) for async notifications
  - User-mode polling loop (`monitorLoop`) reads from shared memory
  - Event channel + WebSocket broadcasting to frontend
- **Location**: 
  - Kernel: `NoMoreStealer/internal/Comm/comm.cpp` (shared memory + work items)
  - User-mode: `NoMoreStealers_Usermode/internal/comm/comm.go` (section reader)
  - User-mode: `NoMoreStealers_Usermode/internal/app/app.go:163-230` (monitor loop)
- **Details**: Uses `NotifyBlock()` from callbacks to queue work items that write to shared memory

**5. Trust Model** ✅ **IMPROVED**
- **Status**: Enhanced - path verification added, default deny implemented
- **Current Implementation**: Path verification + whitelist-only + process integrity checks
- **Location**: `NoMoreStealer/internal/Process/process.cpp:281-377, 819-898`
- **Remaining**: No parent process checking, no behavioral analysis

---

## Priority Contribution Areas

### 🔥 High Impact Improvements

#### 1. Enhanced Process Verification ✅ **PARTIALLY IMPLEMENTED**
**Goal**: Make process spoofing harder

**Current Implementation:**
```cpp
BOOLEAN IsKnownTrustedProcess(PEPROCESS process) {
    // Now includes path verification
    if (!_stricmp(fileName, trustedName)) {
        if (!VerifyTrustedProcessPath(process, fileName)) {
            return FALSE;  // Path verification required
        }
    }
}
```

**Completed:**
- ✅ Process path validation (not just filename)
- ✅ Device path matching (handles `\Device\...` paths)
- ✅ Double backslash path handling

**Still Needed:**
- Parent process verification
- Process genealogy tracking
- Code signing verification in kernel mode

**Skills Needed**: Windows kernel development, process management APIs

#### 2. Broader IRP Coverage ✅ **IMPROVED**
**Goal**: Monitor file modifications, not just creation

**Current Implementation:**
```cpp
FLT_OPERATION_REGISTRATION CallbacksArray[] = {
    { IRP_MJ_CREATE, 0, Callbacks::PreOperation, nullptr },
    { IRP_MJ_WRITE, 0, Callbacks::PreOperation, nullptr },        // ✅ Added
    { IRP_MJ_SET_INFORMATION, 0, Callbacks::PreOperation, nullptr }, // ✅ Added
    { IRP_MJ_READ, 0, Callbacks::PreOperation, nullptr },        // ✅ Added
    { IRP_MJ_CLEANUP, 0, Callbacks::PreOperation, nullptr },      // ✅ Added
    { IRP_MJ_FILE_SYSTEM_CONTROL, 0, Callbacks::PreOperation, nullptr }, // ✅ Added
    { IRP_MJ_OPERATION_END, 0, nullptr, nullptr }
};
```

**Completed:**
- ✅ `IRP_MJ_WRITE` monitoring
- ✅ `IRP_MJ_SET_INFORMATION` for renames/deletes
- ✅ `IRP_MJ_READ` for sensitive data access
- ✅ Memory-mapped file access protection

**Still Needed:**
- Post-operation callbacks for cleanup

**Skills Needed**: Windows Filter Manager, IRP handling

#### 3. Dynamic Configuration
**Goal**: Update protected paths without recompiling

**Current Implementation:**
```cpp
void DiscoverDefaultPaths() {
    Add(L"\\Google\\Chrome\\User Data");  // Hardcoded
    Add(L"\\Microsoft\\Edge\\User Data"); // Hardcoded
    // ...
}
```

**Needed Implementation:**
- IOCTL interface for path management
- Registry-based configuration
- Runtime path addition/removal
- Configuration validation

**Skills Needed**: Windows IOCTL, registry APIs, kernel-user communication

### 🛠️ Technical Improvements

#### 4. Proper User-Mode Communication ✅ **IMPLEMENTED**
**Goal**: Replace DbgPrint with real notifications

**Current Implementation:**
```cpp
// Kernel side - comm.cpp
VOID NotifyBlock(_In_ PUNICODE_STRING path, _In_opt_ const CHAR* procNameAnsi, _In_ ULONG pid) {
    // Creates work item, queues it, writes to shared memory section
    IoQueueWorkItem(workItem->WorkItem, NotifyWorkRoutine, DelayedWorkQueue, workItem);
}

// User-mode side - app.go
func (a *App) monitorLoop() {
    // Polls shared memory section for new notifications
    if currentReady == 1 && a.LastReady == 0 {
        // Reads path, process name, PID from shared memory
        // Creates Event, broadcasts via WebSocket
    }
}
```

**Needed Implementation:**
- Named pipe communication
- Event-based notifications
- Structured message format
- Reliable delivery mechanism

**Skills Needed**: Named pipes, Windows IPC, synchronization

#### 5. Anti-Bypass Mechanisms
**Goal**: Detect and prevent common bypass techniques

**Current Gaps:**
- No detection of process hollowing
- No monitoring of alternate data streams
- No protection against direct NT API usage
- No behavioral analysis

**Needed Features:**
- Process integrity checking
- API hooking detection
- Behavioral pattern analysis
- Memory scanning capabilities

**Skills Needed**: Advanced Windows internals, anti-malware techniques

---

## Getting Started

### Development Environment Setup

1. **Clone the Repository**
```bash
git clone https://github.com/yourusername/NoMoreStealers.git
cd NoMoreStealers
```

2. **Set Up Kernel Development**
- Visual Studio 2019/2022 with WDK
- Windows 10/11 SDK
- Test signing enabled system

3. **Set Up User-Mode Development**
```bash
# Install Go and Wails
go install github.com/wailsapp/wails/v2/cmd/wails@latest
cd NoMoreStealers_Usermode
go mod tidy
```

4. **Essential Tools**
- **DebugView**: For kernel debug output
- **Process Monitor**: For testing file access patterns
- **Virtual Machine**: For safe testing

### Code Structure Understanding

```
NoMoreStealers/
├── NoMoreStealer/              # Kernel driver (C++)
│   ├── main.cpp               # Entry point, filter registration
│   ├── internal/Callbacks/    # IRP_MJ_CREATE handler (MAIN LOGIC)
│   ├── internal/Paths/        # Protected path management
│   ├── internal/Process/      # Trust evaluation (NEEDS WORK)
│   └── internal/Comm/         # Shared memory (BASIC IMPLEMENTATION)
├── NoMoreStealers_Usermode/   # User application (Go + Wails)
│   ├── internal/app/         # Main app logic, event processing
│   ├── internal/comm/        # Shared memory reader
│   ├── internal/process/     # Signature verification
│   └── frontend/             # HTML/CSS/JavaScript UI
```

---

## Contribution Guidelines

### Before You Start

1. **Understand the Limitations**: This is a demo project with known bypasses
2. **Test Safely**: Always use VMs with snapshots
3. **Document Changes**: Explain what you're fixing and why
4. **Consider Impact**: Will this change break existing functionality?

### Code Style

#### Kernel Code (C++)
```cpp
// Use the existing namespace structure
namespace NoMoreStealer {
    namespace ModuleName {
        // PascalCase for functions
        NTSTATUS ProcessRequest(PFLT_CALLBACK_DATA Data);
        
        // camelCase for variables
        PFLT_FILTER gFilterHandle = nullptr;
        
        // UPPER_CASE for constants
        const ULONG MAX_PATH_LENGTH = 260;
    }
}
```

#### User-Mode Code (Go)
```go
// Follow standard Go conventions
func processDriverMessage(msg *DriverMessage) error {
    // Implementation
}

// Use meaningful names
var driverConnection *SharedMemoryReader
```

### Testing Requirements

#### For Kernel Changes
- [ ] Compiles without warnings in Release x64
- [ ] Tested in VM with system snapshots
- [ ] No system crashes during normal operation
- [ ] DebugView shows expected log messages
- [ ] Driver loads and unloads cleanly

#### For User-Mode Changes
- [ ] Go code follows standard conventions
- [ ] Frontend functionality works correctly
- [ ] No memory leaks in long-running tests
- [ ] Error handling implemented properly

### Specific Contribution Ideas

#### For Beginners
- **Documentation improvements**: Fix inaccuracies, add examples
- **UI enhancements**: Improve the Wails frontend
- **Testing**: Create test cases for different scenarios
- **Build scripts**: Automate compilation and deployment

#### For Intermediate Developers
- **Registry configuration**: Replace hardcoded paths
- **Enhanced logging**: Better structured logging system
- **Performance optimization**: Reduce memory usage, improve speed
- **Error handling**: More robust error recovery

#### For Advanced Developers
- **Process verification**: Implement parent process checking
- **IRP expansion**: Add support for write/modify operations
- **Anti-bypass**: Detect process hollowing, API hooking
- **Behavioral analysis**: Pattern recognition for unknown threats

---

## Submitting Contributions

### Pull Request Process

1. **Fork and Branch**
```bash
git checkout -b feature/your-improvement
```

2. **Make Changes**
- Focus on one issue per PR
- Include tests where applicable
- Update documentation

3. **Test Thoroughly**
- Test in clean VM environment
- Verify no regressions
- Check both kernel and user-mode components

4. **Submit PR**
- Clear description of changes
- Explain the problem being solved
- Include testing steps

### PR Template
```markdown
## Description
Brief description of the change and why it's needed.

## Type of Change
- [ ] Bug fix (non-breaking change)
- [ ] New feature (non-breaking change)
- [ ] Breaking change (fix or feature that would cause existing functionality to not work as expected)
- [ ] Documentation update

## Testing
- [ ] Tested in VM environment
- [ ] No system crashes observed
- [ ] DebugView output verified
- [ ] User-mode application functions correctly

## Security Impact
- [ ] No new security vulnerabilities introduced
- [ ] Existing bypasses not made worse
- [ ] Changes improve security posture

## Checklist
- [ ] Code follows project style guidelines
- [ ] Self-review completed
- [ ] Documentation updated if needed
- [ ] No merge conflicts
```

---

## Understanding Current Bypasses

### How Malware Currently Bypasses NoMoreStealer

1. **Filename Spoofing** ✅ **FIXED**
```cpp
// Fixed - now requires path verification:
if (!_stricmp(fileName, "chrome.exe")) {
    if (!VerifyTrustedProcessPath(process, fileName)) {
        return FALSE;  // Path must match trusted location
    }
}
// Bypass: No longer possible - path verification required
```

2. **Write After Open** ✅ **FIXED**
```cpp
// Fixed - now monitors IRP_MJ_WRITE:
{ IRP_MJ_WRITE, 0, Callbacks::PreOperation, nullptr },  // ✅ Added
// Bypass: No longer possible - writes are monitored
```

3. **Alternate Paths**
```cpp
// Current hardcoded protection:
Add(L"\\Google\\Chrome\\User Data");

// Bypass: Access via different path or symlink
```

4. **Process Injection**
```cpp
// Current trust check:
IsKnownTrustedProcess(process)  // Only checks current process

// Bypass: Inject into trusted process, no parent checking
```

Understanding these bypasses helps contributors focus on meaningful improvements.

---

## Research and Learning

### Recommended Reading
- **Windows Internals** by Russinovich & Solomon
- **Windows Kernel Programming** by Pavel Yosifovich
- **Rootkits and Bootkits** by Matrosov, Rodionov & Bratus

### Useful Resources
- [Microsoft Filter Manager Documentation](https://docs.microsoft.com/en-us/windows-hardware/drivers/ifs/filter-manager-architecture)
- [Windows Driver Kit (WDK) Samples](https://github.com/Microsoft/Windows-driver-samples)
- [OSR Online](https://www.osronline.com/) - Windows driver development community

### Testing Resources
- **Malware samples**: Use VirusTotal or malware research sites
- **VM environments**: VMware, VirtualBox with snapshots
- **Analysis tools**: Process Monitor, API Monitor, WinAPIOverride

---

## Recognition

### Contributors
All contributors will be recognized in the project README and documentation.

### Types of Contributions Valued
- 🐛 **Bug fixes** - Fixing implementation issues
- 🔒 **Security improvements** - Making bypasses harder
- 📚 **Documentation** - Improving accuracy and clarity
- 🧪 **Testing** - Finding edge cases and issues
- 💡 **Research** - Understanding new attack vectors
- 🎨 **UI/UX** - Making the interface better

---

## Communication

### Getting Help
- 🐛 **Bug reports**: [GitHub Issues](../../issues)
- 💡 **Feature discussions**: [GitHub Discussions](../../discussions)
- 📧 **Security issues**: Create private issue for sensitive topics

### Response Expectations
- **Issues**: Usually within 2-3 days
- **Pull Requests**: Within 1 week
- **Discussions**: Within 1-2 days

---

## Final Notes

### Remember
- This is a **learning project**, not production security software
- **Known bypasses exist** and are documented
- **Safety first** - always test in isolated environments
- **Quality over quantity** - well-tested small changes are better than large untested ones

### Project Goals
1. **Educational value** - Help people learn about Windows kernel development
2. **Research platform** - Provide a base for security research
3. **Community building** - Foster collaboration on security projects
4. **Awareness** - Demonstrate both protection techniques and their limitations

---

<div align="center">

**🚀 Ready to contribute? Pick an issue and let's improve this project together! 🚀**

*Every contribution, no matter how small, helps make this a better learning resource*

</div>
